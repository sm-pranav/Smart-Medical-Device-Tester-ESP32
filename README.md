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
