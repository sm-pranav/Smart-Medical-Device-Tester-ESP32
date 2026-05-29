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
  readDUT();
  handleButtons();

  // 3) sequencers
  if (State == S_TEST) runTestSequencer();
  else if (State == S_CAL) runCalSequencer();

  // 4) FAULT watch: during a run, the DUT must keep reporting
  if ((State == S_TEST || State == S_CAL) &&
      (millis() - lastReportMs > DUT_TIMEOUT)) {
    State = S_FAULT;
    outputOff = true;
    Serial.println(F("[FAULT] no DUT report (check UART cross-wiring & GND)"));
    beep(400, 300);
  }

  // 5) OLED + LEDs at a calm rate (keeps the waveform smooth)
  if (millis() - lastOledMs > 400) {
    lastOledMs = millis();
    updateOled();
    updateLeds();
  }
}

// =============================================================================
//  buildEcgTable()  - make one normalized P-QRS-T beat from Gaussian bumps
// =============================================================================
float gaussBump(float t, float mu, float sigma) {
  float x = (t - mu) / sigma;
  return expf(-0.5f * x * x);
}
void buildEcgTable() {
  // Amplitudes are fractions of the R-wave (R = 1.0).
  // t runs 0..1 across the ~400 ms COMPLEX (not the whole beat), so the times
  // below are real durations: P at ~48 ms, R at ~188 ms, T at ~312 ms, which
  // gives a PR interval near 140 ms and a QRS around 80 ms - realistic enough
  // that a rate/amplitude detector treats it exactly like a real beat.
  const float P =  0.12f, Q = -0.10f, R = 1.00f, S = -0.16f, T = 0.30f;
  for (int i = 0; i < TABLE_N; i++) {
    float t = (float)i / TABLE_N;              // 0..1 across the complex
    float v = P * gaussBump(t, 0.12f, 0.045f)  // P wave  (broad, low)
            + Q * gaussBump(t, 0.40f, 0.018f)  // Q dip
            + R * gaussBump(t, 0.47f, 0.022f)  // R spike (the tall one)
            + S * gaussBump(t, 0.55f, 0.020f)  // S dip
            + T * gaussBump(t, 0.78f, 0.060f); // T wave  (broad, medium)
    ecgTable[i] = v;
  }
}

// =============================================================================
//  generateTick()  - output one DAC sample of the synthetic ECG
// =============================================================================
void generateTick() {
  if (outputOff || State == S_IDLE || State == S_DONE || State == S_FAULT) {
    dacWrite(PIN_DAC, DAC_BASE);      // flat line
    return;
  }

  // how long one whole beat lasts, in samples
  float samplesPerBeat = (SAMPLE_HZ * 60.0f) / genBPM;

  // The complex keeps its fixed ~400 ms length; only at high rates (above about
  // 128 BPM) is it compressed so it still fits inside one beat.
  float complexSamples = COMPLEX_MAX_FRAC * samplesPerBeat;
  if (complexSamples > COMPLEX_SAMP_MAX) complexSamples = COMPLEX_SAMP_MAX;

  // advance the beat phase and wrap (while, so a big BPM jump settles at once)
  phaseSamp += 1.0f;
  while (phaseSamp >= samplesPerBeat) phaseSamp -= samplesPerBeat;

  // inside the complex -> play the table; after it -> flat resting baseline
  float level = 0.0f;
  if (phaseSamp < complexSamples) {
    int idx = (int)((phaseSamp / complexSamples) * TABLE_N);
    if (idx < 0) idx = 0;
    if (idx >= TABLE_N) idx = TABLE_N - 1;
    level = ecgTable[idx];
  }

  float ampCounts = ampToCounts(genAMP);      // R-wave height in DAC counts
  float val = DAC_BASE + level * ampCounts;

  // optional noise injection for the robustness step
  if (noiseOn) {
    float tsec = (millis() - genStartMs) / 1000.0f;
    val += 12.0f * sinf(2.0f * PI * 50.0f * tsec);   // 50 Hz mains hum
    val += 18.0f * sinf(2.0f * PI * 0.3f  * tsec);   // slow baseline wander
  }

  if (val < 0)   val = 0;
  if (val > 255) val = 255;
  dacWrite(PIN_DAC, (int)val);
}

// =============================================================================
//  readDUT()  - collect UART lines and parse "BPM=.. AMP=.. LO=.."
// =============================================================================
void readDUT() {
  while (Serial2.available()) {
    char c = Serial2.read();
    if (c == '\n' || c == '\r') {
      if (rxLen > 0) { rxBuf[rxLen] = 0; parseReport(rxBuf); rxLen = 0; }
    } else if (rxLen < (int)sizeof(rxBuf) - 1) {
      rxBuf[rxLen++] = c;
    }
  }
}
void parseReport(const char* s) {
  int b = 0, l = 0; float a = 0.0f;
  if (sscanf(s, "BPM=%d AMP=%f LO=%d", &b, &a, &l) == 3) {
    measBPM = b; measAMP = a; leadsOff = l;
    lastReportMs = millis();
  }
}

// =============================================================================
//  handleButtons()  - debounced Start/Stop and Mode
// =============================================================================
void handleButtons() {
  bool run  = digitalRead(PIN_BTN_RUN);
  bool mode = digitalRead(PIN_BTN_MODE);
  unsigned long now = millis();

  // Start/Stop (active LOW)
  if (run != lastRun && now - lastRunMs > DEBOUNCE_MS) {
    lastRunMs = now;
    if (run == LOW) {   // pressed
      if (State == S_IDLE || State == S_DONE || State == S_FAULT) startRun();
      else stopRun();
    }
  }
  lastRun = run;

  // Mode toggle (only when not running)
  if (mode != lastMode && now - lastModeMs > DEBOUNCE_MS) {
    lastModeMs = now;
    if (mode == LOW) {
      if (State == S_IDLE || State == S_DONE || State == S_FAULT) {
        modeCal = !modeCal;
        Serial.print(F("[MODE] ")); Serial.println(modeCal ? "CALIBRATE" : "TEST");
      }
    }
  }
  lastMode = mode;
}

// =============================================================================
//  startRun() / stopRun()
// =============================================================================
void startRun() {
  passCount = 0; failCount = 0; overall = -1;
  stepIdx = 0; stepStartMs = millis();
  lastReportMs = millis();     // give the DUT a fresh timeout window
  noiseOn = false; outputOff = false;
  genStartMs = millis();
  for (int i = 0; i < NUM_STEPS; i++) results[i].verdict = -1;
  for (int i = 0; i < NUM_CAL; i++) { calMeas[i] = 0; calVerdict[i] = -1; }
  calGain = 1.0f; calOffset = 0.0f; calMaxErr = 0.0f;

  if (modeCal) { State = S_CAL;  Serial.println(F("[RUN] calibration sweep")); }
  else         { State = S_TEST; Serial.println(F("[RUN] automated test plan")); }

  // load first setpoint
  if (modeCal) { genBPM = calRefPts[0]; genAMP = 1.0f; phaseSamp = 0.0f; }
  else         { loadStepSetpoint(0); }
}

void stopRun() {
  State = S_IDLE;
  outputOff = true; noiseOn = false;
  Serial.println(F("[RUN] stopped -> IDLE"));
}

// =============================================================================
//  loadStepSetpoint()  - apply the generator settings for a test step
// =============================================================================
void loadStepSetpoint(int i) {
  genBPM = plan[i].bpm;
  genAMP = plan[i].amp;
  noiseOn = (plan[i].chk == CHK_NOISE);
  outputOff = (plan[i].chk == CHK_LEADSOFF);  // open input for leads-off step
  genStartMs = millis();
  phaseSamp  = 0.0f;    // start each step on a clean beat boundary
}

// =============================================================================
//  runTestSequencer()
// =============================================================================
void runTestSequencer() {
  if (millis() - stepStartMs >= STEP_MS) {
    evaluateTestStep(stepIdx);
    stepIdx++;
    if (stepIdx >= NUM_STEPS) { finishTest(); return; }
    stepStartMs = millis();
    loadStepSetpoint(stepIdx);
  }
}

void evaluateTestStep(int i) {
  StepResult r;
  r.gb = plan[i].bpm; r.ga = plan[i].amp;
  r.mb = measBPM;     r.ma = measAMP;
  bool pass = false;

  switch (plan[i].chk) {
    case CHK_BPM:
    case CHK_NOISE: {
      float tol = plan[i].bpm * 0.05f;      // 5%
      if (tol < 5.0f) tol = 5.0f;           // or +/-5 BPM, whichever is larger
      pass = fabs((float)measBPM - plan[i].bpm) <= tol;
      break;
    }
    case CHK_AMP: {
      // +/-10%, but never tighter than +/-0.10 mV. The floor matters at the
      // 0.5 mV step: a bare 10% band there is only +/-0.05 mV, which is finer
      // than this signal chain (8-bit DAC, breadboard resistors) can resolve,
      // so it would fail a DUT that is actually fine.
      float tol = plan[i].amp * 0.10f;
      if (tol < 0.10f) tol = 0.10f;
      pass = fabs(measAMP - plan[i].amp) <= tol;
      break;
    }
    case CHK_LEADSOFF: {
      // open input: pass if the DUT flags leads-off OR reports no rate
      pass = (leadsOff == 1) || (measBPM < 20);
      break;
    }
  }

  r.verdict = pass ? 1 : 0;
  results[i] = r;
  if (pass) passCount++; else failCount++;

  Serial.printf("[STEP %d] gen %.0f/%.1fmV  meas %d/%.2fmV  -> %s\n",
                i + 1, r.gb, r.ga, (int)r.mb, r.ma, pass ? "PASS" : "FAIL");
}

void finishTest() {
  overall = (failCount == 0) ? 1 : 0;
  State = S_DONE;
  outputOff = true; noiseOn = false;
  beep(overall ? 1500 : 400, overall ? 150 : 400);
  Serial.printf("[DONE] TEST complete. pass=%d fail=%d overall=%s\n",
                passCount, failCount, overall ? "PASS" : "FAIL");
}

// =============================================================================
//  runCalSequencer()
// =============================================================================
void runCalSequencer() {
  if (millis() - stepStartMs >= CAL_STEP_MS) {
    calMeas[stepIdx] = measBPM;         // record measured BPM at this ref
    // per-point pass/fail vs BPM tolerance
    float tol = calRefPts[stepIdx] * 0.05f; if (tol < 5.0f) tol = 5.0f;
    bool pass = fabs((float)measBPM - calRefPts[stepIdx]) <= tol;
    calVerdict[stepIdx] = pass ? 1 : 0;
    if (pass) passCount++; else failCount++;
    Serial.printf("[CAL %d] ref %.0f  meas %d  -> %s\n",
                  stepIdx + 1, calRefPts[stepIdx], measBPM, pass ? "PASS" : "FAIL");

    stepIdx++;
    if (stepIdx >= NUM_CAL) { finishCal(); return; }
    stepStartMs = millis();
    genBPM = calRefPts[stepIdx];
    genAMP = 1.0f;
    phaseSamp = 0.0f;      // clean beat boundary for the new reference point
  }
}

void finishCal() {
  computeCalFit();
  overall = (failCount == 0) ? 1 : 0;
  State = S_DONE;
  outputOff = true;
  beep(1500, 150);
  Serial.printf("[DONE] CAL complete. gain=%.3f offset=%.2f maxErr=%.2f\n",
                calGain, calOffset, calMaxErr);
}

// least-squares straight-line fit: measured = gain * reference + offset
void computeCalFit() {
  float sx = 0, sy = 0, sxx = 0, sxy = 0;
  int n = NUM_CAL;
  for (int i = 0; i < n; i++) {
    float x = calRefPts[i], y = calMeas[i];
    sx += x; sy += y; sxx += x * x; sxy += x * y;
  }
  float denom = (n * sxx - sx * sx);
  if (fabs(denom) < 1e-6) { calGain = 1.0f; calOffset = 0.0f; }
  else {
    calGain   = (n * sxy - sx * sy) / denom;
    calOffset = (sy - calGain * sx) / n;
  }
  // max absolute error between measured and reference
  calMaxErr = 0.0f;
  for (int i = 0; i < n; i++) {
    float e = fabs(calMeas[i] - calRefPts[i]);
    if (e > calMaxErr) calMaxErr = e;
  }
}

// =============================================================================
//  LEDs
// =============================================================================
void updateLeds() {
  bool green = false, red = false;
  if (State == S_FAULT) { red = true; }
  else if (State == S_DONE) { green = (overall == 1); red = (overall == 0); }
  else if (State == S_TEST || State == S_CAL) {
    // show the most recent step verdict live
    int last = stepIdx - 1;
    if (State == S_TEST && last >= 0 && results[last].verdict >= 0) {
      green = (results[last].verdict == 1);
      red   = (results[last].verdict == 0);
    } else if (State == S_CAL && last >= 0 && calVerdict[last] >= 0) {
      green = (calVerdict[last] == 1);
      red   = (calVerdict[last] == 0);
    }
  }
  digitalWrite(PIN_LED_PASS, green ? HIGH : LOW);
  digitalWrite(PIN_LED_FAIL, red   ? HIGH : LOW);
}

// =============================================================================
//  OLED status screen
// =============================================================================
const char* stateName() {
  switch (State) {
    case S_IDLE:  return "IDLE";
    case S_TEST:  return "TESTING";
