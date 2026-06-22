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

1. **Install ESP32 board support.** In the Arduino IDE open Preferences and add this to Additional
   Boards Manager URLs:
   ```
   https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
   ```
   Then in Boards Manager search for `esp32` (by Espressif) and install it.
2. **Install the OLED libraries.** In Library Manager install `Adafruit SSD1306` and
   `Adafruit GFX Library`.
3. **Wire both boards** per the pin maps above. Build the attenuator (100 kohm, 100 ohm, 1 uF) right
   next to the AD8232 and keep the wire to the RA pad short. Cross the UART lines (Tester TX to DUT RX,
   Tester RX to DUT TX) and tie all grounds together.
4. **Flash the DUT.** Open `code/dut_monitor_esp32/dut_monitor_esp32.ino`, set Board to
   `ESP32 Dev Module`, select the DUT's COM port, and Upload. For a first bring-up you can set
   `#define TEST_MODE 1` to stream the raw ADC value into the Serial Plotter, then set it back to `0`.
5. **Flash the Tester.** Open `code/tester_esp32/tester_esp32.ino`, select the Tester's COM port, and
   Upload. The IDE will show `index_html.h` as a second tab; that is expected and it must stay in the
   same folder. The Serial Monitor at 115200 baud should print that the SoftAP came up at
   `http://192.168.4.1`. TEST_MODE here runs an I2C scan, a DAC ramp, a button check and a UART echo.
6. **Power on.** With both boards powered, the OLED shows a status screen and the `MedTester` Wi-Fi
   network appears.
7. **Connect and open the dashboard.** On a phone or laptop, join Wi-Fi `MedTester` (password
   `medtest123`) and open `http://192.168.4.1`. Ignore the "no internet" warning for that network.
8. **Run a test.** Press Start on the bench, or the Start button on the dashboard. The ten-step plan
   runs and each step shows PASS or FAIL.
9. **Run a calibration.** With the bench idle, press Mode (or the Calibrate button on the dashboard) to
   switch to calibration, then Start. The sweep runs and the calibration chart and gain/offset/error
   fill in.
10. **Log a run (optional).** With a PC on the `MedTester` network:
    ```
    pip install requests        # optional; the script falls back to the standard library
    python code/report_logger.py
    ```
    It writes a timestamped CSV. Options: `python code/report_logger.py --host 192.168.4.1 --interval 1.0 --out run1.csv`

Both sketches are confirmed to build clean against ESP32 core 3.3.11 with Adafruit SSD1306 2.5.17,
Adafruit GFX 1.12.6 and Adafruit BusIO 1.17.4 (BusIO is pulled in automatically as a dependency, so
accept the IDE's prompt to install it). For reference, the compiler reports:

| Sketch | Flash | RAM |
|---|---|---|
| `dut_monitor_esp32` | 280,512 bytes (21%) | 22,284 bytes (6%) |
| `tester_esp32` | 1,006,792 bytes (76%) | 50,072 bytes (15%) |

The tester is the larger one because it links WiFi, WebServer and the embedded dashboard page. 76% of
the default 1.31 MB app partition still leaves room, but it is worth knowing if you plan to add much.

## How to verify it works

On boot the Serial Monitor should show the access point starting:

```
[WiFi] SoftAP 'MedTester' at http://192.168.4.1
[WEB ] server on port 80
[BOOT] ready. IDLE. press Start to run.
```

Then:

- Press Start in Test mode. The bench steps through the plan. For each heart-rate step the measured BPM
  should land within the larger of plus or minus 5 BPM or 5 percent of the generated value, so the 60 BPM
  step passes anywhere from 55 to 65 and the 240 BPM step passes from 228 to 252. The step shows PASS with
  the green LED and the pass/fail tally climbs.
- Watch the amplitude steps. The 0.5 and 1.0 mV steps should read close and pass. The 2.0 mV step is the
  honest edge case: the AD8232's fixed gain and the ESP32's 3.3 V ADC ceiling mean signals above roughly
  1.5 mV start to clip, so that step may read low and can fail. That is a real dynamic-range finding, not
  a wiring mistake.
- On the leads-off step the tester holds its output flat, so within about three seconds the DUT reports
  `BPM=0 AMP=0.00` and the step passes on that basis. The dashboard's lead badge reads from the AD8232's
  LO+/LO- pins, which detect a genuinely open electrode; because this bench injects a signal through a
  capacitor rather than using real electrodes, the badge may stay on "Leads connected" through the whole
  run. To see the badge actually flip, unclip the AD8232's LA or RL reference wire while it is running.
- Switch to Calibrate and Start. Points appear on the chart near the dashed ideal line, a fitted line is
  drawn, and gain reads near 1.000 with a small offset and a max error of a few BPM.
- If the DUT is unplugged or the serial link is broken, within about four seconds the Tester shows FAULT
  with the red LED, which confirms the fault-detection path.
- The CSV from `report_logger.py` should contain a row per second across the run, with generated and
  measured columns and verdicts. That file is your validation record.

What success looks like: generated and measured heart rate agree within tolerance across the plan, the
verdicts are correct, the calibration curve is close to the ideal line with a sensible gain and offset,
leads-off is flagged, and the same numbers appear on the OLED, the dashboard and the CSV.

## The /data JSON contract

The Tester serves this JSON at `http://192.168.4.1/data`, the dashboard polls it once a second, and the
Python logger writes the scalar keys to CSV. Keep these identical across the firmware, the dashboard and
the logger.

| Key | Type | Meaning |
|---|---|---|
| `state` | string | IDLE, TESTING, CALIBRATE, COMPLETE or FAULT |
| `state_code` | int | 0, 10, 20, 30 or 90 |
| `mode` | string | TEST or CAL |
| `step` | int | Current step or calibration point, 1-based |
| `total_steps` | int | Steps in the active plan |
| `gen_bpm` | number | Generated heart rate, BPM |
| `gen_amp` | number | Generated R-wave amplitude, mV |
| `meas_bpm` | int | DUT-reported heart rate, BPM |
| `meas_amp` | number | DUT-reported amplitude, mV |
| `verdict` | string | Last step verdict: PASS, FAIL or -- |
| `pass_count` | int | Steps passed so far |
| `fail_count` | int | Steps failed so far |
| `overall` | string | PASS, FAIL, RUNNING, FAULT or -- |
| `leads_off` | int | 0 = leads connected, 1 = leads-off detected |
| `cal_gain` | number | Calibration slope (measured = gain x reference + offset) |
| `cal_offset` | number | Calibration intercept, BPM |
| `cal_max_err` | number | Largest calibration error, BPM |
| `uptime_s` | int | Tester uptime, seconds |
| `steps` | array | Per-step objects: `n`, `gb` (gen BPM), `ga` (gen mV), `mb` (meas BPM), `ma` (meas mV), `v` (verdict) |
| `cal` | array | Calibration points: `ref` (reference BPM), `meas` (measured BPM) |

The DUT reports to the Tester over UART as one line per second, in the format
`BPM=<int> AMP=<float> LO=<0 or 1>`, for example `BPM=59 AMP=0.98 LO=0`.

## Safety and scope

This bench runs at 3.3 to 5 V and never connects to a person. The ECG is generated electronically and
injected into a chip, so there are no body electrodes in normal use, and the AD8232 electrode leads
should not be attached to anyone while it is powered. It is a student research prototype, not a medical
device, must not be used on patients or for any clinical decision, and is not a substitute for a
certified analyzer such as those from Fluke Biomedical, Rigel, Datrend or BC Group. Standards such as
IEC 60601-1 (medical electrical safety), IEC 60601-2-27 (ECG monitors) and AAMI/ANSI EC11/EC13 describe
what a real product would meet; this prototype only demonstrates the concepts behind them and is not
tested or certified to any of them.

## Troubleshooting

| Problem | Likely cause and fix |
|---|---|
| Build error: `redefinition of 'const int PIN_TX2'` | Both `.ino` files are in the same folder, so Arduino compiled them as one program. Each sketch needs its own folder named after its `.ino`. |
| Build error: `'function' does not name a type` | The dashboard page has been moved into the `.ino`. It must stay in `index_html.h`; the Arduino preprocessor misreads the page's JavaScript as C++. |
| Build error: `Adafruit_SSD1306.h: No such file` | Libraries not installed. Library Manager, install `Adafruit SSD1306` and `Adafruit GFX Library`, and accept `Adafruit BusIO`. |
| OLED stays blank | Check VCC on 3V3, SDA on 21, SCL on 22. Run the Tester in TEST_MODE and look for `found 0x3C`; if it shows `0x3D`, change `OLED_ADDR` in the sketch. |
| DUT always reports BPM 0 | No signal reaching the AD8232. Check the attenuator chain and the 1 uF cap, confirm the AD8232 is powered, and that RA carries the injected signal. Watch the raw trace in the DUT's TEST_MODE. |
| BPM jumpy or wildly wrong | Grounding or noise. Shorten the wire to RA, confirm the 100 ohm goes to ground, tie all grounds together, and nudge `THRESH_FRAC` up in the DUT if noise causes double-counting. |
| Amplitude reads low at 2 mV | Expected. The AD8232's fixed ~1100 gain with a 3.3 V ADC clips above roughly 1.5 mV. Tune `AFE_GAIN` in the DUT so a known 1.0 mV reads near 1.00, and treat the top step as a dynamic-range check. |
| UART garbage or FAULT on screen | TX/RX not crossed, baud mismatch, or grounds not tied. Tester GPIO17 to DUT GPIO16, Tester GPIO16 to DUT GPIO17, both at 115200, shared ground. |
| MedTester Wi-Fi not visible | Wait about 10 seconds after boot and check the Serial Monitor shows the SoftAP line. Move closer to the board. |
| Dashboard will not load | Use http not https, and stay connected to MedTester despite the phone's no-internet warning. |
| ESP32 resets under load | Power dip. Use a good USB source or a power bank with an always-on mode. |

## Limitations and next steps

The honest scope, so the framing stays straight. The device under test is a monitor I built myself, not
a certified commercial monitor, so this demonstrates the tester architecture rather than a real
qualification. The signal chain is hobby grade: an 8-bit DAC, breadboard resistors and a general-purpose
ADC, so it is repeatable and good for showing the method but not traceable, and it cannot state a
measurement uncertainty the way a real calibration certificate does. The amplitude path is the weakest
part, because the AD8232's fixed gain against a 3.3 V ADC clips above about 1.5 mV, which is exactly why
the 2.0 mV step is included as an honest edge case rather than hidden. Heart-rate accuracy is the strong
part, since it depends only on timing between peaks.

Reasonable next steps would be an instrumentation-grade DAC and analog stage for a cleaner, wider
amplitude range, an interface that reads a real monitor's data port so the DUT could be a genuine
commercial monitor, multi-parameter simulation such as respiration or SpO2, a traceable reference so the
calibration could report real uncertainty, and moving the waveform generation onto a hardware timer for
jitter-free output.

## Author

Pranav S M
