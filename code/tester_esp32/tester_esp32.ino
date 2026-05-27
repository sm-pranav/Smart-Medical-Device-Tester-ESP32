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
// ============================================================================
const int   SAMPLE_HZ  = 250;                    // DAC update rate
const long  SAMPLE_US  = 1000000L / SAMPLE_HZ;   // 4000 us per sample
const int   TABLE_N    = 200;                    // points in the P-QRS-T complex
float ecgTable[TABLE_N];                         // normalized 0..1 (R = 1.0)

// ---- beat timing -----------------------------------------------------------
// IMPORTANT: the table holds only the P-QRS-T *complex*, NOT the whole beat.
// In a real heart the complex keeps roughly the same duration (~400 ms) no matter
// the rate; what shrinks as the rate rises is the flat resting gap between beats.
// We copy that behaviour. If we instead stretched the complex across the whole
// beat, the R spike would get narrower and narrower at high rates - at 240 BPM it
// would be about 2.5 ms wide, which is thinner than one 4 ms sample AND far above
// the AD8232's ~40 Hz passband, so the DUT would simply miss the beats.
// Keeping the complex a fixed length keeps the R spike ~19 ms wide even at 240 BPM.
const float COMPLEX_MS_MAX   = 400.0f;                                  // ~400 ms complex
const float COMPLEX_SAMP_MAX = COMPLEX_MS_MAX * SAMPLE_HZ / 1000.0f;    // = 100 samples
const float COMPLEX_MAX_FRAC = 0.85f;   // at high rates, compress to fit the beat

// ---- attenuator / amplitude math --------------------------------------------
// Divider: DAC -> R_TOP in series -> node -> R_BOT to GND. The AD8232 sees the
// node voltage. Ratio = R_BOT / (R_TOP + R_BOT). With 100k + 100 ohm ~= 1/1000,
// so 1 V at the DAC becomes ~1 mV at the AD8232 input. We AC-couple with 1 uF so
// only the AC ECG passes; the DAC's DC baseline is blocked.
const float R_TOP = 100000.0f;   // 100 kohm series
const float R_BOT = 100.0f;      // 100 ohm to ground
const float DIV_RATIO = R_BOT / (R_TOP + R_BOT);   // ~0.000999
const int   DAC_BASE  = 90;      // DAC baseline count (~1.16 V); AC-coupled away
const float DAC_VREF  = 3.3f;

// Convert a target R-wave amplitude (mV at the AD8232 input) to DAC counts of
// deviation from the baseline.
float ampToCounts(float mV) {
  float vdac   = (mV / 1000.0f) / DIV_RATIO;   // volts needed at the DAC
  float counts = vdac / DAC_VREF * 255.0f;     // 8-bit DAC
  return counts;
}

// live generation state
volatile float genBPM = 60.0f;   // commanded heart rate
volatile float genAMP = 1.0f;    // commanded R amplitude (mV)
bool  noiseOn   = false;         // inject mains hum + baseline wander (step 9)
bool  outputOff = true;          // true = hold flat baseline (idle / leads-off)
float phaseSamp = 0.0f;          // position within the current beat (samples)
unsigned long lastSampleUs = 0;
unsigned long genStartMs   = 0;  // for the 50 Hz / wander phase

// ============================================================================
//  TEST PLAN
// ============================================================================
enum CheckType { CHK_BPM, CHK_AMP, CHK_NOISE, CHK_LEADSOFF };
struct PlanStep { float bpm; float amp; CheckType chk; };

PlanStep plan[] = {
  { 30.0f, 1.0f, CHK_BPM },       // 1
  { 60.0f, 1.0f, CHK_BPM },       // 2
  {120.0f, 1.0f, CHK_BPM },       // 3
  {180.0f, 1.0f, CHK_BPM },       // 4
  {240.0f, 1.0f, CHK_BPM },       // 5
  { 60.0f, 0.5f, CHK_AMP },       // 6
  { 60.0f, 1.0f, CHK_AMP },       // 7
  { 60.0f, 2.0f, CHK_AMP },       // 8
  { 60.0f, 1.0f, CHK_NOISE },     // 9  (mains hum + baseline wander)
  { 60.0f, 0.0f, CHK_LEADSOFF }   // 10 (open input / leads-off)
};
const int NUM_STEPS = sizeof(plan) / sizeof(plan[0]);

// per-step results
struct StepResult { float gb, ga, mb, ma; int verdict; };  // verdict: -1 na,0 fail,1 pass
StepResult results[16];

// ============================================================================
//  CALIBRATION SWEEP  (BPM points at 1.0 mV)
// ============================================================================
const float calRefPts[] = { 30, 65, 100, 135, 170, 205, 240 };
const int   NUM_CAL = sizeof(calRefPts) / sizeof(calRefPts[0]);
float calMeas[NUM_CAL];
int   calVerdict[NUM_CAL];
float calGain = 1.0f, calOffset = 0.0f, calMaxErr = 0.0f;

// ============================================================================
//  STATE MACHINE
// ============================================================================
enum { S_IDLE = 0, S_TEST = 10, S_CAL = 20, S_DONE = 30, S_FAULT = 90 };
int  State = S_IDLE;
bool modeCal = false;         // false = TEST mode, true = CALIBRATE mode
int  stepIdx = 0;
unsigned long stepStartMs = 0;
int  passCount = 0, failCount = 0;
int  overall = -1;            // -1 na, 0 fail, 1 pass

const unsigned long STEP_MS      = 7000;   // time per test step
const unsigned long CAL_STEP_MS  = 7000;   // time per calibration point
const unsigned long DUT_TIMEOUT  = 4000;   // no report for this long -> FAULT

// ============================================================================
//  DUT REPORT (parsed from UART2)
// ============================================================================
int   measBPM  = 0;
float measAMP  = 0.0f;
int   leadsOff = 0;
unsigned long lastReportMs = 0;
char  rxBuf[64];
int   rxLen = 0;

// ============================================================================
//  BUTTONS (debounced)
// ============================================================================
bool lastRun = HIGH, lastMode = HIGH;
unsigned long lastRunMs = 0, lastModeMs = 0;
const unsigned long DEBOUNCE_MS = 40;

// ============================================================================
//  MISC
// ============================================================================
unsigned long bootMs = 0;
unsigned long lastOledMs = 0;

// forward declarations
void buildEcgTable();
void generateTick();
void loadStepSetpoint(int i);
#if TEST_MODE
void runBenchTests();
#endif
void readDUT();
void parseReport(const char* s);
void handleButtons();
void startRun();
void stopRun();
void runTestSequencer();
void runCalSequencer();
void evaluateTestStep(int i);
void finishTest();
void finishCal();
void computeCalFit();
void updateLeds();
void updateOled();
void beep(int freq, int ms);
String buildJson();
void handleRoot();
void handleData();
void handleStart();
void handleStop();
void handleMode();

// =============================================================================
//  SETUP
// =============================================================================
void setup() {
  Serial.begin(115200);
  Serial2.begin(115200, SERIAL_8N1, PIN_RX2, PIN_TX2);

  pinMode(PIN_BTN_RUN,  INPUT_PULLUP);
  pinMode(PIN_BTN_MODE, INPUT_PULLUP);
  pinMode(PIN_LED_PASS, OUTPUT);
  pinMode(PIN_LED_FAIL, OUTPUT);
  pinMode(PIN_BUZZER,   OUTPUT);
  digitalWrite(PIN_LED_PASS, LOW);
  digitalWrite(PIN_LED_FAIL, LOW);
  digitalWrite(PIN_BUZZER,   LOW);

  dacWrite(PIN_DAC, DAC_BASE);          // hold flat baseline at start

  buildEcgTable();

  // ---- OLED ----
  Wire.begin(PIN_SDA, PIN_SCL);
  oledOk = oled.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR);
  if (oledOk) {
    oled.clearDisplay();
    oled.setTextColor(SSD1306_WHITE);
    oled.setTextSize(1);
    oled.setCursor(0, 0);
    oled.println(F("Med Device Tester"));
    oled.println(F("booting..."));
    oled.display();
  } else {
    Serial.println(F("[OLED] not found at 0x3C (continuing without display)"));
  }

#if TEST_MODE
  runBenchTests();     // never returns to normal loop in test mode
#endif

  // ---- Wi-Fi SoftAP + web routes ----
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);
  IPAddress ip = WiFi.softAPIP();
  Serial.print(F("[WiFi] SoftAP '")); Serial.print(AP_SSID);
  Serial.print(F("' at http://")); Serial.println(ip);

  server.on("/",        handleRoot);
  server.on("/data",    handleData);
  server.on("/start",   handleStart);
  server.on("/stop",    handleStop);
  server.on("/mode",    handleMode);
  server.begin();
  Serial.println(F("[WEB ] server on port 80"));

  bootMs = millis();
  lastSampleUs = micros();
  for (int i = 0; i < NUM_STEPS; i++) results[i].verdict = -1;

  Serial.println(F("[BOOT] ready. IDLE. press Start to run."));
}

// =============================================================================
//  MAIN LOOP  (cooperative scheduler)
// =============================================================================
void loop() {
  // 1) keep the DAC waveform going at a steady 250 Hz
  unsigned long nowUs = micros();
  if ((long)(nowUs - lastSampleUs) >= SAMPLE_US) {
    lastSampleUs += SAMPLE_US;
    generateTick();
  }

  // 2) service everything else
  server.handleClient();
