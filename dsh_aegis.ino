/* ============================================================================
 *  DSH-AEGIS  —  a physical password manager
 *  ----------------------------------------------------------------------------
 *  ESP32-S3-WROOM-1  •  128x32 SSD1306 OLED  •  6 buttons  •  SD card vault
 *  •  native USB-C HID keyboard
 *
 *  The idea: your passwords live encrypted on the SD card. You unlock the
 *  device with a PIN (entered on the buttons), scroll through your entries on
 *  the OLED, and when you hit PLAY the selected password gets *typed* into
 *  whatever field your computer's cursor is sitting in — because AEGIS shows
 *  up to the host as a plain USB keyboard. Nothing leaves the device except
 *  keystrokes.
 *
 *  Buttons (from the schematic):
 *     RECORD  IO0   start adding a new entry (over the USB serial console)
 *     SAVE    IO1   commit the vault to the SD card (encrypted)
 *     PLAY    IO2   type the selected entry
 *     SCROLL← IO3   previous
 *     SELECT  IO4   confirm / open
 *     SCROLL→ IO5   next
 *     (SCL IO6 / SDA IO7 -> OLED)
 *
 *  Arduino IDE board settings that matter:
 *     Board:            "ESP32S3 Dev Module"
 *     USB CDC On Boot:  Enabled
 *     USB Mode:         USB-OTG (TinyUSB)
 *     PSRAM:            Disabled   (or "QSPI PSRAM" if your part is an R2)
 *                       -- NOT "OPI PSRAM": that would claim IO35-37, which the
 *                          SD card is using on this board.
 *
 *  Libraries: U8g2 (OLED), plus the SD/SPI/USB stuff that ships with the
 *  ESP32 Arduino core. Crypto is mbedTLS, also already in the core.
 * ========================================================================== */

#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include <U8g2lib.h>
#include <vector>

#include "USB.h"
#include "USBHIDKeyboard.h"

#include "mbedtls/aes.h"
#include "mbedtls/md.h"
#include "esp_random.h"

/* ----------------------------------------------------------------------------
 *  1. WIRING  —  the only part you should need to touch
 * -------------------------------------------------------------------------- */

// Buttons + I2C: these I read straight off your schematic.
#define PIN_RECORD   0
#define PIN_SAVE     1
#define PIN_PLAY     2
#define PIN_SCROLL_L 3
#define PIN_SELECT   4
#define PIN_SCROLL_R 5
#define PIN_SCL      6
#define PIN_SDA      7

// SD card (SPI), straight off the schematic.
// Note: the symbol brackets IO35-37 as "PSRAM" because on an *R8* (octal PSRAM)
// module those pins are wired to the PSRAM die and are unusable. This build
// targets a module with no PSRAM / quad PSRAM, where they're free GPIO. If you
// ever swap in an R8 part, these three have to move.
#define PIN_SD_CS    35
#define PIN_SD_MOSI  36
#define PIN_SD_SCK   37
#define PIN_SD_MISO  38

/* ----------------------------------------------------------------------------
 *  2. FEEL  —  knobs for behaviour, tune to taste
 * -------------------------------------------------------------------------- */

static const char*    VAULT_PATH     = "/AEGIS.VAULT";
static const uint8_t  PIN_LENGTH     = 4;        // digits in the master PIN
static const uint32_t KDF_ITERATIONS = 60000;    // PBKDF2 rounds (unlock cost)
static const uint32_t IDLE_LOCK_MS   = 60000;    // auto-lock after inactivity
static const uint8_t  TYPE_DELAY_MS  = 6;        // per-keystroke, HID reliability
static const uint8_t  GEN_PW_LENGTH  = 20;       // default generated password len

/* ----------------------------------------------------------------------------
 *  3. HARDWARE OBJECTS
 * -------------------------------------------------------------------------- */

// Full-frame buffer so we can animate freely (U8g2 clips off-screen draws).
U8G2_SSD1306_128X32_UNIVISION_F_HW_I2C oled(U8G2_R0, U8X8_PIN_NONE);
USBHIDKeyboard Keyboard;
SPIClass sdSPI(FSPI);

/* ----------------------------------------------------------------------------
 *  4. THE VAULT
 * -------------------------------------------------------------------------- */

struct Entry {
  String label;      // "GitHub"
  String username;   // "pratik@…"
  String password;   // the secret
};

std::vector<Entry> vault;
uint8_t  masterPin[PIN_LENGTH];    // held in RAM only while unlocked
bool     vaultDirty = false;       // true = unsaved changes, nudge the SAVE btn
bool     unlocked   = false;

/* ----------------------------------------------------------------------------
 *  5. CRYPTO
 *  PBKDF2-HMAC-SHA256 stretches the PIN into a key; AES-256-CBC encrypts the
 *  vault; an HMAC over the ciphertext lets us reject a wrong PIN (or tampering)
 *  before we ever try to decrypt. Small, boring, and correct — the good kind.
 * -------------------------------------------------------------------------- */

static void hmacSha256(const uint8_t* key, size_t klen,
                       const uint8_t* msg, size_t mlen, uint8_t* out /*32*/) {
  mbedtls_md_context_t ctx;
  mbedtls_md_init(&ctx);
  mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), 1);
  mbedtls_md_hmac_starts(&ctx, key, klen);
  mbedtls_md_hmac_update(&ctx, msg, mlen);
  mbedtls_md_hmac_finish(&ctx, out);
  mbedtls_md_free(&ctx);
}

// Rolled by hand so it works the same on every core version.
static void pbkdf2(const uint8_t* pw, size_t pwlen,
                   const uint8_t* salt, size_t slen,
                   uint32_t iters, uint8_t* out, size_t outlen) {
  uint32_t blocks = (outlen + 31) / 32;
  uint8_t  U[32], T[32], block[64];
  for (uint32_t i = 1; i <= blocks; i++) {
    memcpy(block, salt, slen);
    block[slen + 0] = (i >> 24) & 0xff;
    block[slen + 1] = (i >> 16) & 0xff;
    block[slen + 2] = (i >>  8) & 0xff;
    block[slen + 3] =  i        & 0xff;
    hmacSha256(pw, pwlen, block, slen + 4, U);
    memcpy(T, U, 32);
    for (uint32_t j = 1; j < iters; j++) {
      hmacSha256(pw, pwlen, U, 32, U);
      for (int k = 0; k < 32; k++) T[k] ^= U[k];
    }
    size_t off = (i - 1) * 32;
    size_t n   = (outlen - off < 32) ? (outlen - off) : 32;
    memcpy(out + off, T, n);
  }
}

static void randomBytes(uint8_t* buf, size_t n) {
  for (size_t i = 0; i < n; i++) buf[i] = (uint8_t)(esp_random() & 0xff);
}

// PKCS#7 pad -> AES-256-CBC. `out` must have room for the padded length.
static size_t aesEncrypt(const uint8_t* key, const uint8_t* iv,
                         const uint8_t* in, size_t inLen, uint8_t* out) {
  size_t pad     = 16 - (inLen % 16);          // 1..16, always adds a block if aligned
  size_t total   = inLen + pad;
  memcpy(out, in, inLen);
  for (size_t i = 0; i < pad; i++) out[inLen + i] = (uint8_t)pad;

  uint8_t ivCopy[16];  memcpy(ivCopy, iv, 16);  // CBC mutates the IV
  mbedtls_aes_context aes; mbedtls_aes_init(&aes);
  mbedtls_aes_setkey_enc(&aes, key, 256);
  mbedtls_aes_crypt_cbc(&aes, MBEDTLS_AES_ENCRYPT, total, ivCopy, out, out);
  mbedtls_aes_free(&aes);
  return total;
}

// Returns plaintext length, or 0 if the padding is nonsense.
static size_t aesDecrypt(const uint8_t* key, const uint8_t* iv,
                         const uint8_t* in, size_t inLen, uint8_t* out) {
  if (inLen == 0 || inLen % 16) return 0;
  uint8_t ivCopy[16]; memcpy(ivCopy, iv, 16);
  mbedtls_aes_context aes; mbedtls_aes_init(&aes);
  mbedtls_aes_setkey_dec(&aes, key, 256);
  mbedtls_aes_crypt_cbc(&aes, MBEDTLS_AES_DECRYPT, inLen, ivCopy, in, out);
  mbedtls_aes_free(&aes);

  uint8_t pad = out[inLen - 1];
  if (pad == 0 || pad > 16) return 0;
  for (uint8_t i = 0; i < pad; i++)          // constant-ish padding check
    if (out[inLen - 1 - i] != pad) return 0;
  return inLen - pad;
}

/* ----------------------------------------------------------------------------
 *  6. SERIALISE  <->  DISK
 *  On the wire the vault is just tab-separated lines: label \t user \t pass.
 *  We forbid tabs/newlines inside fields, so parsing stays trivial.
 * -------------------------------------------------------------------------- */

static String serializeVault() {
  String s;
  for (auto& e : vault) {
    s += e.label; s += '\t';
    s += e.username; s += '\t';
    s += e.password; s += '\n';
  }
  return s;
}

static void parseVault(const String& plain) {
  vault.clear();
  int start = 0;
  while (start < (int)plain.length()) {
    int nl = plain.indexOf('\n', start);
    if (nl < 0) nl = plain.length();
    String line = plain.substring(start, nl);
    start = nl + 1;
    if (line.length() == 0) continue;

    int t1 = line.indexOf('\t');
    int t2 = line.indexOf('\t', t1 + 1);
    if (t1 < 0 || t2 < 0) continue;
    Entry e;
    e.label    = line.substring(0, t1);
    e.username = line.substring(t1 + 1, t2);
    e.password = line.substring(t2 + 1);
    vault.push_back(e);
  }
}

// File layout: "AEGIS1" | salt[16] | iv[16] | u32 cipherLen | cipher | mac[32]
static bool saveVault() {
  uint8_t salt[16], iv[16], keyMaterial[64];
  randomBytes(salt, 16);
  randomBytes(iv, 16);
  pbkdf2(masterPin, PIN_LENGTH, salt, 16, KDF_ITERATIONS, keyMaterial, 64);
  const uint8_t* encKey = keyMaterial;       // first 32 bytes
  const uint8_t* macKey = keyMaterial + 32;  // next  32 bytes

  String plain = serializeVault();
  std::vector<uint8_t> cipher(plain.length() + 16);
  size_t cipherLen = aesEncrypt(encKey, iv,
                                (const uint8_t*)plain.c_str(),
                                plain.length(), cipher.data());

  // MAC covers salt+iv+len+cipher so nothing can be swapped around underneath us.
  std::vector<uint8_t> macIn;
  macIn.insert(macIn.end(), salt, salt + 16);
  macIn.insert(macIn.end(), iv, iv + 16);
  uint32_t clen = cipherLen;
  macIn.push_back(clen & 0xff);       macIn.push_back((clen >> 8) & 0xff);
  macIn.push_back((clen >> 16)&0xff); macIn.push_back((clen >> 24)& 0xff);
  macIn.insert(macIn.end(), cipher.begin(), cipher.begin() + cipherLen);
  uint8_t mac[32];
  hmacSha256(macKey, 32, macIn.data(), macIn.size(), mac);

  SD.remove(VAULT_PATH);
  File f = SD.open(VAULT_PATH, FILE_WRITE);
  if (!f) return false;
  f.write((const uint8_t*)"AEGIS1", 6);
  f.write(salt, 16);
  f.write(iv, 16);
  f.write((uint8_t*)&clen, 4);
  f.write(cipher.data(), cipherLen);
  f.write(mac, 32);
  f.close();

  memset(keyMaterial, 0, sizeof(keyMaterial));  // don't leave keys lying around
  vaultDirty = false;
  return true;
}

// Tries to unlock with the PIN currently in masterPin[]. Returns false on a
// bad PIN (or a corrupt/absent file) without leaking which.
static bool loadVault() {
  File f = SD.open(VAULT_PATH, FILE_READ);
  if (!f) return false;

  char magic[6];
  if (f.read((uint8_t*)magic, 6) != 6 || memcmp(magic, "AEGIS1", 6)) { f.close(); return false; }
  uint8_t salt[16], iv[16]; uint32_t clen = 0;
  f.read(salt, 16); f.read(iv, 16); f.read((uint8_t*)&clen, 4);
  if (clen == 0 || clen > 65536) { f.close(); return false; }

  std::vector<uint8_t> cipher(clen);
  if (f.read(cipher.data(), clen) != (int)clen) { f.close(); return false; }
  uint8_t macFile[32];
  if (f.read(macFile, 32) != 32) { f.close(); return false; }
  f.close();

  uint8_t keyMaterial[64];
  pbkdf2(masterPin, PIN_LENGTH, salt, 16, KDF_ITERATIONS, keyMaterial, 64);
  const uint8_t* encKey = keyMaterial;
  const uint8_t* macKey = keyMaterial + 32;

  std::vector<uint8_t> macIn;
  macIn.insert(macIn.end(), salt, salt + 16);
  macIn.insert(macIn.end(), iv, iv + 16);
  macIn.push_back(clen & 0xff);        macIn.push_back((clen >> 8) & 0xff);
  macIn.push_back((clen >> 16) & 0xff); macIn.push_back((clen >> 24) & 0xff);
  macIn.insert(macIn.end(), cipher.begin(), cipher.end());
  uint8_t macCalc[32];
  hmacSha256(macKey, 32, macIn.data(), macIn.size(), macCalc);

  if (memcmp(macCalc, macFile, 32) != 0) {      // wrong PIN or tampered file
    memset(keyMaterial, 0, sizeof(keyMaterial));
    return false;
  }

  std::vector<uint8_t> plain(clen + 1, 0);
  size_t plainLen = aesDecrypt(encKey, iv, cipher.data(), clen, plain.data());
  memset(keyMaterial, 0, sizeof(keyMaterial));
  if (plainLen == 0) return false;
  plain[plainLen] = 0;

  parseVault(String((const char*)plain.data()));
  return true;
}

/* ----------------------------------------------------------------------------
 *  7. PASSWORD GENERATOR
 * -------------------------------------------------------------------------- */

static String generatePassword(uint8_t len) {
  static const char* set =
      "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789"
      "!@#$%^&*()-_=+[]{}";
  const int n = strlen(set);
  String out;
  for (uint8_t i = 0; i < len; i++) out += set[esp_random() % n];
  return out;
}

/* ----------------------------------------------------------------------------
 *  8. USB HID  —  type a string into the host
 * -------------------------------------------------------------------------- */

static void typeString(const String& s) {
  for (size_t i = 0; i < s.length(); i++) {
    Keyboard.write(s[i]);
    delay(TYPE_DELAY_MS);
  }
}

/* ----------------------------------------------------------------------------
 *  9. BUTTONS  —  debounced edge detection
 * -------------------------------------------------------------------------- */

struct Button {
  uint8_t  pin;
  bool     stable;       // debounced level (true = pressed)
  bool     lastRaw;
  uint32_t lastChange;
  bool     pressedEdge;  // one-shot: set on press, cleared by consumer
};

Button buttons[6] = {
  {PIN_RECORD, false, false, 0, false},
  {PIN_SAVE,   false, false, 0, false},
  {PIN_PLAY,   false, false, 0, false},
  {PIN_SCROLL_L, false, false, 0, false},
  {PIN_SELECT, false, false, 0, false},
  {PIN_SCROLL_R, false, false, 0, false},
};
enum { B_RECORD, B_SAVE, B_PLAY, B_LEFT, B_SELECT, B_RIGHT };

static void buttonsBegin() {
  for (auto& b : buttons) pinMode(b.pin, INPUT_PULLUP);  // pressed = LOW
}

static void buttonsUpdate() {
  uint32_t now = millis();
  for (auto& b : buttons) {
    bool raw = (digitalRead(b.pin) == LOW);
    if (raw != b.lastRaw) { b.lastRaw = raw; b.lastChange = now; }
    if (now - b.lastChange > 25 && raw != b.stable) {  // 25ms debounce
      b.stable = raw;
      if (raw) b.pressedEdge = true;                   // fire on press
    }
  }
}

static bool took(uint8_t idx) {           // consume a press edge, once
  if (buttons[idx].pressedEdge) { buttons[idx].pressedEdge = false; return true; }
  return false;
}

/* ----------------------------------------------------------------------------
 *  10. UI  —  the "iOS on a stamp-sized screen" part
 *  Everything animated is just a float easing toward a target every frame
 *  (exponential smoothing). It reads as springy and liquid and costs nothing.
 * -------------------------------------------------------------------------- */

enum Screen { SC_SETPIN, SC_LOCK, SC_LIST, SC_ACTIONS, SC_REVEAL, SC_ADD, SC_TOAST };
Screen screen = SC_LOCK;
Screen toastReturn = SC_LIST;

// smoothed animation state
float carousel   = 0;   // eases toward selectedIndex (horizontal card slide)
float pillY      = 0;   // eases toward the selected menu row
float shake      = 0;   // decays to 0; nonzero = wrong-PIN wiggle
uint32_t lastActivity = 0;

int  selectedIndex = 0;     // which vault entry
int  actionIndex   = 0;     // which action in the SC_ACTIONS menu
int  pinDigits[PIN_LENGTH]; // digits being entered
int  pinCursor = 0;
int  pinPass   = 0;         // first-run needs two matching passes
int  pinFirst[PIN_LENGTH];

String toastText;
uint32_t toastUntil = 0;

static void bumpActivity() { lastActivity = millis(); }

static void toast(const String& t, uint32_t ms, Screen back) {
  toastText = t; toastUntil = millis() + ms; toastReturn = back; screen = SC_TOAST;
}

// approach `target` by a fraction each frame — the whole animation system
static float ease(float cur, float target, float k = 0.22f) {
  float d = target - cur;
  if (fabs(d) < 0.001f) return target;
  return cur + d * k;
}

// draw text clipped to the panel, scrolling horizontally if it's too wide
static void drawMarquee(const char* txt, int x, int y, int w, uint32_t now) {
  int tw = oled.getStrWidth(txt);
  if (tw <= w) { oled.drawStr(x, y, txt); return; }
  int span   = tw + 20;                       // gap before it loops
  int offset = (now / 40) % span;
  oled.setClipWindow(x, 0, x + w, 32);
  oled.drawStr(x - offset, y, txt);
  oled.drawStr(x - offset + span, y, txt);
  oled.setMaxClipWindow();
}

// ---- individual screens -----------------------------------------------------

static void drawPinScreen(const char* title, uint32_t now) {
  oled.setFont(u8g2_font_5x8_tf);
  int tw = oled.getStrWidth(title);
  oled.drawStr((128 - tw) / 2, 8, title);

  // four rounded slots that fill as you enter digits
  int slotW = 20, gap = 6, total = PIN_LENGTH * slotW + (PIN_LENGTH - 1) * gap;
  int x0 = (128 - total) / 2 + (int)shake;
  oled.setFont(u8g2_font_7x14B_tf);
  for (int i = 0; i < PIN_LENGTH; i++) {
    int x = x0 + i * (slotW + gap);
    oled.drawRFrame(x, 14, slotW, 16, 4);
    if (i < pinCursor) {
      char d[2] = { (char)('0' + pinDigits[i]), 0 };
      oled.drawStr(x + (slotW - 7) / 2, 26, d);        // already-entered
    } else if (i == pinCursor) {
      char d[2] = { (char)('0' + pinDigits[i]), 0 };
      oled.drawRBox(x, 14, slotW, 16, 4);              // active slot: inverted
      oled.setDrawColor(0);
      oled.drawStr(x + (slotW - 7) / 2, 26, d);
      oled.setDrawColor(1);
    }
  }
}

static void drawList(uint32_t now) {
  if (vault.empty()) {
    oled.setFont(u8g2_font_6x12_tf);
    const char* m = "Vault empty";
    oled.drawStr((128 - oled.getStrWidth(m)) / 2, 15, m);
    oled.setFont(u8g2_font_5x8_tf);
    const char* h = "RECORD to add";
    oled.drawStr((128 - oled.getStrWidth(h)) / 2, 27, h);
    return;
  }

  // horizontal carousel: current card centred, neighbours sliding in/out
  for (int i = 0; i < (int)vault.size(); i++) {
    float rel = i - carousel;               // 0 = dead centre
    if (fabs(rel) > 1.4f) continue;         // offscreen, skip
    int cx = 64 + (int)(rel * 128);
    int alpha = (fabs(rel) < 0.5f) ? 1 : 0; // only the centred one gets full text

    oled.setFont(u8g2_font_7x14B_tf);
    const char* label = vault[i].label.c_str();
    if (alpha) {
      drawMarquee(label, cx - 58, 16, 116, now);
    } else {
      int tw = oled.getStrWidth(label);
      oled.drawStr(cx - tw / 2, 16, label);
    }
  }

  // little iOS-style page dots
  int dots = vault.size();
  int dw = dots * 6 - 2, dx = (128 - dw) / 2;
  for (int i = 0; i < dots; i++) {
    if (i == selectedIndex) oled.drawDisc(dx + i * 6 + 1, 28, 1);
    else                    oled.drawPixel(dx + i * 6 + 1, 28);
  }
}

static const char* ACTIONS[] = { "Type password", "Type user+pass", "Reveal", "Delete", "Back" };
static const int   N_ACTIONS = 5;

static void drawActions(uint32_t now) {
  oled.setFont(u8g2_font_5x8_tf);
  const char* head = vault[selectedIndex].label.c_str();
  oled.drawStr(2, 7, head);
  oled.drawHLine(0, 9, 128);

  // sliding selection pill; two rows visible in the 32px panel
  int rowH = 11;
  int top  = actionIndex * rowH;            // scroll so the selection stays put-ish
  int viewTop = (actionIndex >= 1) ? (actionIndex - 1) * rowH : 0;
  oled.setFont(u8g2_font_6x12_tf);
  for (int i = 0; i < N_ACTIONS; i++) {
    int y = 11 + i * rowH - viewTop;
    if (y < 10 || y > 32) continue;
    if (i == actionIndex) {
      oled.drawRBox(1, y, 126, rowH - 1, 3);
      oled.setDrawColor(0);
      oled.drawStr(6, y + 9, ACTIONS[i]);
      oled.setDrawColor(1);
    } else {
      oled.drawStr(6, y + 9, ACTIONS[i]);
    }
  }
}

static void drawReveal(uint32_t now) {
  oled.setFont(u8g2_font_5x8_tf);
  oled.drawStr(2, 7, "Password:");
  oled.setFont(u8g2_font_7x14B_tf);
  drawMarquee(vault[selectedIndex].password.c_str(), 2, 24, 124, now);
}

static void drawAdd(uint32_t now) {
  oled.setFont(u8g2_font_6x12_tf);
  const char* m = "Add via Serial";
  oled.drawStr((128 - oled.getStrWidth(m)) / 2, 13, m);
  oled.setFont(u8g2_font_5x8_tf);
  const char* h = "see console  •  SELECT=cancel";
  oled.drawStr((128 - oled.getStrWidth(h)) / 2, 26, h);
}

static void drawToast(uint32_t now) {
  oled.setFont(u8g2_font_7x14B_tf);
  int tw = oled.getStrWidth(toastText.c_str());
  oled.drawStr((128 - tw) / 2, 21, toastText.c_str());
}

/* ----------------------------------------------------------------------------
 *  11. SERIAL COMPANION  —  how new entries get in
 *  Six buttons can't type a password, so RECORD hands off to the USB serial
 *  console. Send one line:  label \t username \t password
 *  ...or leave the password as "*" (or "gen:24") to have AEGIS roll a strong
 *  one for you. The entry is *staged*; press SAVE on the device to persist it.
 * -------------------------------------------------------------------------- */

bool addMode = false;
String addBuffer;

static void addModeBegin() {
  addMode = true; addBuffer = "";
  screen = SC_ADD; bumpActivity();
  Serial.println();
  Serial.println(F("== AEGIS: add entry =="));
  Serial.println(F("Format:  label<TAB>username<TAB>password"));
  Serial.println(F("Password can be '*' (auto) or 'gen:N' for length N."));
  Serial.print  (F("> "));
}

static void addModeCommit(const String& line) {
  int t1 = line.indexOf('\t');
  int t2 = line.indexOf('\t', t1 + 1);
  if (t1 < 0 || t2 < 0) {
    Serial.println(F("  ! need two tabs. try again:"));
    Serial.print(F("> "));
    return;
  }
  Entry e;
  e.label    = line.substring(0, t1);
  e.username = line.substring(t1 + 1, t2);
  String pw  = line.substring(t2 + 1);

  if (pw == "*")                 pw = generatePassword(GEN_PW_LENGTH);
  else if (pw.startsWith("gen:")) pw = generatePassword(constrain(pw.substring(4).toInt(), 4, 64));
  e.password = pw;

  vault.push_back(e);
  selectedIndex = vault.size() - 1;
  carousel = selectedIndex;
  vaultDirty = true;
  addMode = false;

  Serial.print(F("  staged: ")); Serial.println(e.label);
  Serial.println(F("  press SAVE on the device to write it to the card."));
  toast("Staged", 900, SC_LIST);
}

static void serialPump() {
  if (!addMode) return;
  while (Serial.available()) {
    char c = Serial.read();
    bumpActivity();
    if (c == '\r') continue;
    if (c == '\n') { String l = addBuffer; addBuffer = ""; addModeCommit(l); }
    else addBuffer += c;
  }
}

/* ----------------------------------------------------------------------------
 *  12. INPUT HANDLING per screen
 * -------------------------------------------------------------------------- */

static void submitPin() {
  if (!SD.exists(VAULT_PATH)) {
    // first run: capture PIN twice, then create an empty vault
    if (pinPass == 0) {
      for (int i = 0; i < PIN_LENGTH; i++) pinFirst[i] = pinDigits[i];
      pinPass = 1; pinCursor = 0;
      return;
    }
    bool match = true;
    for (int i = 0; i < PIN_LENGTH; i++) if (pinFirst[i] != pinDigits[i]) match = false;
    if (!match) { shake = 6; pinPass = 0; pinCursor = 0; toast("Mismatch", 800, SC_SETPIN); return; }
    for (int i = 0; i < PIN_LENGTH; i++) masterPin[i] = pinDigits[i];
    vault.clear();
    saveVault();
    unlocked = true; screen = SC_LIST; selectedIndex = 0; carousel = 0;
    toast("Vault created", 900, SC_LIST);
    return;
  }

  // returning user: try to decrypt
  for (int i = 0; i < PIN_LENGTH; i++) masterPin[i] = pinDigits[i];
  if (loadVault()) {
    unlocked = true; screen = SC_LIST; selectedIndex = 0; carousel = 0;
  } else {
    shake = 6; pinCursor = 0;                 // wrong PIN: wiggle + reset
    memset(masterPin, 0, PIN_LENGTH);
    toast("Wrong PIN", 800, SC_LOCK);
  }
}

static void handlePinInput() {
  if (took(B_LEFT))  { pinDigits[pinCursor] = (pinDigits[pinCursor] + 9) % 10; bumpActivity(); }
  if (took(B_RIGHT)) { pinDigits[pinCursor] = (pinDigits[pinCursor] + 1) % 10; bumpActivity(); }
  if (took(B_RECORD)) {                        // backspace
    if (pinCursor > 0) pinCursor--;
    pinDigits[pinCursor] = 0; bumpActivity();
  }
  if (took(B_SELECT)) {
    bumpActivity();
    if (pinCursor < PIN_LENGTH - 1) { pinCursor++; }
    else { submitPin(); }
  }
}

static void handleList() {
  if (took(B_LEFT))  { selectedIndex = (selectedIndex + vault.size() - 1) % max(1,(int)vault.size()); bumpActivity(); }
  if (took(B_RIGHT)) { selectedIndex = (selectedIndex + 1) % max(1,(int)vault.size()); bumpActivity(); }
  if (took(B_RECORD)) { addModeBegin(); }
  if (took(B_SAVE)) {
    bumpActivity();
    if (saveVault()) toast("Saved", 900, SC_LIST);
    else             toast("SD error", 1200, SC_LIST);
  }
  if (!vault.empty() && took(B_PLAY)) {         // quick-type the password
    bumpActivity();
    typeString(vault[selectedIndex].password);
    toast("Typed", 700, SC_LIST);
  }
  if (!vault.empty() && took(B_SELECT)) {       // open the actions menu
    bumpActivity(); actionIndex = 0; pillY = 0; screen = SC_ACTIONS;
  }
}

static void doAction() {
  Entry& e = vault[selectedIndex];
  switch (actionIndex) {
    case 0: typeString(e.password); toast("Typed", 700, SC_LIST); screen = SC_LIST; break;
    case 1:                                     // username, Tab, password
      typeString(e.username); Keyboard.write('\t'); delay(30); typeString(e.password);
      toast("Typed", 700, SC_LIST); screen = SC_LIST; break;
    case 2: screen = SC_REVEAL; break;
    case 3:                                     // delete
      vault.erase(vault.begin() + selectedIndex);
      if (selectedIndex >= (int)vault.size()) selectedIndex = max(0, (int)vault.size() - 1);
      carousel = selectedIndex; vaultDirty = true;
      toast("Deleted - SAVE", 1000, SC_LIST); screen = SC_LIST; break;
    default: screen = SC_LIST; break;           // Back
  }
}

static void handleActions() {
  if (took(B_LEFT))  { actionIndex = (actionIndex + N_ACTIONS - 1) % N_ACTIONS; bumpActivity(); }
  if (took(B_RIGHT)) { actionIndex = (actionIndex + 1) % N_ACTIONS; bumpActivity(); }
  if (took(B_SELECT) || took(B_PLAY)) { bumpActivity(); doAction(); }
  if (took(B_RECORD)) { screen = SC_LIST; bumpActivity(); }   // back
}

static void handleReveal() {
  if (took(B_SELECT) || took(B_RECORD) || took(B_LEFT) || took(B_RIGHT)) {
    bumpActivity(); screen = SC_ACTIONS;
  }
  if (took(B_PLAY)) { typeString(vault[selectedIndex].password); toast("Typed", 700, SC_LIST); }
}

static void handleAdd() {
  if (took(B_SELECT)) { addMode = false; screen = SC_LIST; bumpActivity(); }  // cancel
}

/* ----------------------------------------------------------------------------
 *  13. SETUP / LOOP
 * -------------------------------------------------------------------------- */

static void lockDevice() {
  // wipe secrets from RAM and drop back to the lock screen
  for (auto& e : vault) { e.password = ""; e.username = ""; }
  vault.clear();
  memset(masterPin, 0, PIN_LENGTH);
  unlocked = false; addMode = false;
  pinCursor = 0; pinPass = 0;
  for (int i = 0; i < PIN_LENGTH; i++) pinDigits[i] = 0;
  screen = SD.exists(VAULT_PATH) ? SC_LOCK : SC_SETPIN;
}

void setup() {
  Serial.begin(115200);
  buttonsBegin();

  Wire.begin(PIN_SDA, PIN_SCL);
  oled.begin();
  oled.setBusClock(400000);

  Keyboard.begin();
  USB.begin();

  sdSPI.begin(PIN_SD_SCK, PIN_SD_MISO, PIN_SD_MOSI, PIN_SD_CS);
  bool sdOk = SD.begin(PIN_SD_CS, sdSPI);

  // opening splash
  oled.clearBuffer();
  oled.setFont(u8g2_font_7x14B_tf);
  const char* t = "DSH-AEGIS";
  oled.drawStr((128 - oled.getStrWidth(t)) / 2, 20, t);
  oled.sendBuffer();
  delay(700);

  if (!sdOk) {
    // no card = nothing to do; sit on an error frame
    for (;;) {
      oled.clearBuffer();
      oled.setFont(u8g2_font_6x12_tf);
      oled.drawStr(6, 14, "No SD card");
      oled.setFont(u8g2_font_5x8_tf);
      oled.drawStr(6, 27, "insert & reset");
      oled.sendBuffer();
      delay(400);
    }
  }

  screen = SD.exists(VAULT_PATH) ? SC_LOCK : SC_SETPIN;
  for (int i = 0; i < PIN_LENGTH; i++) pinDigits[i] = 0;
  bumpActivity();
}

void loop() {
  uint32_t now = millis();
  buttonsUpdate();
  serialPump();

  // auto-lock on idle (only once we're actually in)
  if (unlocked && screen != SC_ADD && now - lastActivity > IDLE_LOCK_MS) {
    lockDevice();
  }

  // route input to the active screen
  switch (screen) {
    case SC_SETPIN:
    case SC_LOCK:    handlePinInput(); break;
    case SC_LIST:    handleList();     break;
    case SC_ACTIONS: handleActions();  break;
    case SC_REVEAL:  handleReveal();   break;
    case SC_ADD:     handleAdd();      break;
    case SC_TOAST:   if (now > toastUntil) screen = toastReturn; break;
  }

  // advance animations (this is the whole "liquid" trick — nothing fancy)
  carousel = ease(carousel, selectedIndex, 0.25f);
  if (shake > 0.05f) shake = -shake * 0.7f;   // ring down toward zero
  else shake = 0;

  // render
  oled.clearBuffer();
  switch (screen) {
    case SC_SETPIN:  drawPinScreen(pinPass == 0 ? "Set PIN" : "Confirm PIN", now); break;
    case SC_LOCK:    drawPinScreen("Enter PIN", now); break;
    case SC_LIST:    drawList(now);    break;
    case SC_ACTIONS: drawActions(now); break;
    case SC_REVEAL:  drawReveal(now);  break;
    case SC_ADD:     drawAdd(now);     break;
    case SC_TOAST:   drawToast(now);   break;
  }
  // unsaved-changes dot, top-right
  if (unlocked && vaultDirty && screen == SC_LIST) oled.drawDisc(124, 3, 1);
  oled.sendBuffer();

  delay(8);   // ~120 fps cap; keeps the eases smooth without pegging the CPU
}
