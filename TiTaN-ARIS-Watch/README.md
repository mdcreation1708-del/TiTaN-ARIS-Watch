# TiTaN ARIS Watch — GitHub Starter

**Brand:** TiTaN Lab of Innovation  
**Status:** source starter only — not yet compiled or hardware-tested.

This repository contains:
- `android/` — Android companion app source (Java + Gradle): BLE scan/connect, notification access listener, and phone speech-to-text forwarding.
- `firmware/` — ESP32 DevKit V1 watch firmware for the GC9A01 display and three physical buttons, with a BLE command service.

## What this version can and cannot do

**Included in source:** BLE command/status exchange, forwarding Android notifications to the watch when notification access is enabled and the app's BLE connection is active, and speech-to-text using the Android phone's available recognizer.

**Not included / not verified:** an AI/LLM response engine, text-to-speech assistant conversation, guaranteed background notification delivery, battery percentage/charging sensing, Wi-Fi/NTP time sync, or verified compile/build results. This is not a finished release. Treat all behavior as unverified until built and tested on your phone and board.

## Start here

1. Download this repository ZIP or clone it.
2. Open the **`android` folder** in Android Studio (not the repository root) and let Gradle sync.
3. If Android Studio reports missing SDK/platform packages, install Android SDK Platform 35 and a compatible Android SDK Build-Tools package in SDK Manager. Do not install a preview SDK just to fix errors.
4. Build the app. If there are errors, save the complete Build output and fix those before trying to install.
5. Open `firmware/TiTaN_ARIS_Watch_OS_V2_Lite.ino` in Arduino IDE. Install Adafruit GFX Library and Adafruit GC9A01A, select the ESP32 Dev Module board, compile, and record the exact errors or flash/RAM size before uploading.
6. Back up any known-working watch sketch before changing firmware.
7. Test BLE with the watch powered on and advertising as `TiTaN ARIS Watch`. Enable the app's notification access only if you want notification forwarding. Speech recognition requires a working Android speech-recognition provider and microphone permission.

## Hardware assumptions

- ESP32 DevKit V1 / classic ESP32 board (not ESP32-C3 for this first test)
- GC9A01 240x240 SPI display: SCLK 18, MOSI 23, CS 5, DC 16, RST 17
- Three buttons: UP 25, OK 26, DOWN 27, buttons to GND using `INPUT_PULLUP`
- Do not connect a Li-Po battery directly to a GPIO or the 3V3 rail. This firmware does not measure battery voltage or charging state.

## BLE protocol

Service UUID: `9f8c0001-7b5a-4d2e-9a11-3c6a7b8c0001`  
Command characteristic (phone writes): `9f8c0002-7b5a-4d2e-9a11-3c6a7b8c0001`  
Event characteristic (watch notifies): `9f8c0003-7b5a-4d2e-9a11-3c6a7b8c0001`

Commands supported by the firmware source: `PING`, `STATUS`, `HOME`, `MENU`, `CLEAR`, `NOTE|title|body`, `VOICE|recognized text`.

## GitHub upload (beginner route)

1. Sign in at GitHub and create a **new repository** named `TiTaN-ARIS-Watch`.
2. Keep it **Private** initially if you prefer not to publish your source.
3. On the new repository page choose **Add file → Upload files**.
4. Extract this ZIP on your computer. Open the extracted `titan_github_repo` folder, select its contents (`README.md`, `.gitignore`, `android`, `firmware`) and drag them into GitHub's upload page. Upload the contents, not the outer folder itself.
5. Enter commit message `Add TiTaN ARIS Watch starter source` and click **Commit changes**.
6. Refresh the repository page and confirm `android/`, `firmware/`, `README.md`, and `.gitignore` appear.

GitHub stores the source; it does not itself build/install the Android app. Never upload API keys, passwords, signing keys, or personal tokens.
