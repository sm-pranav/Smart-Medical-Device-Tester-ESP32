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

