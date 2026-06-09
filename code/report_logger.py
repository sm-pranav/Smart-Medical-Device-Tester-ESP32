# =============================================================================
#  SMART MEDICAL DEVICE TESTER - Report Logger
#  (produces the "System Validation Data" / calibration-record deliverable)
# -----------------------------------------------------------------------------
#  WHAT IT DOES
#    While your PC is connected to the Tester's Wi-Fi hotspot ("MedTester"), this
#    script polls the Tester's  /data  endpoint once a second and appends every
#    sample to a CSV file. Run it across a full automated test plan and a
#    calibration sweep and you get a timestamped record of:
#      - what was generated (BPM / amplitude) at each step,
#      - what the DUT measured,
#      - the PASS/FAIL verdict,
#      - and the calibration fit (gain / offset / max error).
#    That CSV is the validation evidence a test/V&V report needs. Open it in
#    Excel / Google Sheets to build a results table or plot the calibration data.
#
#  HOW TO RUN
#    1) Power on the Tester. On your PC, connect to Wi-Fi  "MedTester"
#       (password medtest123). The Tester lives at  http://192.168.4.1
#    2) Optional but nicer:   pip install requests
#       If you skip it, the script falls back to Python's built-in urllib, so it
#       works with nothing installed beyond standard Python 3.
#    3) Run it:
#           python report_logger.py
#       or with options:
#           python report_logger.py --host 192.168.4.1 --interval 1.0 --out run1.csv
#    4) Press Start on the bench (or the dashboard) and let a test plan and a
#       calibration sweep run. Watch the rows appear. Ctrl+C to stop; the CSV is
#       saved as you go.
#
#  NOTE
#    Monitoring/validation tool only - it never changes the test. Part of a
#    STUDENT RESEARCH PROTOTYPE, not a medical device, not a certified analyzer.
# =============================================================================

import argparse
import csv
import json
import os
import time
from datetime import datetime

# ---- Try the friendly "requests" library; fall back to stdlib urllib. --------
try:
    import requests  # type: ignore
    _HAVE_REQUESTS = True
except ImportError:
    _HAVE_REQUESTS = False
    import urllib.request
    import urllib.error


# The columns we write to the CSV. Every key after "pc_time" must match a JSON
# key the Tester returns from /data (see the data contract in the README, and
# buildJson() in tester_esp32.ino).
CSV_FIELDS = [
    "pc_time",       # PC wall-clock timestamp (added by this script)
    "uptime_s",      # Tester uptime in seconds
    "state",         # IDLE / TESTING / CALIBRATE / COMPLETE / FAULT
    "mode",          # TEST or CAL
    "step",          # current step / calibration point (1-based)
    "total_steps",   # steps in the active plan
    "gen_bpm",       # generated heart rate (BPM)
    "gen_amp",       # generated R-wave amplitude (mV)
    "meas_bpm",      # DUT-reported heart rate (BPM)
    "meas_amp",      # DUT-reported amplitude (mV)
    "verdict",       # last step verdict: PASS / FAIL / --
    "overall",       # PASS / FAIL / RUNNING / FAULT / --
    "pass_count",    # steps passed so far
    "fail_count",    # steps failed so far
    "leads_off",     # 0 = leads connected, 1 = leads-off detected
    "cal_gain",      # calibration fit: measured = gain*reference + offset
    "cal_offset",    # calibration offset (BPM)
    "cal_max_err",   # calibration max absolute error (BPM)
]


def fetch_data(url, timeout=4.0):
    """Return the Tester's /data as a dict, or None if it could not be read."""
    if _HAVE_REQUESTS:
        try:
            resp = requests.get(url, timeout=timeout)
            resp.raise_for_status()
            return resp.json()
        except Exception as exc:
            print(f"[warn] request failed: {exc}")
            return None
    else:
        try:
            with urllib.request.urlopen(url, timeout=timeout) as resp:
