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
