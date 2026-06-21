# Smart Medical Device Tester and Calibration System

An automated bench that tests and calibrates a patient-monitor ECG channel. It generates a known
synthetic ECG, injects it into a device under test, reads back what the device measured, and marks
each step PASS or FAIL against a tolerance while also running a calibration sweep. It runs on two
ESP32 boards and is controlled from a phone through a page the tester serves over its own Wi-Fi. This
is a student proof-of-concept with a self-built stand-in monitor, not a certified analyzer and not a
medical device.

## Overview

Before a patient monitor's ECG channel is trusted in a hospital, and at intervals afterwards, someone
has to check it: does it report the right heart rate, does it measure the right amplitude, does it
notice when a lead falls off. In industry that job is done with an ECG simulator feeding a known signal
into the monitor and an analyzer checking what the monitor reports back. Fluke Biomedical, Rigel,
Datrend and BC Group build these as certified, traceable instruments.

This project reproduces that generate-measure-compare-calibrate loop on a desk. The genuinely hard part
of real biomedical testing is not making a reference ECG, it is capturing what the monitor actually
measured so a machine can decide PASS or FAIL, because a real monitor only exposes that if it has a data
port. I solved it honestly: instead of buying a certified monitor I built the device under test myself
from an AD8232 ECG front-end and a second ESP32, so it reports its measured heart rate and amplitude
back over a serial link and the loop closes every time. That is the one design decision the whole
project turns on, and it is why I am careful never to claim I tested a commercial monitor.

## Key features

- Generates a synthetic P-QRS-T ECG on the ESP32 DAC, sweeping heart rate across 30, 60, 120, 180 and
  240 BPM and amplitude across 0.5, 1.0 and 2.0 mV.
- Attenuates the 0 to 3.3 V DAC output by about 1000 times and AC-couples it, so the signal reaching
  the front-end is a realistic ~1 mV ECG rather than a logic-level voltage.
- Reads the device under test's measured BPM, amplitude and leads-off flag back over a UART link once a
  second.
- Runs a fixed ten-step test plan and marks each step PASS or FAIL. Heart-rate tolerance is the larger
  of plus or minus 5 BPM or 5 percent; amplitude tolerance is plus or minus 10 percent but never tighter
  than plus or minus 0.10 mV, because a bare 10 percent band at the 0.5 mV step is finer than an 8-bit
  DAC on a breadboard can resolve. The plan includes a mains-hum-plus-baseline-wander robustness step and
  a leads-off step.
- Runs a calibration sweep across seven reference heart rates, fits a straight line
  `measured = gain x reference + offset` by least squares, and reports gain, offset and the largest
  error, with a calibration chart on the dashboard.
- Hosts its own Wi-Fi access point and serves a live dashboard with the step table, an overall verdict,
  live generated-versus-measured readouts, the calibration curve and a leads-off badge. No cloud, no
  router, no app.
- Logs a full run to CSV over Wi-Fi with an optional Python script, which is the validation-data and
  calibration-record deliverable.
- Runs standalone from two buttons and an OLED once flashed; a PC is only needed to log.

## How it works

The Tester board builds one P-QRS-T complex as a lookup table and replays it on its DAC at whatever heart
rate it currently commands, holding a flat baseline between beats. That output is shrunk about 1000 times
by a resistor divider and passed through a capacitor, so the AD8232 on the DUT board sees a
millivolt-scale ECG. The AD8232 amplifies it by roughly
1100 times and filters it, and the DUT's ESP32 samples that output at 250 Hz, removes the baseline,
detects R-peaks with an adaptive threshold and a refractory period, works out BPM and amplitude, and
reports them back. For each step the Tester compares the reported value against what it generated and
records a verdict. In calibration mode it sweeps the reference and fits a correction line.

```
   Tester ESP32 (instrument)                        DUT ESP32 + AD8232 (monitor under test)
   -------------------------                        ---------------------------------------
   ECG lookup table -> DAC (GPIO25)                  AD8232 amplifies ~1100x + filters
        |                                                   ^                  |
        v                                                   |                  v
   100k / 100 ohm divider (~1/1000) + 1uF ---> RA input     |             ADC @ 250 Hz (GPIO34)
        (0-3.3 V becomes ~mV)                                |             baseline removal
                                                             |             R-peak detect -> BPM
   UART2 (GPIO16/17)  <==== "BPM=59 AMP=0.98 LO=0" =========+             amplitude estimate
        |
        v
   compare vs tolerance -> PASS / FAIL     calibration fit (gain / offset / max error)
   OLED + green/red LEDs + buzzer
   Wi-Fi SoftAP "MedTester"  ->  dashboard at http://192.168.4.1
                     |
                     v
              phone / laptop browser
```

The DAC waveform is driven from a cooperative scheduler in the main loop rather than a hardware timer
interrupt, which keeps the code simple and lets the same loop serve the web page and read the serial
link. The trade-off is that the OLED is redrawn only a few times a second so its I2C traffic does not
disturb the 250 Hz sample cadence, and that is enough because the DUT averages several beats.

One detail worth calling out, because it cost me the high-rate test steps before I caught it: the lookup
table holds only the complex, not the whole beat. My first version stretched the complex to fill the beat
period, which meant the R spike got narrower as the rate went up. At 240 BPM it was about 2.5 ms wide,
thinner than a single 4 ms sample and well outside the AD8232's roughly 40 Hz passband, so the DUT missed
the beats and steps 4 and 5 failed for a reason that had nothing to do with the DUT. Holding the complex
at a fixed 400 ms and shortening only the gap between beats, which is what a real heart does, keeps the R
spike around 19 ms wide even at 240 BPM.

## Tech stack

| Area | What I used |
|---|---|
| Boards | 2x ESP32 DevKit V1 (30-pin), programmed as "ESP32 Dev Module" |
| Toolchain | Arduino IDE with the Espressif esp32 board package; Serial Monitor at 115200 baud |
| Tester libraries | Adafruit SSD1306 and Adafruit GFX for the OLED; built-in WiFi and WebServer for the hotspot and dashboard; built-in Wire for I2C |
| DUT libraries | None beyond the ESP32 core; plain analogRead and Serial2 |
| Signal generation | 8-bit DAC on GPIO25 replaying a Gaussian-built P-QRS-T lookup table at 250 Hz |
| Signal processing | Software baseline removal, adaptive-threshold R-peak detection with a refractory period, RR averaging |
| Front-end | AD8232 single-lead ECG module (gain ~1100, built-in filtering, leads-off detect) |
| Dashboard | Vanilla HTML, CSS and JavaScript with a plain canvas chart, served from the Tester |
| Logger | Python 3, using requests if present and falling back to the standard-library urllib |

## Repository structure

```
07_Medical_Device_Tester/
|-- README.md                          this file
`-- code/
    |-- tester_esp32/                  open this folder's .ino in the Arduino IDE
    |   |-- tester_esp32.ino           Tester firmware: ECG generation, sequencer, compare, calibration, OLED, Wi-Fi
    |   `-- index_html.h               the dashboard page the firmware serves (see note below)
    |-- dut_monitor_esp32/
    |   `-- dut_monitor_esp32.ino      DUT stand-in monitor: AD8232 acquire + R-peak/BPM/amplitude + serial report
    |-- dashboard.html                 editable copy of the dashboard (same markup as index_html.h)
    `-- report_logger.py               logs a run to CSV over Wi-Fi (validation data)
```

Two layout details that are not cosmetic, both of which I hit while getting this to build:

Each sketch sits in its own folder named after its `.ino`, because Arduino compiles *every* `.ino` in a
folder as one translation unit. With both sketches side by side in `code/`, the build fails on
`redefinition of 'const int PIN_TX2'` and duplicate `setup()`/`loop()`.

The dashboard markup lives in `index_html.h` rather than inside the `.ino`. The Arduino preprocessor
auto-generates function prototypes and does not understand C++ raw string literals, so it reads the
page's JavaScript `function` lines as C++ and the build dies with `error: 'function' does not name a
type`. Plain `.h` files skip that step. Keep `index_html.h` and `dashboard.html` in sync if you edit
either.

## What you need

### Bill of materials

Essentials come to roughly Rs 1,500 to 2,000. Sourced from Robu.in, Amazon.in or SP Road in Bengaluru.

| Part | Qty | Essential | Role |
|---|---|---|---|
| ESP32 DevKit V1 (30-pin) | 2 | Yes | Board 1 = Tester (DAC + Wi-Fi), Board 2 = DUT monitor |
| AD8232 single-lead ECG module | 1 | Yes | ECG analog front-end for the DUT (gain ~1100, filtering, leads-off) |
| SSD1306 0.96" OLED, I2C, 128x64 | 1 | Yes | Tester status screen |
| Resistor 100 kohm | 1 | Yes | Attenuator series resistor |
| Resistor 100 ohm | 1 | Yes | Attenuator bottom resistor (to ground) |
| Resistor 10 kohm | 2-3 | Yes | AD8232 input reference and spares |
| Film capacitor 1 uF | 1 | Yes | AC-couples the generated signal into the AD8232 |
| Momentary push button | 2 | Yes | Start/Stop and Mode (Test/Calibrate) |
| 5 mm LED, green and red | 1 each | Yes | PASS (green), FAIL/FAULT (red) |
| Resistor 220 ohm | 2 | Yes | LED current limiting |
| Breadboard + DuPont jumpers (M-M, M-F) | 1 set | Yes | Solderless wiring |
| Micro-USB cable | 2 | Yes | Program and power the two boards |
| Passive buzzer | 1 | No | Beep on complete / fault |
| USB power bank (5 V) | 1 | No | Standalone bench power |

### Wiring and pin maps

Both boards run at 3.3 V logic, so the UART link needs no level shifter, but every ground must be tied
together: both ESP32s, the AD8232, the OLED, the buttons and the LEDs share one ground rail. Analog
reads use ADC1 pins (GPIO32 to GPIO39) because ADC2 does not work while Wi-Fi is active. Do not feed the
3.3 V DAC straight into the AD8232; it expects millivolts, which is what the divider and capacitor
provide. The I2C OLED is at address 0x3C.

Tester board (ESP32 #1):

| ESP32 pin | Connects to | Note |
|---|---|---|
| GPIO25 (DAC1) | Attenuator input (100 kohm) | Synthetic ECG out, 0 to 3.3 V before attenuation |
| GPIO21 (SDA) | OLED SDA | I2C |
| GPIO22 (SCL) | OLED SCL | I2C |
| GPIO16 (RX2) | DUT GPIO17 (TX2) | UART2 receive |
| GPIO17 (TX2) | DUT GPIO16 (RX2) | UART2 send |
| GPIO32 | Start/Stop button, other leg to GND | INPUT_PULLUP |
| GPIO33 | Mode button, other leg to GND | INPUT_PULLUP |
| GPIO26 | 220 ohm to green LED, LED to GND | PASS |
| GPIO27 | 220 ohm to red LED, LED to GND | FAIL / FAULT |
| GPIO14 | Passive buzzer to GND | Optional |
| GPIO34 (ADC1) | Tester's own GPIO25 (the DAC pin) | Optional self-monitor. Input-only ADC1 pin, used only by TEST_MODE so the board can read back its own DAC output without a multimeter. Not needed for normal runs. |
| 3V3 | OLED VCC | Logic power |
| GND | Common ground rail | Shared with the DUT and AD8232 |

DUT board (ESP32 #2 + AD8232):

| ESP32 pin | Connects to | Note |
|---|---|---|
| GPIO34 (ADC1) | AD8232 OUTPUT | Input-only ADC pin, reads the amplified ECG |
| GPIO25 | AD8232 LO+ | Leads-off detect (optional) |
| GPIO26 | AD8232 LO- | Leads-off detect (optional) |
| GPIO17 (TX2) | Tester GPIO16 (RX2) | UART2 report |
| GPIO16 (RX2) | Tester GPIO17 (TX2) | UART2 commands |
| 3V3 | AD8232 3.3V | Front-end power |
| GND | AD8232 GND + common ground | Shared ground is essential |

The signal-injection chain: Tester GPIO25 goes through a 100 kohm resistor in series to a node, and a
100 ohm resistor runs from that node to ground, which gives about a 1/1000 divider. From that node a
1 uF capacitor carries the signal into the AD8232 RA pad (the + input). Reference the AD8232 LA and RL
pads to ground through 10 kohm each. The result is a roughly 1 mV ECG at the front-end, which the
AD8232 amplifies back up to a clean ADC-readable swing.

```
  TESTER GPIO25 (DAC) --[100k]--+--)|(1uF---> AD8232 RA (+ input)
                                |
                             [100 ohm]
                                |
                               GND  (shared with everything, including the DUT)
```

### Arduino libraries to install

In the Arduino IDE Library Manager, install for the Tester sketch:

- Adafruit SSD1306
- Adafruit GFX Library

WiFi, WebServer and Wire are part of the ESP32 board package, so there is nothing else to install. The
DUT sketch needs no libraries at all.

## Setup and run

