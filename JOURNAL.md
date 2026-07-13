# DSH-AEGIS — Build Journal

**Total time: 34 hours over 6 days (June 24 – June 30)**

This is basically me writing down what I did each day so future me remembers why
anything is the way it is. Spoiler: some of it is because of a bug I caused
myself.

---

## Day 1 — June 24 · 5 hours

**Schematic, part one**

Started the whole thing today. The plan in my head: a little board with a screen
and some buttons that plugs into your computer and types your passwords for you,
so I can stop using the same password on nine different websites like a maniac.

Dropped the ESP32-S3 symbol into KiCad and immediately felt bad about my life
choices. This chip has *so many pins*. Like an unreasonable number of pins. I sat
there for a solid twenty minutes just reading pin names trying to figure out
which ones I'm even allowed to touch.

![ESP32-S3 symbol in the schematic](Screenshot_from_2026-06-23_01-42-36.png)

Picked the S3 specifically because it does USB natively. That's the whole trick
of this project — the board shows up to your computer as a **keyboard**, not as
some weird device that needs drivers. So when it "types" your password it's not
faking anything, it genuinely is a keyboard. That only works if the chip has
native USB, and the S3 does.

Then I wired up the six buttons. RECORD, SAVE, PLAY, SCROLL LEFT, SELECT, SCROLL
RIGHT. Every button is just: pin on one side, ground on the other. No pull-up
resistors, because the ESP32 has pull-ups built in and I can turn them on in
software. Free real estate.

![Buttons wired to the ESP32](Screenshot_from_2026-06-23_01-47-30.png)

The green lines got a little spaghetti-ish by the end but they're all connected
to the right things, which is the only thing KiCad actually cares about.

**Time: 5 hours.** Mostly reading the datasheet and second-guessing myself.

---

## Day 2 — June 25 · 6 hours

**Finishing the schematic + footprints**

Added everything else today: the OLED (I2C, so just two wires, SCL and SDA, very
polite of it), the SD card socket over SPI, the USB-C connector, an AMS1117 to
turn 5V into 3.3V, and two 5.1K resistors on the CC pins so the USB-C port
actually knows it's supposed to give me power.

Those 5.1K resistors are apparently a thing everybody forgets. I did not forget
them because I read four different forum posts of people going "why does my
USB-C board not power on" and the answer was always the resistors. Learning from
other people's pain, that's efficiency.

![The finished schematic](Screenshot_from_2026-07-13_14-05-19.png)

Then footprints. This is the part nobody tells you about. A symbol is just a
drawing, it doesn't know how big the actual part is. So you have to go through
every single component and tell KiCad "this specific physical shape is what this
is." Fifteen components, one at a time, in a dialog box that lists **7,447**
possible footprints.

![The footprint assignment dialog](Screenshot_from_2026-06-24_03-50-50.png)

Seven thousand. I scrolled through so many connectors. My eyes hurt.

**Time: 6 hours.** Roughly 2 of schematic and 4 of footprint suffering.

---

## Day 3 — June 26 · 8 hours

**PCB layout. The long day.**

This was the big one. Today I actually turned the schematic into a board.

When you first import everything into the PCB editor it just dumps every part in
a pile in the corner with a horrifying spiderweb of thin white lines showing what
needs to connect to what. It looks completely impossible. It is not impossible,
it just takes hours.

Laid it out so the six buttons sit in a 2x3 grid at the bottom like a little
keypad, the screen goes above them, and the ESP32 sits at the top. USB-C on the
left edge so the cable comes out the side.

![Routing near the voltage regulator](Screenshot_from_2026-06-24_02-57-05.png)

Also: the ESP32 module has an **antenna** on it, and you're not allowed to put
copper under an antenna or it stops working properly. So there's a big pink
hatched KEEP-OUT ZONE at the top of my board that I'm not allowed to touch. It's
like a tiny no-fly zone. Respect it or your WiFi is bad forever.

![The routed board](Screenshot_from_2026-06-26_00-54-56.png)

Final count: **217 track segments, 21 vias, 43 nets, 0 unrouted.** That last
number is the one that matters and I stared at it for a while feeling good about
myself.

**Time: 8 hours.** I did not move from the chair. My cats came, sat on the desk,
judged me, and left.

---

## Day 4 — June 27 · 4 hours

**Silkscreen art (the fun part)**

Easy day. Everything electrical works, so today was just decorating.

Silkscreen is the white printing on a PCB. It costs nothing extra. So obviously
I put a heart on the back, my name, the project name, and some Kanye lyrics down
the side, because if I'm going to spend a week on a circuit board it's going to
have a *personality*.

![Silkscreen work on the back of the board](Screenshot_from_2026-06-25_00-19-52.png)

Then I opened the 3D viewer to see what it'd actually look like when it shows up
in the mail, and honestly? It looks like a real product. Like a thing you'd buy.
I might have made a small noise.

![3D render of the finished board](Screenshot_from_2026-06-25_03-43-38.png)

**Time: 4 hours.** Half of it was just spinning the 3D view around admiring it,
which I'm counting, because morale is a real resource.

---

## Day 5 — June 28 · 5 hours

**A case, in Tinkercad, because I don't know Fusion**

The board is great but a bare PCB in your pocket is asking for a short circuit,
so it needs a case.

I used Tinkercad. Yes, Tinkercad. I know. Real CAD people are making a face right
now. But here's the thing: I need a box with holes in it, and Tinkercad makes
boxes with holes in it in about fifteen minutes, whereas Fusion 360 makes me cry
in about fifteen minutes. I'll learn Fusion later. Probably. Maybe.

![Case version 1 in Tinkercad](Screenshot_from_2026-06-26_02-14-46.png)

Version 1 is a bottom tray, a lid with a window cut out for the screen, six
button caps, and a little kickstand-looking front piece with a heart on it. There
are cutouts for the USB-C port on the side and the SD card slot.

Then I laid everything out flat on the plate to see if it'd actually print, and
to check the button caps weren't going to be so tiny that they fly off the bed
and vanish into the carpet dimension forever.

![Everything laid out for printing](Screenshot_from_2026-06-26_03-57-41.png)

Nothing's printed yet, that's for when the boards arrive and I can measure things
for real instead of trusting my own numbers.

**Time: 5 hours.**

---

## Day 6 — June 30 · 6 hours

**Firmware, and two bugs that made me question everything**

Wrote the actual code today. The structure is: PIN screen → unlock → scroll
through your saved logins → press PLAY → it types the password.

Encryption is PBKDF2 to stretch your PIN into a real key, then AES-256 for the
vault, plus an HMAC so that if you type the wrong PIN it fails the check and the
device never even *tries* to decrypt anything. I stuck to boring standard
algorithms on purpose. Inventing your own crypto is how you end up on a list of
cautionary examples.

![The PIN unlock code](Screenshot_from_2026-07-13_14-57-41.png)

### Bug 1: the SD card that would not mount

Flashed it. Screen lights up. "No SD card." Cool.

Swapped the card. Reformatted the card. Different card. Checked every solder
joint twice. Re-checked my pin numbers against the schematic like six times. SD_CS
is IO35, SD_MOSI is IO36, SD_SCK is IO37, SD_MISO is IO38. All correct. Still
nothing. I was about ninety percent sure I'd broken the board.

Turns out the problem was a **dropdown menu in the Arduino IDE.**

If you tell the ESP32 you have OPI PSRAM, the chip claims IO35, IO36 and IO37 for
the PSRAM chip at boot, before your code ever runs. Those are three of my four SD
pins. So the chip was politely stealing my SD card bus out from under me and my
code was just... talking to nothing.

Set **PSRAM: Disabled**. Card mounted instantly. First try. I stared at the wall
for a bit.

(I put a giant comment about this at the top of the file so future me never loses
those two hours again.)

### Bug 2: the screen that would not stop vibrating

Second one was funnier. The whole UI slides around smoothly because every
animation is just a number easing toward a target a little bit each frame. Get
close enough, snap to the target, stop.

The "close enough" check was:

```c
if (abs(d) < 0.001f) return target;
```

`d` is a float. `abs()` in Arduino is the **integer** version. So `abs(0.4)` is
`0`. Which is less than 0.001. Which means it snapped when it wasn't done, then
drifted, then snapped, then drifted, forever. My beautiful liquid iOS animation
looked like the screen was having a panic attack.

Fix was one letter:

```c
if (fabs(d) < 0.001f) return target;
```

`fabs` is the float one. Everything went smooth immediately. One letter. Forty
minutes.

![The list and actions handling code](Screenshot_from_2026-07-13_14-58-03.png)

**Time: 6 hours.** Around 4 writing, 2 losing my mind.

---

## Where it's at

Board's routed, case is modeled, firmware works. Next up is actually ordering the
PCB and printing the case, and then finding out how many things I got wrong in
real life that looked fine on screen. Realistically: several.

Stuff I still want to do:

* Add passwords **on** the device instead of over the serial monitor (a password
  manager that needs a computer to set up is a little embarrassing)
* Longer PIN than 4 digits
* Change your PIN without wiping everything
* A lockout after too many wrong guesses

**Grand total: 34 hours.** Would do again. Have already started thinking about
version 2, which is the real sign of a problem.
