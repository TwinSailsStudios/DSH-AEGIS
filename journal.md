---
title: "DSH-AEGIS"
author: "Pratik Dash (TwinSailsStudios)"
description: "A physical password manager — ESP32-S3, OLED, six buttons, SD card. Plugs in over USB-C and types your passwords for you."
created_at: "2026-06-24"
---

> Heads up: this is a **design log**, not a build log. Over these 6 days I did the
> schematic, the PCB, the case model, and the firmware. I haven't ordered the
> board yet — PCBs cost money and I'm funding this myself. So nothing here has
> been flashed or tested on real hardware. It's all verified in KiCad, not on a
> bench.

---

# June 24th: Starting the schematic, meeting a chip with too many pins

Day one. The idea: a handheld thing with a screen and six buttons that plugs into
your computer and types your passwords for you, so I can stop using the same
password on nine different websites.

Picked the **ESP32-S3** specifically because it has native USB. That's the whole
trick — the board doesn't *pretend* to be a keyboard, your computer genuinely
thinks a keyboard got plugged in. No drivers, no companion app, works on any
machine you walk up to. If I used a normal ESP32 I'd need a separate USB chip and
the whole thing gets uglier and more expensive.

Opened **KiCad**, dropped the S3 symbol in, and immediately felt out of my depth.
This chip has an absurd number of pins. Spent the first chunk of the day just
reading through the symbol working out which ones I'm actually allowed to touch.

![the ESP32-S3 symbol](assets/Screenshot_from_2026-06-23_01-42-36.png)

Then wired the six buttons: RECORD, SAVE, PLAY, SCROLL LEFT, SELECT, SCROLL
RIGHT. Each one is a GPIO on one leg, ground on the other. No pull-up resistors,
because the ESP32 has them built in and I can switch them on in software. Six
fewer parts to buy.

![the six buttons wired up](assets/Screenshot_from_2026-06-23_01-47-30.png)

Wires got messy but KiCad only cares that the connections are right.

**Software used:** KiCad 8 (schematic editor)
**Lapse:** [LAPSE LINK]

**Total time spent: 5h**

---

# June 25th: Finishing the schematic, then 7,447 footprints

Added everything else: the OLED (I2C, so just SCL and SDA), the SD card socket
(SPI, four wires), the USB-C receptacle, an AMS1117-3.3 to drop 5V to 3.3V, and
two 5.1K resistors on the CC pins.

Those resistors matter. Without them the USB-C port has no way to signal "I'm a
device, give me power," and the board just doesn't turn on. I only knew that
because I read a pile of forum threads from people asking why their USB-C board
was dead, and the answer was always the CC resistors.

![the finished schematic](assets/Screenshot_from_2026-07-13_14-05-19.png)

Then footprints, which is the part nobody warns you about. A schematic symbol is
basically a cartoon — it has no idea how big the real component is. So you go
through every part and tell KiCad which exact physical shape it corresponds to.

15 components, one at a time, from a list of **7,447**.

![the footprint assignment dialog](assets/Screenshot_from_2026-06-24_03-50-50.png)

I have now seen every connector ever manufactured by mankind.

**Software used:** KiCad (schematic editor, footprint assignment)
**Lapse:** [LAPSE LINK]

**Total time spent: 6h**

---

# June 26th: PCB layout. The long one.

Biggest day. Turned the schematic into an actual board.

When you first import everything into the PCB editor it dumps all the parts in a
pile with a spiderweb of thin white lines showing what needs to connect to what.
It looks impossible. It isn't, it just takes hours.

Layout: six buttons in a 2x3 grid at the bottom like a keypad, OLED right above
them, ESP32 at the top, USB-C on the left edge so the cable comes out the side
instead of jabbing into your palm.

![routing near the regulator](assets/Screenshot_from_2026-06-24_02-57-05.png)

Learned the annoying way that the ESP32 module has an **antenna** on it, and you
can't run copper underneath an antenna or the wifi degrades. So there's a big
hatched keep-out zone across the top of the board I'm not allowed to route
through. Had to redo a few traces after I figured that out.

![routed board, 0 unrouted](assets/Screenshot_from_2026-06-26_00-54-56.png)

Final: **217 track segments, 21 vias, 43 nets, 0 unrouted.** That last number is
the one that matters.

Did not move from the chair for eight hours.

**Software used:** KiCad (PCB editor)
**Lapse:** [LAPSE LINK]

**Total time spent: 8h**

---

# June 27th: Silkscreen, and catching a pin conflict before it cost me a board

Lighter day. Started with silkscreen — the white printing on a PCB. It costs
nothing extra, the fab prints it either way, so I put a heart on the back, my
name, the project name, and some Kanye lyrics down the side.

![silkscreen on the back](assets/Screenshot_from_2026-06-25_00-19-52.png)

Then, while double-checking pin assignments against the datasheet before calling
the board finished, I found something that would have wasted an entire PCB order.

My SD card is on **IO35, IO36, IO37, IO38**. On an ESP32-S3 module with **octal
(R8) PSRAM**, IO35–37 are wired internally to the PSRAM die. They physically
cannot be used as GPIO. The KiCad symbol literally brackets those three pins and
labels them "PSRAM" — I'd been staring at that label for three days without
registering what it meant.

So this board only works with a module that has **no PSRAM, or quad PSRAM**. And
in the firmware, PSRAM has to be set to **Disabled**, because if the ESP32 thinks
it has OPI PSRAM it claims those pins at boot, before any of my code runs, and
the SD card would just silently never mount.

I would have had zero idea why. I'd have gotten the board back, soldered it, and
spent a weekend blaming my solder joints. Very glad I caught this on a screen and
not on a desk. Wrote a big comment about it at the top of the firmware so future
me doesn't undo it.

Then opened the 3D viewer to see what it'll actually look like:

![3D render of the board](assets/Screenshot_from_2026-06-25_03-43-38.png)

**Software used:** KiCad (PCB editor, 3D viewer)
**Lapse:** [LAPSE LINK]

**Total time spent: 4h**

---

# June 28th: A case, in Tinkercad

A bare PCB in your pocket is a short circuit waiting to happen, so it needs a
case.

I used **Tinkercad**. I know. But I need a box with holes in it, and Tinkercad
makes a box with holes in it in fifteen minutes, whereas Fusion 360 makes me want
to lie down. I'll learn Fusion eventually.

![case ver 1](assets/Screenshot_from_2026-06-26_02-14-46.png)

Ver 1: bottom tray the PCB drops into, lid with a window cut out for the OLED,
six button caps, a front piece with the heart on it, and cutouts for USB-C on the
side and the SD slot.

Laid it out flat to check it'd actually print, and mostly to check the button
caps weren't so small they'd fly off the bed and vanish into the carpet forever.

![laid out for printing](assets/Screenshot_from_2026-06-26_03-57-41.png)

Not printing it yet. Every dimension is a guess off the KiCad model, and I'd
rather measure the real board with calipers than waste filament. So it stays a
model for now.

**Software used:** Tinkercad
**Lapse:** [LAPSE LINK]

**Total time spent: 5h**

---

# June 30th: The firmware

Took a day off, then wrote the code.

Structure: PIN screen → unlock → scroll through your saved logins → hit PLAY → it
types the password wherever your cursor is.

For the crypto I stuck to boring, standard, well-tested stuff on purpose.
**PBKDF2** stretches the 4-digit PIN into a real key (60,000 rounds, so guessing
is slow). **AES-256** encrypts the vault. An **HMAC** over the ciphertext means a
wrong PIN fails a check and the device never even attempts to decrypt anything.
None of that is my invention. Rolling your own crypto is how you end up as
somebody else's cautionary example.

![the PIN unlock logic](assets/Screenshot_from_2026-07-13_14-57-41.png)

The UI is what I spent the most time on. Everything that moves — the carousel
sliding between entries, the selection pill in the menus, the shake when you type
the wrong PIN — is one function: a number easing toward a target a fraction each
frame. That's the entire animation system. Costs almost nothing and it makes a
128x32 monochrome screen feel like it has a UI instead of a menu.

![list and actions handling](assets/Screenshot_from_2026-07-13_14-58-03.png)

The weak spot is adding entries. Six buttons can't type a password, so RECORD
hands off to the serial monitor and you send `label / user / pass` separated by
tabs. A password manager that needs a computer to set up is a bit embarrassing
and it's the first thing I want to fix.

**None of this has run on real hardware.** It compiles, and I've read it through
enough times to be reasonably confident in it, but until I can afford the PCB
it's theoretical. I fully expect to find things wrong the second it boots.

**Software used:** VS Code, Arduino IDE (ESP32 core), U8g2 library, mbedTLS
**Lapse:** [LAPSE LINK]

**Total time spent: 6h**

---

# Where it's at

Schematic done. Board routed, 0 unrouted nets. Case modelled. Firmware written.

Not built. That's the honest status — I'm paying for this myself and the board
plus components is real money I don't have right now. When I do, this turns into
a build log.

Still on the list:

* Add entries **on the device** instead of over serial
* PIN longer than 4 digits
* Change your PIN without wiping the vault
* Lockout after repeated wrong guesses
* Actually order the thing

**Grand total: 34h**
