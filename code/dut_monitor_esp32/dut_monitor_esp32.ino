/* =============================================================================
   DUT MONITOR  -  ESP32 #2  (the "ECG patient monitor under test")
   Smart Medical Device Tester & Calibration System
   -----------------------------------------------------------------------------
   WHAT THIS BOARD IS
     This is the DEVICE UNDER TEST (DUT). It stands in for a real patient-monitor
     ECG channel. It is built from an AD8232 single-lead ECG analog front-end
     feeding this ESP32. It does exactly what a real monitor's ECG channel does:
       1) takes a tiny (millivolt) ECG signal at its input,
       2) amplifies and filters it (that is the AD8232's job),
       3) samples the result, finds the R-peaks (the tall spikes of each beat),
       4) works out heart rate in BPM and the signal amplitude,
       5) reports those numbers out over a serial link.
     The Tester board (ESP32 #1) generates a known ECG into this board's input and
     reads these reported numbers back to decide PASS/FAIL and to calibrate.

     HONEST SCOPE NOTE: this is a self-built stand-in for a certified monitor so the
     whole test loop closes on a desk. On real hardware the same Tester would feed a
     real monitor and read its measured values back through the monitor's data port.
     This is a student proof-of-concept, NOT a medical device, NOT for clinical use.

   HOW R-PEAK DETECTION WORKS (one paragraph)
     We sample the AD8232 output at a steady 250 Hz. Each sample we subtract a slow
     running average to remove the slow baseline drift (a simple high-pass). We keep
     a decaying estimate of the recent peak height and set a threshold at a fraction
     of it (an adaptive threshold, so it follows big or small signals). When the
     cleaned signal crosses that threshold AND enough time has passed since the last
     beat (a refractory period so one R-wave is not counted twice), we mark an R-peak.
     The time between R-peaks gives instantaneous BPM; we average a few of them for a
     steady reading. Amplitude is the R-peak height mapped back to millivolts.

   PIN MAP  (ESP32 #2 - DUT)
     GPIO34 (ADC1)  <- AD8232 OUTPUT      input-only ADC pin, reads the ECG
     GPIO25         <- AD8232 LO+         leads-off detect (optional)
     GPIO26         <- AD8232 LO-         leads-off detect (optional)
     GPIO17 (TX2)   -> Tester GPIO16 (RX) UART2, report BPM/amplitude
     GPIO16 (RX2)   <- Tester GPIO17 (TX) UART2, receive commands (optional)
     3V3            -> AD8232 3.3V
     GND            -> AD8232 GND + COMMON GROUND with the Tester (essential!)

   UART REPORT FORMAT  (must match the Tester's parser exactly)
     One line, about once per second, ending in newline:
         BPM=<integer> AMP=<float 2dp> LO=<0 or 1>
     Example:
         BPM=59 AMP=0.98 LO=0
     LO=1 means leads-off / no lead contact detected.

   LIBRARIES
     None beyond the ESP32 core. Uses analogRead() and Serial2 only.

   BOARD SETTINGS (Arduino IDE)
     Board: "ESP32 Dev Module"    Upload speed: 115200    USB serial: 115200
   ============================================================================= */

// ---- set to 1 to print raw ADC values for bring-up / calibration, 0 for normal -
#define TEST_MODE 0

// -------------------------- pin assignments ---------------------------------
const int PIN_ECG   = 34;   // AD8232 output -> ADC1 (input only)
const int PIN_LO_P  = 25;   // AD8232 LO+  (leads-off positive)
const int PIN_LO_N  = 26;   // AD8232 LO-  (leads-off negative)
const int PIN_TX2   = 17;   // UART2 TX -> Tester RX (GPIO16)
const int PIN_RX2   = 16;   // UART2 RX <- Tester TX (GPIO17)

// -------------------------- sampling --------------------------------------
const int   SAMPLE_HZ      = 250;                 // ECG sample rate
const long  SAMPLE_US      = 1000000L / SAMPLE_HZ; // 4000 us between samples

// -------------------------- amplitude -> mV conversion --------------------
// The AD8232 has a fixed gain of ~1100. An R-wave of A mV at the input becomes
// A * 1100 mV at the AD8232 output, which the 12-bit ADC reads as counts.
//   input_mV = adc_counts_of_R_height * (3300 mV / 4095) / AFE_GAIN
// AFE_GAIN is here as a tunable constant. In TEST_MODE, watch the raw R height
// and adjust AFE_GAIN so a known 1.0 mV input reads back as ~1.00.
const float ADC_MV_FS  = 3300.0f;   // full-scale ADC input in mV (~3.3 V)
const float ADC_COUNTS = 4095.0f;   // 12-bit ADC
const float AFE_GAIN   = 1100.0f;   // AD8232 typical gain (datasheet-order value)

// -------------------------- detection tuning ------------------------------
const float BASELINE_A     = 0.02f;   // baseline follower speed (smaller = slower)
const float ENV_DECAY       = 0.995f;  // peak-envelope decay per sample
const float THRESH_FRAC     = 0.45f;   // R threshold = this fraction of envelope
const int   REFRACTORY_MS   = 200;     // min gap between beats (=> max 300 BPM)
const float MIN_ENV_COUNTS  = 40.0f;   // ignore tiny noise below this envelope
const int   RR_AVG_N        = 5;       // how many RR intervals to average

// -------------------------- detector state --------------------------------
float baseline   = 2048.0f;   // slow average of raw ADC (starts mid-scale)
float envelope   = 200.0f;    // decaying estimate of recent peak height
float lastSignal = 0.0f;      // previous cleaned sample (for rising-edge check)
bool  aboveThr   = false;     // are we currently above threshold?

unsigned long lastBeatMs = 0;         // time of last accepted R-peak
long   rrBuf[RR_AVG_N];               // ring buffer of recent RR intervals (ms)
int    rrCount = 0;                   // how many valid RR values so far
int    rrHead  = 0;                   // ring buffer write index

float  rPeakSum   = 0.0f;             // sum of recent R-peak heights (counts)
int    rPeakN     = 0;                // how many R-peaks summed
float  lastRHeight = 0.0f;            // last accepted R-peak height (counts)

int    reportedBPM = 0;               // latest computed BPM
float  reportedAMP = 0.0f;            // latest computed amplitude (mV)
int    reportedLO  = 0;               // leads-off flag

unsigned long lastSampleUs = 0;
unsigned long lastReportMs = 0;
unsigned long lastBeatSeenMs = 0;     // for "no signal -> BPM 0" timeout

// =============================================================================
//  SETUP
// =============================================================================
void setup() {
  Serial.begin(115200);                                  // USB debug
  Serial2.begin(115200, SERIAL_8N1, PIN_RX2, PIN_TX2);   // link to Tester

  // Pull-DOWN so that if you leave LO+/LO- unwired (they are optional) the pins
  // read LOW = "leads connected" instead of floating and reporting random
  // leads-off alarms. The AD8232 drives these pins hard, so it still overrides
  // the internal pull-down when it really does detect an open lead.
  pinMode(PIN_LO_P, INPUT_PULLDOWN);
  pinMode(PIN_LO_N, INPUT_PULLDOWN);
  analogReadResolution(12);                 // 0..4095
  analogSetPinAttenuation(PIN_ECG, ADC_11db); // full 0..~3.3 V range

  // seed the baseline with a first reading so it settles fast
  baseline = analogRead(PIN_ECG);

  Serial.println();
  Serial.println(F("=============================================="));
  Serial.println(F("  DUT ECG MONITOR (ESP32 #2)  -  stand-in DUT"));
  Serial.println(F("  Sampling AD8232 on GPIO34 at 250 Hz"));
  Serial.println(F("  Reporting: BPM=.. AMP=.. LO=..  on UART2"));
#if TEST_MODE
  Serial.println(F("  TEST_MODE = 1  -> printing raw ADC values"));
#endif
  Serial.println(F("=============================================="));

  for (int i = 0; i < RR_AVG_N; i++) rrBuf[i] = 0;
  lastSampleUs   = micros();
  lastBeatSeenMs = millis();
}

// =============================================================================
//  MAIN LOOP  - steady 250 Hz sampler + once-a-second report
// =============================================================================
void loop() {
  unsigned long nowUs = micros();

  // ---- take one sample every SAMPLE_US (steady scheduler) ----
  if ((long)(nowUs - lastSampleUs) >= SAMPLE_US) {
    lastSampleUs += SAMPLE_US;          // advance by a fixed step (keeps cadence)
    processSample();
  }

  // ---- send a report about once per second ----
  unsigned long nowMs = millis();
  if (nowMs - lastReportMs >= 1000) {
    lastReportMs = nowMs;
    sendReport(nowMs);
  }
}

// =============================================================================
//  processSample()  - read one ADC sample and run the R-peak detector
// =============================================================================
void processSample() {
  int raw = analogRead(PIN_ECG);

#if TEST_MODE
  // In test mode we just stream raw values so you can see the waveform in the
  // Serial Plotter and tune AFE_GAIN / thresholds. No detection output.
  Serial.println(raw);
  return;
#endif

  // ---- 1) baseline removal (simple high-pass) ----
  // baseline slowly follows the DC level; signal is what is left after removing it
  baseline += BASELINE_A * (raw - baseline);
  float signal = raw - baseline;         // now centred near zero
  float mag = fabs(signal);

  // ---- 2) adaptive envelope (tracks recent peak size) ----
  envelope *= ENV_DECAY;                  // slowly decay
  if (mag > envelope) envelope = mag;     // jump up to new peaks
  if (envelope < MIN_ENV_COUNTS) envelope = MIN_ENV_COUNTS;

  float threshold = THRESH_FRAC * envelope;

  // ---- 3) rising-edge threshold crossing + refractory ----
  unsigned long nowMs = millis();
  bool nowAbove = (signal > threshold);

  if (nowAbove && !aboveThr) {
    // just crossed upward -> candidate R-peak
    if (nowMs - lastBeatMs >= (unsigned long)REFRACTORY_MS) {
      registerBeat(nowMs, signal);
    }
  }
  aboveThr = nowAbove;
  lastSignal = signal;

  // ---- 4) track the true peak height during the beat (for amplitude) ----
  // While above threshold, remember the tallest signal value of this beat.
  if (nowAbove && signal > lastRHeight) lastRHeight = signal;
  if (!nowAbove && lastRHeight > 0.0f) {
    // beat's rising portion finished: fold this peak height into the average
    rPeakSum += lastRHeight;
    rPeakN++;
    if (rPeakN > 8) { rPeakSum *= 0.5f; rPeakN = 4; }  // keep it a rolling mean
    lastRHeight = 0.0f;
  }

  // ---- 5) if no beat for a while, report nothing (no signal / leads off) ----
  // We must clear the peak accumulator too, otherwise updateAmplitude() below
  // would immediately re-report the last amplitude we saw and the open-input
  // test step would never show 0.00 mV.
  if (nowMs - lastBeatSeenMs > 3000) {
    reportedBPM = 0;
    reportedAMP = 0.0f;
    rrCount     = 0;           // forget stale intervals
    rPeakSum    = 0.0f;        // forget stale peak heights
    rPeakN      = 0;
  }

  updateAmplitude();
}

// =============================================================================
//  registerBeat()  - called when a valid R-peak is accepted
