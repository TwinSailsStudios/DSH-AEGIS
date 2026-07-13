# DSH-AEGIS

A password manager you can hold. It's a little ESP32 board with a screen and six
buttons, and it plugs into your computer over USB-C. When you press PLAY, it
types your password for you, because to the computer it just looks like a normal
keyboard.

Your passwords live encrypted on a microSD card. They never get sent anywhere.
The only thing that ever leaves the device is keystrokes.

I built this because I kept reusing the same password everywhere, which is bad,
and password manager apps felt like a lot of trust to put in a company. This way
the passwords are on a card in my pocket.

## What it does

* Locks behind a 4-digit PIN
* Stores your logins encrypted with AES-256
* Types the password straight into the login box (USB HID keyboard)
* Can also type username, then Tab, then password, so it fills the whole form
* Generates strong random passwords for you
* Shows the password on screen if you actually need to read it
* Auto-locks after 60 seconds if you walk away
* Has a UI I spent way too long on (smooth sliding, page dots, little pop-up
  messages, the whole iOS thing)

## Hardware

| Part | What it is |
|---|---|
| ESP32-S3-WROOM-1 | the brain, and it does USB natively |
| SSD1306 OLED 128x32 | the screen (I2C) |
| 6x tactile push buttons | the controls |
| microSD card module | where the vault lives (SPI) |
| USB-C receptacle | power + keyboard |
| AMS1117-3.3 | drops 5V to 3.3V |

**Important:** this board uses IO35, IO36 and IO37 for the SD card. Those pins
are only free if your ESP32-S3 module has no PSRAM or quad PSRAM. If you get an
**R8** module (octal PSRAM), those pins belong to the PSRAM chip and the SD card
will not work. Check the part number before you order.

## Pinout

| Pin | Goes to |
|---|---|
| IO0 | RECORD button |
| IO1 | SAVE button |
| IO2 | PLAY button |
| IO3 | SCROLL LEFT button |
| IO4 | SELECT button |
| IO5 | SCROLL RIGHT button |
| IO6 | OLED SCL |
| IO7 | OLED SDA |
| IO35 | SD CS |
| IO36 | SD MOSI |
| IO37 | SD SCK |
| IO38 | SD MISO |

## Setting it up

1. Install the **U8g2** library in the Arduino IDE. Everything else already comes
   with the ESP32 board package.
2. Board settings:
   * Board: **ESP32S3 Dev Module**
   * USB CDC On Boot: **Enabled**
   * USB Mode: **USB-OTG (TinyUSB)**
   * PSRAM: **Disabled**

   That last one matters. If you leave PSRAM on OPI, the chip takes IO35-37 for
   itself and the SD card just quietly won't work, which took me a while to
   figure out.
3. Put a FAT32 formatted microSD card in.
4. Flash `dsh_aegis.ino`.

## Using it

**First time.** It'll ask you to make a PIN. Use SCROLL LEFT and SCROLL RIGHT to
change the digit, SELECT to move to the next one. It asks twice so you don't lock
yourself out with a typo. Then it makes an empty vault on the card.

**After that.** Type your PIN in the same way. If it's wrong the screen shakes at
you and you start over.

**Adding a password.** There's no keyboard on the device, so pressing RECORD hands
it off to the serial monitor. Open it at 115200 and send one line like this, with
actual Tab characters between the parts:

```
GitHub    pratik@example.com    hunter2
```

If you'd rather have the device make up a password for you, put `*` instead of
the password and it'll generate a 20 character one. `gen:32` gives you 32
characters.

That entry is only *staged* at this point. Press **SAVE** on the device to
actually encrypt it and write it to the card. There's a little dot in the corner
of the screen reminding you when you have unsaved stuff.

**Getting a password out.** Scroll to the one you want with the left and right
buttons, click into whatever login box you're filling out, and press **PLAY**. It
types it.

If you want the other options, press **SELECT** instead and you get a menu: type
the password, type username + Tab + password, show the password on screen, delete
it, or go back.

## How the encryption works

I'm not a cryptographer so I stuck to the boring standard stuff, which is what
you're supposed to do anyway.

Your PIN goes through PBKDF2-HMAC-SHA256 with 60,000 rounds and a random salt.
That turns 4 digits into a real key, and the 60,000 rounds make guessing slow.
The vault gets encrypted with AES-256-CBC. There's also an HMAC over the
ciphertext, so if you enter the wrong PIN, or if somebody messed with the file,
it fails the check and the device never even tries to decrypt anything.

The salt and the IV are new every single time you save.

## Stuff I know isn't perfect

* A 4-digit PIN is only 10,000 combinations. The PBKDF2 rounds slow an attacker
  down a lot, but if somebody steals the SD card and is patient, that's not
  amazing. Making the PIN longer is on my list.
* There's no limit on how many times you can guess the PIN on the device itself.
* You have to use a computer to add a password, which is a little ironic for a
  device whose whole job is not needing the computer. I want to add on-device
  entry using the buttons.
* No way to change your PIN yet without wiping everything.

## Ideas for later

* Longer / alphanumeric PIN
* Adding entries on the device instead of over serial
* Change PIN (re-encrypt the vault in place)
* Wrong-PIN lockout with a delay that gets longer
## License

MIT. Do whatever you want with it.
