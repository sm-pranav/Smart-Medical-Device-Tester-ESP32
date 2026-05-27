/* =============================================================================
   TESTER  -  ESP32 #1  (the "instrument")
   Smart Medical Device Tester & Calibration System
   -----------------------------------------------------------------------------
   WHAT THIS BOARD IS
     This is the automated tester / calibrator. It behaves like a bench ECG
     simulator + analyzer:
       1) GENERATES a known synthetic ECG (P-QRS-T) at a commanded heart rate and
          amplitude, using the ESP32 DAC on GPIO25.
       2) That 0..3.3 V signal is attenuated by a resistor divider (~divide by
          1000) and AC-coupled through a 1 uF cap down to a ~millivolt ECG, which
          is injected into the AD8232 input on the DUT board.
       3) READS BACK the DUT's measured BPM / amplitude / leads-off over UART2.
       4) COMPARES measured vs generated against a tolerance band -> PASS / FAIL
          for each step of a fixed automated test plan.
       5) CALIBRATION mode sweeps the reference across the range, fits a straight
          line (measured = gain * reference + offset), and reports gain, offset,
          max error and a corrected reading.
     Everything shows on the OLED, on PASS/FAIL LEDs, and on a Wi-Fi dashboard that
     THIS board hosts itself (its own hotspot, no internet, no cloud).

     HONEST SCOPE NOTE: the "device under test" is a self-built stand-in monitor
     (AD8232 + a second ESP32), so the whole loop closes on a desk. A real tester
     would feed a certified monitor and read its values back through the monitor's
     data port. This is a student proof-of-concept, NOT a certified analyzer,
     NOT a medical device, NOT for clinical use.

   PIN MAP  (ESP32 #1 - TESTER)
     GPIO25 (DAC1) -> attenuator input        synthetic ECG out (0..3.3 V)
     GPIO21 (SDA)  -> OLED SDA                 I2C, OLED at 0x3C
     GPIO22 (SCL)  -> OLED SCL                 I2C
     GPIO16 (RX2)  <- DUT TX (GPIO17)          UART2, receive DUT reports
     GPIO17 (TX2)  -> DUT RX (GPIO16)          UART2, send commands (optional)
     GPIO32        -> Button 1 (Start/Stop)    INPUT_PULLUP, other leg to GND
     GPIO33        -> Button 2 (Mode Test/Cal) INPUT_PULLUP, other leg to GND
     GPIO26        -> Green PASS LED (+220 ohm)
     GPIO27        -> Red FAIL/FAULT LED (+220 ohm)
     GPIO14        -> Passive buzzer (optional)
     GPIO34 (ADC1) <- optional self-monitor tap of the generated signal (TEST_MODE)
     5V/VIN, GND   -> power + COMMON GROUND with the DUT and the AD8232 (essential!)

   UART REPORT FORMAT expected from the DUT (must match dut_monitor_esp32.ino):
       BPM=<int> AMP=<float2> LO=<0/1>       e.g.  BPM=59 AMP=0.98 LO=0

   LIBRARIES (install via Arduino Library Manager)
     - Adafruit SSD1306
     - Adafruit GFX Library
     WiFi.h and WebServer.h are built into the ESP32 board package. Wire.h too.

   BOARD SETTINGS (Arduino IDE)
     Board: "ESP32 Dev Module"    Upload speed: 115200    Serial Monitor: 115200
   ============================================================================= */

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <WiFi.h>
#include <WebServer.h>
#include "index_html.h"   // the dashboard page (big raw string; see that file)

// ---- set to 1 for bench bring-up (I2C scan, DAC ramp, UART echo), 0 for normal
#define TEST_MODE 0

// ============================================================================
//  PINS
// ============================================================================
const int PIN_DAC     = 25;   // DAC1 -> attenuator
const int PIN_SDA     = 21;
const int PIN_SCL     = 22;
const int PIN_RX2     = 16;   // <- DUT TX
const int PIN_TX2     = 17;   // -> DUT RX
const int PIN_BTN_RUN = 32;   // Start / Stop
const int PIN_BTN_MODE= 33;   // Test / Calibrate
const int PIN_LED_PASS= 26;   // green
const int PIN_LED_FAIL= 27;   // red
const int PIN_BUZZER  = 14;   // optional
const int PIN_SELFMON = 34;   // optional self-monitor of generated signal (ADC1)

// ============================================================================
//  OLED
// ============================================================================
#define OLED_W 128
#define OLED_H 64
#define OLED_ADDR 0x3C
Adafruit_SSD1306 oled(OLED_W, OLED_H, &Wire, -1);
bool oledOk = false;

// ============================================================================
//  Wi-Fi SoftAP + web server
// ============================================================================
const char* AP_SSID = "MedTester";
const char* AP_PASS = "medtest123";   // >= 8 chars for WPA2
WebServer server(80);

// ============================================================================
//  ECG WAVEFORM GENERATION
// ----------------------------------------------------------------------------
//  We build one normalized ECG beat (P-QRS-T) as a lookup table where the R-peak
//  = 1.0 and the baseline = 0.0 (small negatives for Q and S). We replay it at
//  the commanded heart rate on the DAC. Amplitude is set by scaling the table.
