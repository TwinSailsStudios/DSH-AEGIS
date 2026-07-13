---
title: "DSH-AEGIS"
author: "Pratik Dash (TwinSailsStudios)"
description: "A physical password manager. ESP32-S3, OLED, six buttons, SD card. Plugs in over USB-C and types your passwords for you."
created_at: "2026-06-24"
---

Quick note before anything: this is a design log, not a build log. I did the schematic, the PCB, the case, and the firmware, but I have not ordered the board. PCBs cost money and I'm paying for my own hobbies. So none of this has been flashed or tested on real hardware, it's all verified in KiCad, but it might not work if you do try to build it. HOLY IM SO DONE WITH MY LAPTOP, IT TURNED OFF WHILE I WAS ADDING MY LAPSE LINKS AND I LOST MY PROGROESS. IT DIDNT EVEN RUN OUTTA POWER.

# Day 1 - Starting the schematic - June 24th - 5 Hours

I've decided to build a physical password manager. Little handheld thing, screen, six buttons, SD card in the back, plugs into your computer over USB-C and types your passwords for you. I got tired of using the same password on like nine different sites which is objectively terrible.

I've done PCB stuff before but nothing with a chip this big, so this is a step up for me.

First thing I had to decide was the MCU, and I went with the ESP32-S3. The big reason is native USB. That means the board doesn't pretend to be a keyboard, your computer genuinely thinks a keyboard got plugged in. No drivers, no app, works on any machine you walk up to. If I used a normal ESP32 I'd need a whole separate USB chip and it gets uglier and more expensive.

Opened KiCad, dropped the S3 symbol in, and immediately felt out of my depth. This chip has an absurd number of pins. Spent most of today just reading the symbol figuring out which ones I'm even allowed to touch.

![](assets/Screenshot_from_2026-06-23_01-42-36.png)

Then I did the six buttons. RECORD, SAVE, PLAY, SCROLL LEFT, SELECT, SCROLL RIGHT. Each one is just a GPIO on one leg and ground on the other, that's it. No pull-up resistors needed because the ESP32 has them built in and I turn them on in software. Six fewer parts to buy.

![](assets/Screenshot_from_2026-06-23_01-47-30.png)

The wires got messy but KiCad only cares that the connections are right, not that it looks nice.

Lapse: [yo MURDER my laptop]

**Total time spent: 5h**

# Day 2 - Rest of the schematic + footprints - June 25th - 6 Hours

Added everything else today:

* the OLED, which is I2C so only two wires, SCL and SDA
* the SD card socket, SPI, four wires
* USB-C receptacle
* AMS1117-3.3 to drop 5V down to 3.3V
* two 5.1K resistors on the CC pins

Those resistors are important and I almost didn't put them in. Without them the USB-C port has no way to tell your laptop "hey I'm a device, give me power" and the board just doesn't turn on. I only knew about this because I read like four forum threads of people asking why their USB-C board was dead and the answer was always the CC resistors.

![](assets/Screenshot_from_2026-07-13_14-05-19.png)

Then footprints, which nobody warns you about. A schematic symbol is basically a cartoon, it doesn't know how big the real part actually is. So you have to go through every single component and tell KiCad which exact physical shape it is in real life.

15 components, one at a time, out of a list of 7,447 footprints.

![](assets/Screenshot_from_2026-06-24_03-50-50.png)

I have now seen every connector ever manufactured.

Lapse: ["yeah add a lapse section, it'll be such a good idea!"]

**Total time spent: 6h**

# Day 3 - PCB layout, the long day - June 26th - 8 Hours

Biggest day by a mile. Today the schematic became an actual board.

When you first push everything into the PCB editor it just dumps all the parts in a pile with a horrifying spiderweb of white lines showing what needs to connect to what. It looks impossible. It's not, it just takes eight hours.

The layout I went with is six buttons in a 2x3 grid at the bottom like a keypad, OLED sitting right above them, ESP32 up top, USB-C on the left edge so the cable comes out the side instead of jabbing into your hand.

![](assets/Screenshot_from_2026-06-24_02-57-05.png)

Learned the annoying way that the ESP32 module has an antenna on it and you can't run copper underneath an antenna or the wifi gets worse. So there's this big hatched keep-out zone across the top of my board that I'm not allowed to route through. Had to redo a few traces once I realized.

![](assets/Screenshot_from_2026-06-26_00-54-56.png)

Ended at 217 track segments, 21 vias, 43 nets, 0 unrouted. That last number is the one that actually matters and I looked at it for a while.

Did not move from this chair all day.

Lapse: [waaaaaaaaaaaaaaaaah]

**Total time spent: 8h**

# Day 4 - Silkscreen, and catching a pin conflict that would've cost me a board - June 27th - 4 Hours

Easy day mostly. Silkscreen is the white printing on a PCB and it costs literally nothing extra, the fab prints it either way, so obviously I put a heart on the back, my name, the project name, and some Kanye lyrics down the side.

![](assets/Screenshot_from_2026-06-25_00-19-52.png)

Then while I was double checking my pin assignments against the datasheet before calling the board done, I found something that would've ruined the entire PCB order.

My SD card is on IO35, IO36, IO37 and IO38. On an ESP32-S3 module with octal (R8) PSRAM, IO35 through 37 are wired internally to the PSRAM chip. You physically cannot use them as GPIO. The KiCad symbol even brackets those three pins and literally labels them PSRAM, and I had been staring at that label for three days without it registering.

So this board only works with a module that has no PSRAM or quad PSRAM. And in the firmware, PSRAM has to be set to Disabled, because if the ESP32 thinks it has OPI PSRAM it grabs those pins at boot before my code even runs, and the SD card would just silently never mount.

I would have had zero clue why. I'd have gotten the board back, soldered the whole thing, and spent a weekend blaming my solder joints. Very glad I caught this on a screen and not on a desk. Put a big comment about it at the top of the firmware.

Then opened the 3D viewer to see what it'd look like when it's real, and honestly it looks like a product.

![](assets/Screenshot_from_2026-06-25_03-43-38.png)

Lapse: [TIMWANMNIHAC]

**Total time spent: 4h**

# Day 5 - Case in Tinkercad - June 28th - 5 Hours

A bare PCB in your pocket is a short circuit waiting to happen so it needs a case.

I used Tinkercad. I know. But I need a box with holes in it and Tinkercad makes a box with holes in it in fifteen minutes, and Fusion 360 makes me want to lie down. I'll learn Fusion eventually.

![](assets/Screenshot_from_2026-06-26_02-14-46.png)

Version 1 is a bottom tray the PCB drops into, a lid with a window cut out for the OLED, six button caps, a front piece with the heart on it, and cutouts for the USB-C port on the side and the SD card slot.

Then I laid everything out flat to check it'd actually print, and mostly to check the button caps weren't so tiny they'd fly off the print bed and disappear into the carpet forever.

![](assets/Screenshot_from_2026-06-26_03-57-41.png)

Not printing it yet though. Every dimension in here is a guess off the KiCad model and I'd rather measure the real board with calipers than waste filament. So it stays a model.

Lapse: [payed the toll on a broken road]

**Total time spent: 5h**

# Day 6 - Firmware - June 30th - 6 Hours

Took a day off and then wrote the code. Used VS Code and the Arduino IDE, with the U8g2 library for the screen and mbedTLS for the crypto (both already come with the ESP32 core).

The flow is PIN screen, then unlock, then scroll through your saved logins, then hit PLAY and it types the password wherever your cursor is.

For the encryption I stuck to boring standard stuff on purpose. PBKDF2 stretches the 4 digit PIN into a real key, 60,000 rounds so guessing is slow. AES-256 for the vault itself. And an HMAC over the ciphertext, which means if you type the wrong PIN it fails a check and the device never even tries to decrypt anything. None of that is my invention. Making up your own crypto is how you end up as somebody's cautionary example.

![](assets/Screenshot_from_2026-07-13_14-57-41.png)

The UI is the part I spent the most time on. Everything that moves, the carousel sliding between entries, the pill in the menus, the shake when you get the PIN wrong, is all one tiny function. It's just a number easing toward a target a little bit each frame. That's the entire animation system. Costs basically nothing and it makes a 128x32 monochrome screen feel like it actually has a UI instead of a menu.

![](assets/Screenshot_from_2026-07-13_14-58-03.png)

The weak point is adding entries. Six buttons can't type a password, so RECORD hands off to the serial monitor and you send label, username, password separated by tabs. A password manager that needs a computer to set it up is a little embarrassing and it's the first thing I want to fix.

None of this has run on real hardware. It compiles and I've read through it a bunch of times but until I can afford the PCB it's all theoretical. I fully expect to find stuff wrong the second it actually boots.

Lapse: [i hate ts laptop dawg]

**Total time spent: 6h**

# Where it's at

Schematic done, board routed with 0 unrouted nets, case modelled, firmware written. Not built, because I'm funding this myself and the board plus components is real money I don't have right now. When I do have it, this becomes a build log.

Still want to do:

* add entries on the device instead of over serial
* PIN longer than 4 digits
* change your PIN without wiping the whole vault
* lockout after too many wrong guesses
* actually order the thing

**Grand total: 34h**
