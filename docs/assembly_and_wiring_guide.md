# Assembly & Wiring Guide - Smart Medical Device Tester

This is the click-simple build guide. If you can plug jumper wires into a breadboard and click
Upload in the Arduino IDE, you can build this. Everything is written for someone who has never
touched a microcontroller. Follow the tables exactly, test each part on its own, then combine.

The bench is two small boards on a breadboard:

- **Tester board** (ESP32 #1) - the instrument. It generates a fake ECG, sends it out, reads the
  result back, and decides PASS/FAIL. It also hosts the Wi-Fi dashboard.
- **DUT board** (ESP32 #2 + AD8232) - the "ECG monitor under test". It receives the fake ECG,
  measures the heart rate and amplitude, and reports those numbers back to the Tester.

> **Honest note.** The "device under test" is a monitor we build ourselves from an AD8232 chip and a
> second ESP32, so the whole test loop closes on your desk. A real tester would feed a certified
> patient monitor and read its numbers back through the monitor's data port. This is a
> proof-of-concept for a student portfolio. It is **not a medical device** and never touches a person.

---

## 1. Full Bill of Materials

Buy from Robu.in, Amazon.in, or SP Road (Bengaluru). Prices are rough street prices in INR and
change often. **Essentials** get you a fully working demo. **Optional** parts are nice-to-have.

| # | Part | Qty | Essential? | ~INR | What it does |
|:--|:--|:--|:--|:--|:--|
| 1 | ESP32 DevKit V1 (30-pin) | 2 | Essential | 350 x2 | Board 1 = Tester (has the DAC + Wi-Fi). Board 2 = the DUT monitor. Both run at 3.3 V logic. |
| 2 | AD8232 single-lead ECG module | 1 | Essential | 250 | The ECG analog front-end. It amplifies and filters the tiny ECG. This is what makes the DUT a believable "ECG monitor". |
| 3 | 0.96" OLED, SSD1306, I2C, 128x64 | 1 | Essential | 200 | The Tester's on-bench screen: step, generated vs measured, PASS/FAIL. |
| 4 | Resistor 100 kohm (1/4 W) | 1 | Essential | 1 | Series resistor of the attenuator (drops the signal to millivolts). |
| 5 | Resistor 100 ohm (1/4 W) | 1 | Essential | 1 | Bottom resistor of the attenuator (to ground). |
| 6 | Resistor 10 kohm (1/4 W) | 2-3 | Essential | 1 each | Handy for biasing/reference and spare dividers. |
| 7 | Film capacitor 1 uF | 1 | Essential | 5 | AC-couples the generated signal into the AD8232 (blocks DC, passes the ECG). |
| 8 | Momentary push button | 2 | Essential | 3 each | Start/Stop and Mode (Test/Calibrate). |
| 9 | 5 mm LED green + red | 1 each | Essential | 2 each | PASS (green) / FAIL or FAULT (red). |
| 10 | Resistor 220 ohm | 2 | Essential | 1 each | Current-limit for the two LEDs. |
| 11 | Breadboard (half or full size) | 1-2 | Essential | 70 each | Solderless wiring. Two small ones keep the boards tidy. |
| 12 | DuPont jumper wires (M-M and M-F) | 1 pack | Essential | 100 | All the connections. Get a 40-pack of each. |
| 13 | Passive buzzer | 1 | Optional | 10 | Beep on test complete / fault. |
| 14 | USB power bank (5 V, slim) | 1 | Optional | 500 | Run the bench standalone without a PC. |
| 15 | 2x micro-USB cables | 2 | Essential | 50 each | Program and power the two ESP32 boards. |

**Approximate total:** essentials come to roughly **Rs 1,500 - 2,000**. With the buzzer and a power
bank, **Rs 2,000 - 2,500**. You very likely already have USB cables and a phone to view the dashboard.

A note on the AD8232 module: buy the small breakout that has labelled pads such as `OUTPUT`, `LO+`,
`LO-`, `SDN`, `3.3V`, `GND`, and a 3-pin electrode header (`RA`, `LA`, `RL`). Almost every AD8232
board sold in India is this same layout.

---

## 2. Tools You Need

| Tool | Essential? | Why |
|:--|:--|:--|
| A computer with the Arduino IDE | Essential | To flash both boards. Free download. |
| DuPont jumper wires + breadboard | Essential | The wiring itself (item 11/12 above). |
| Small multimeter | Strongly recommended | To check the attenuator output and hunt down wiring mistakes. A Rs 300 meter is plenty. |
| Soldering iron | Optional | Only if your AD8232 or ESP32 came with loose header pins. Most boards arrive pre-soldered. |
| Small screwdriver | Optional | For a project box if you enclose it later. |

If your boards came with the pin headers already soldered (most do), you need **no soldering at
all**. If the headers are loose in the bag, solder them onto the board once, then everything else is
plug-in.

---

## 3. Wiring - Breadboard First

Read this whole section before plugging anything in. Wire with the USB cables unplugged, then plug in
to power once each sub-section is done.

### 3.0 The one rule that matters most

> **Tie ALL grounds together.** Both ESP32 boards, the AD8232, the OLED, the buttons, the LEDs - every
> GND pin must connect to the same ground rail on the breadboard. If the two boards do not share
> ground, the UART link will produce garbage and the injected ECG will float. When in doubt, add
> another ground jumper.

And the second rule:

> **Never feed 3.3 V straight into the AD8232 input.** The AD8232 expects a signal around **1 millivolt**
> (0.001 V). The ESP32 DAC puts out up to 3.3 V. We shrink that by about 1000x with a resistor divider
> and pass it through a capacitor before it reaches the AD8232. Skipping the divider will just peg the
> front-end and you will get nonsense readings.

### 3.1 Tester ESP32 (#1) <-> OLED

| Tester ESP32 pin | OLED pin | Notes |
|:--|:--|:--|
| 3V3 | VCC | Power |
| GND | GND | Common ground |
| GPIO21 | SDA | I2C data |
| GPIO22 | SCL | I2C clock |

```
   TESTER ESP32                 OLED (SSD1306, 0x3C)
   +----------+                 +-----------------+
   |     3V3  |---------------->| VCC             |
   |     GND  |---------------->| GND             |
   |  GPIO21  |--- SDA -------->| SDA             |
   |  GPIO22  |--- SCL -------->| SCL             |
   +----------+                 +-----------------+
```

### 3.2 Tester ESP32 (#1) <-> buttons, LEDs, buzzer

Buttons use the ESP32's internal pull-ups, so one leg goes to the GPIO and the other leg goes to GND.
No extra resistor needed for the buttons. LEDs get a 220 ohm resistor in series (long leg = positive =
to the GPIO through the resistor, short leg = to GND).

| Tester ESP32 pin | Connects to | Notes |
|:--|:--|:--|
| GPIO32 | Button 1, one leg | Other leg of button 1 -> GND (Start/Stop) |
| GPIO33 | Button 2, one leg | Other leg of button 2 -> GND (Mode: Test/Calibrate) |
| GPIO26 | 220 ohm -> Green LED + leg | Green LED - leg -> GND (PASS) |
| GPIO27 | 220 ohm -> Red LED + leg | Red LED - leg -> GND (FAIL/FAULT) |
| GPIO14 | Passive buzzer + | Buzzer - -> GND (optional) |

```
   GPIO32 ---[ BTN1 ]--- GND        (Start/Stop)
   GPIO33 ---[ BTN2 ]--- GND        (Mode)

   GPIO26 ---[220R]---|>|--- GND    green LED (PASS)
   GPIO27 ---[220R]---|>|--- GND    red LED  (FAIL/FAULT)
   GPIO14 ----------[buzzer]--- GND (optional)
```

### 3.3 The attenuator: Tester DAC -> millivolt ECG -> AD8232 input

This is the heart of the bench and the part to build carefully. The ESP32 DAC on **GPIO25** puts out
0 to 3.3 V. We do two things to it:

1. **Shrink it ~1000x** with a voltage divider so 1 V at the DAC becomes about 1 mV at the AD8232.
2. **AC-couple it** through a 1 uF capacitor so only the moving ECG shape passes and the steady DC
   level is blocked (the AD8232 sets its own reference internally).

Build this small chain on the breadboard:

```
  TESTER GPIO25 (DAC)
        |
        +---[ 100 kohm ]---+------)|(-----> AD8232  RA pad (+ input)
                           |     1uF
                        [ 100 ohm ]
                           |
                          GND  (shared with everything)
```

- The **100 kohm** goes in series from the DAC pin to a middle node.
- The **100 ohm** goes from that middle node to **GND**. The middle node now carries about 1/1000 of
  the DAC voltage (100 / (100000 + 100) is close to 1/1000).
- From the middle node, go through the **1 uF capacitor** into the AD8232's **RA** pad (the + input).
- Tie the AD8232's **LA** and **RL** pads to the shared **GND** through a 10 kohm each (a simple
  reference). Many AD8232 boards are happy with LA and RL just referenced to ground for this bench
  injection; if your readings are noisy, add the 10 kohm from LA to GND and RL to GND.

Why these values: a real ECG R-wave is about 1 mV. The tester commands amplitudes of 0.5, 1.0 and
2.0 mV. With the ~1/1000 divider, that means the DAC swings about 0.5 V, 1.0 V and 2.0 V - all
comfortably inside its range. If you own a multimeter, you can verify the divider: put the DAC at a
steady mid value (TEST_MODE does a slow ramp) and confirm the middle node reads roughly 1/1000 of the
DAC pin voltage.

> The RA / LA / RL pads are where ECG electrodes would normally clip onto a body. Here we inject our
> synthetic signal into RA instead. This is the "proof-of-concept signal injection" - it exercises the
> exact same front-end path a real electrode would.

**Optional self-monitor jumper (only if you do not own a multimeter).** One extra wire from the Tester's
**GPIO25 straight to its own GPIO34** lets the Tester read back its own DAC output. In `TEST_MODE` the
DAC-ramp test then prints both the commanded value and the measured value, so you can confirm the DAC is
working with no test gear at all. Tap it at GPIO25 (before the divider), never after it - after the
divider the signal is millivolts and the ADC cannot resolve it. Skip this wire entirely if you have a
multimeter; the firmware does not need it in normal operation.

| Tester ESP32 pin | Connects to | Notes |
|:--|:--|:--|
| GPIO34 (ADC1) | Tester GPIO25 (the DAC pin) | Optional. Input-only ADC1 pin. Used only by `TEST_MODE`; not needed for normal runs. |

### 3.4 DUT ESP32 (#2) <-> AD8232

| DUT ESP32 pin | AD8232 pin | Notes |
|:--|:--|:--|
| 3V3 | 3.3V | Power the front-end |
| GND | GND | Common ground (shared with the Tester) |
| GPIO34 | OUTPUT | The amplified/filtered ECG -> ADC1 input-only pin |
| GPIO25 | LO+ | Leads-off detect (optional) |
| GPIO26 | LO- | Leads-off detect (optional) |

```
   DUT ESP32                    AD8232
   +----------+                 +-----------------+
   |     3V3  |---------------->| 3.3V            |
   |     GND  |---------------->| GND             |
   |  GPIO34  |<--- OUTPUT -----| OUTPUT          |
   |  GPIO25  |<--- LO+ --------| LO+  (optional) |
   |  GPIO26  |<--- LO- --------| LO-  (optional) |
   +----------+                 | RA <- injected mV from the attenuator (3.3)
                                | LA, RL -> reference to GND (via 10k)
                                +-----------------+
```

Leave the AD8232 `SDN` pad unconnected (or tie to 3.3V) so the chip stays awake.

**About LO+ / LO- (leads-off).** These two wires are genuinely optional. The firmware sets those pins to
`INPUT_PULLDOWN`, so if you leave them unwired they read as "leads connected" and everything still works
- you will not get random false leads-off alarms. Wire them if you want the dashboard's lead badge to be
live. Be aware that the AD8232 detects a *truly open electrode* by DC path, and this bench feeds the
front-end through a capacitor rather than real electrodes, so on many modules the badge will simply stay
on "Leads connected" for the whole run. That is not a fault. If you want to watch the badge flip, unclip
the **LA** or **RL** reference wire while the bench is running - that is a real open-lead condition.

### 3.5 The UART cross-link (Tester <-> DUT)

The two boards talk over a serial link. The golden rule of serial wiring is **TX goes to RX** and
**RX goes to TX** - crossed over. Both boards are 3.3 V, so no level shifter is needed. They must
share ground (again).

| Tester ESP32 | DUT ESP32 | Notes |
|:--|:--|:--|
| GPIO17 (TX2) | GPIO16 (RX2) | Tester sends -> DUT receives |
| GPIO16 (RX2) | GPIO17 (TX2) | DUT sends -> Tester receives |
| GND | GND | Shared ground (essential) |

```
   TESTER GPIO17 (TX) ------------> GPIO16 (RX) DUT
   TESTER GPIO16 (RX) <------------ GPIO17 (TX) DUT
   TESTER GND        <------------> GND        DUT
```

If you see garbage characters or nothing at all on the link, 9 times out of 10 the TX/RX are not
crossed, or the grounds are not tied. The link runs at 115200 baud.

### 3.6 Full wiring at a glance

```
                         COMMON GROUND RAIL  (all GND pins land here)
   ================================================================================

   TESTER ESP32 #1                                  DUT ESP32 #2
   ---------------                                  --------------
   GPIO25 DAC --[100k]--+--)|(1uF-- AD8232 RA        GPIO34 <-- AD8232 OUTPUT
                        |                            GPIO25 <-- AD8232 LO+
                     [100 ohm]                       GPIO26 <-- AD8232 LO-
                        |                            3V3    --> AD8232 3.3V
                       GND                           GND    --> AD8232 GND
   GPIO21 SDA --> OLED SDA
   GPIO22 SCL --> OLED SCL                           GPIO17 TX2 --> TESTER GPIO16 RX2
   GPIO32 --> Start/Stop button --> GND              GPIO16 RX2 <-- TESTER GPIO17 TX2
   GPIO33 --> Mode button --> GND
   GPIO26 --[220R]-->|>|-- GND  green
   GPIO27 --[220R]-->|>|-- GND  red
   GPIO14 --> buzzer --> GND (optional)
   GPIO17 TX2 --> DUT GPIO16 RX2
   GPIO16 RX2 <-- DUT GPIO17 TX2
```

---

## 4. Test Each Part Alone Before Combining

### 4.0 First, how the sketches are laid out (this matters)

```
code/
  tester_esp32/
    tester_esp32.ino      <- open THIS one for the Tester
    index_html.h          <- the dashboard page; must stay in this folder
  dut_monitor_esp32/
    dut_monitor_esp32.ino <- open THIS one for the DUT
```

Each sketch has its own folder, named exactly like its `.ino` file. That is not just tidiness - the
Arduino IDE compiles **every** `.ino` in a folder together as one program, so if both sketches sit in
the same folder the build fails with errors like `redefinition of 'const int PIN_TX2'` and two
`setup()` functions. Keep them in separate folders and open one at a time.

`index_html.h` holds the dashboard web page. The Arduino IDE will show it as a second tab next to the
Tester sketch, which is normal - do not delete it or move it. (The page has to live in a `.h` file
because the IDE tries to read the page's JavaScript as C++ if you put it directly in the `.ino`.)

You will need libraries `Adafruit SSD1306` and `Adafruit GFX Library` from Library Manager for the
Tester; accept the prompt to also install `Adafruit BusIO`, which they depend on. The DUT sketch needs
no libraries. Set Board to **ESP32 Dev Module** for both.

Now bring up one piece at a time. Do not wire everything and hope. Both sketches have a
`#define TEST_MODE` switch near the top - set it to `1`, upload, watch the Serial Monitor (115200 baud),
then set it back to `0` for normal use.

### 4.1 OLED / I2C scanner (Tester)

Set `#define TEST_MODE 1` in `tester_esp32.ino` and upload. The first thing it does is scan the I2C
bus and print any addresses it finds. You want to see `found 0x3C`. If you see `0x3D`, change
`OLED_ADDR` to `0x3D` in the sketch. If you see nothing, SDA/SCL are swapped or there is no shared
ground.

### 4.2 DAC ramp (Tester)

Still in TEST_MODE, the Tester slowly ramps GPIO25 from 0 to full and back over a few seconds. Put a
multimeter on GPIO25 and watch the voltage climb from ~0 V to ~3.3 V. Then move the meter to the
middle node of the divider and confirm it reads roughly 1/1000 of that (a few millivolts). This
proves the DAC and the attenuator both work.

### 4.3 AD8232 raw read (DUT)

Set `#define TEST_MODE 1` in `dut_monitor_esp32.ino` and upload. It streams the raw ADC value from
GPIO34. Open the Arduino **Serial Plotter** (Tools menu) at 115200 baud. With the tester injecting a
signal you will see a moving ECG-like trace. With no signal you will see a flatish line near mid-scale.
This is also where you tune `AFE_GAIN` if the amplitude reading is off.

### 4.4 UART loopback / echo

With both boards flashed (DUT normal, Tester in TEST_MODE), the Tester's test routine echoes anything
the DUT sends. You should see lines like `BPM=59 AMP=0.98 LO=0` scroll past in the Tester's Serial
Monitor. If you see nothing, the TX/RX are not crossed or grounds are not tied. If you see garbled
text, check that both are at 115200 baud.

### 4.5 Button test (Tester)

TEST_MODE prints the two button states for a few seconds. Press each button and confirm the printed
value flips between 1 and 0. If a button never changes, its second leg is not on GND.

Once every piece checks out, set both `TEST_MODE` back to `0` and re-upload.

---

## 5. Bench Layout

Keep it tidy so the demo photographs well and behaves reliably:

- Put the two ESP32 boards on one long breadboard (or two small ones side by side), USB ports facing
  out so you can plug both cables in.
- Build the attenuator (100k, 100 ohm, 1 uF) as a tight little cluster right next to the AD8232. Keep
  the wire from the divider node to the AD8232 RA pad **short** - long analog wires pick up hum.
- Run the two UART jumpers and the ground jumper between the boards as a neat little bundle.
- Put the OLED, the two buttons, and the two LEDs along the front edge near the Tester so they are the
  "face" of the instrument.
- Optional: mount the whole breadboard in a small clear project box with the OLED and buttons on the
  lid. This makes it look like a real bench instrument.

## 6. Powering It

Simplest path: plug **both** ESP32 boards into USB - one to your PC (to see the Serial Monitor) and
one to a phone charger or the second USB port. That is enough to run the full demo.

Standalone: power both boards from a single USB power bank using a short two-port cable or a small
USB hub, so the bench runs with no computer. The dashboard is served by the Tester itself, so a phone
is the only "screen" you need.

> Even when the two boards are powered from different USB sources, **their grounds must still be tied
> together** with a jumper. Different USB chargers do not share a ground otherwise, and the UART link
> and the injected signal both depend on a common ground.

## 7. Troubleshooting

| Symptom | Likely cause and fix |
|:--|:--|
| Won't compile: `redefinition of 'const int PIN_TX2'`, or two `setup()` functions | Both sketches are in one folder. Arduino builds every `.ino` in a folder together. Put each in its own folder named after its `.ino` (see 4.0). |
| Won't compile: `'function' does not name a type; did you mean 'union'?` | The dashboard page ended up inside the `.ino`. It must stay in `index_html.h` in the Tester's folder (see 4.0). |
| Won't compile: `Adafruit_SSD1306.h: No such file or directory` | Libraries missing. Library Manager: install `Adafruit SSD1306` and `Adafruit GFX Library`, accept `Adafruit BusIO`. |
| OLED stays blank | Not on 3V3, SDA/SCL swapped, or wrong address. Run TEST_MODE, look for `found 0x3C`. If it shows `0x3D`, set `OLED_ADDR` to `0x3D`. |
| I2C scan finds nothing | SDA/SCL swapped or no shared ground. Re-seat the four wires; OLED VCC on 3V3. |
| DUT always reports `BPM=0` | No signal reaching the AD8232. Check the attenuator chain and the 1 uF cap orientation, confirm the AD8232 is powered (3.3V + GND), and that RA carries the injected signal. Run the DUT in TEST_MODE and look at the raw trace in the Serial Plotter. |
| BPM is wildly wrong / jumpy | Divider ratio off, poor grounding, or noise pickup. Shorten the analog wire to RA, make sure the 100 ohm is to ground, and confirm all grounds are tied. Increase `THRESH_FRAC` in the DUT slightly if noise causes double-counting. |
| Amplitude reading looks low, especially at 2 mV | Expected. The AD8232 has a fixed gain (~1100) and the ESP32 ADC tops out at ~3.3 V, so signals above roughly 1.5 mV start to clip. The 2.0 mV step exercises the top of the front-end's range on purpose. Tune `AFE_GAIN` in the DUT so a known 1.0 mV reads back near 1.00. |
| UART shows garbage | Baud mismatch (both must be 115200) or TX/RX not crossed. Tester GPIO17 -> DUT GPIO16, Tester GPIO16 <- DUT GPIO17. |
| UART shows nothing / FAULT on screen | TX/RX swapped the wrong way, or grounds not tied. The Tester declares FAULT if it gets no report for 4 seconds. |
| `MedTester` Wi-Fi not visible | Wait ~10 s after boot. Check the Serial Monitor prints `SoftAP 'MedTester' at http://192.168.4.1`. Move closer to the board. |
| Dashboard will not load | Use `http://192.168.4.1` (not https). Ignore the phone's "no internet" warning for that network and stay connected. |
| DAC output looks wrong on the meter | Confirm you are probing GPIO25 (DAC1), not another pin. In TEST_MODE the ramp is slow; a fast meter helps. |
| ESP32 resets or browns out | Power dip. Use a decent USB source or a power bank with an always-on mode; keep the boards separate USB ports if one is struggling. |
| Green and red LEDs both off during a run | Normal until the first step verdict lands (about 7 seconds in). They light per-step after that. |

Once the parts pass their individual tests and the two boards are cross-wired with a shared ground,
flash both in normal mode, press Start on the Tester, and the automated test plan will run.
