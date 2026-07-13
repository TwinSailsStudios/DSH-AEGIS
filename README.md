# DSH-AEGIS

<img width="1920" height="1035" alt="HOVA" src="https://github.com/user-attachments/assets/75a87415-c3d9-4c18-bcd2-3031036c1f7f" />
https://github.com/user-attachments/assets/2e60c4a9-2eaf-4800-a794-9d0f289f58ca

A physical password manager. ESP32-S3, a tiny OLED, six buttons, and an SD card.
Plugs into your computer over USB-C and *types* your passwords for you.

> **Status: designed, not built.** The schematic and PCB are done, the firmware
> is written. I haven't ordered the board yet because I'm paying for this stuff
> myself and PCBs cost money. Everything here is verified in KiCad, not on a
> bench. I'll update this when I can afford to fab it.

## why

I was using the same password on like nine different sites. Which is bad. Every
password manager I looked at wanted me to trust a company with all of it, and I
didn't love that either. So: keep them on an SD card, in my pocket, encrypted.

## what it does

- Locks behind a PIN you enter on the buttons
- Vault is encrypted on the SD card with AES-256
- Types the password straight into the login box over USB
- Can do username → Tab → password to fill a whole form
- Generates strong random passwords
- Auto-locks after 60s if you walk away

The typing thing is the whole point. The ESP32-S3 has **native USB**, so the board
doesn't *pretend* to be a keyboard — your computer genuinely thinks a keyboard got
plugged in. No drivers, no app. Works on any machine you walk up to.

## hardware

| part | what it does |
|---|---|
| ESP32-S3-WROOM-1 | the brain. picked for native USB |
| SSD1306 128x32 OLED | the screen (I2C) |
| 6x tactile switches | the controls |
| microSD module | where the vault lives (SPI) |
| USB-C receptacle | power + keyboard |
| AMS1117-3.3 | 5V → 3.3V |
| 2x 5.1K resistors | CC pins, so USB-C actually gives you power |

## pinout

| pin | goes to |
|---|---|
| IO0–IO5 | RECORD, SAVE, PLAY, SCROLL-L, SELECT, SCROLL-R |
| IO6 / IO7 | OLED SCL / SDA |
| IO35–IO38 | SD CS / MOSI / SCK / MISO |

**Heads up on IO35–37.** On an ESP32-S3 module with **octal (R8) PSRAM**, those
three pins are wired to the PSRAM die internally and you can't use them as GPIO.
This board only works with a **no-PSRAM or quad-PSRAM** module. If you build it,
check the part number, and set **PSRAM: Disabled** in the Arduino IDE — leave it
on OPI and the chip claims those pins at boot, before your code runs, and the SD
card silently never mounts.

## how you'd use it

Enter your PIN. Scroll to the login you want with the arrow buttons. Click into
whatever password box you're filling out on your computer. Hit **PLAY**. Done.

**SELECT** opens a menu with more options: type just the password, type
username+Tab+password, show the password on screen, or delete it.

Adding a new entry is the weak spot right now — you press **RECORD** and it hands
off to the serial monitor, where you send `label⇥username⇥password`. A password
manager that needs a computer to set up is a bit embarrassing and I want to fix
it. (Put `*` as the password and it generates one for you.)

Then hit **SAVE** to encrypt it and write it to the card.

## the crypto

Boring on purpose. Your PIN goes through PBKDF2-HMAC-SHA256 (60,000 rounds, random
salt) to turn 4 digits into a real key. Vault is AES-256-CBC. There's an HMAC over
the ciphertext, so a wrong PIN fails the check and the device never even attempts
to decrypt. Salt and IV are fresh every save.

I did not invent any of this. Inventing your own crypto is how you become a
cautionary example in someone's blog post.

## build it yourself

Firmware is in `firmware/`. You need the **U8g2** library; everything else ships
with the ESP32 Arduino core.

Board settings:
- Board: ESP32S3 Dev Module
- USB CDC On Boot: **Enabled**
- USB Mode: **USB-OTG (TinyUSB)**
- PSRAM: **Disabled** (see the IO35 warning above)

KiCad project is in `hardware/`.

## what's not done

- **Never physically built.** Simulated and verified in KiCad only.
- A 4-digit PIN is 10,000 combinations. The PBKDF2 rounds slow an attacker down a
  lot but it's not amazing if someone steals the card and is patient.
- No lockout after repeated wrong guesses.
- Can't add entries on the device, needs serial.
- No way to change your PIN without wiping everything.
- Case is modelled in Tinkercad but not printed.

## AI use
I used gemini to help with some kicad features and trying to debug a probelm with exporting my 3d model of my pcb
I used Claude to help write the firmware and to talk through design decisions
(pin conflicts, how to structure the vault encryption). The schematic, PCB layout,
3D model, and this README are mine. the firmware, im not gonna act like i wrote it from scratch, it was more like using claude to frankenstien my code togetehr into a working firmware, since i haventbuilt ts irl yet, if any of yall try PLEASE lmk if theres any code problems, thx:

## license

MIT. do whatever.
