# ESP Wartezeiten E-Paper Display (ESP32-S3)

Created as a Christmas gift.

Battery-friendly ESP32 app that fetches Europa-Park ride wait and opening times, renders them on a 1.54" e-paper, and deep-sleeps between updates. Wake cycles are driven by an external RTC alarm; the device adapts its wake interval depending on park/coaster status and performs a daily time refresh to handle drift and DST.

> Built with the help of AI.

## Features
- Fetches waiting times and opening hours via HTTPS APIs (cert bundle provided by ESP-IDF).
- Renders status and wait time to a 1.54" e-paper (LVGL, 1 bpp).
- External RTC (PCF85263A) sets alarms for short polling (default 1 minute) during open hours, and sleeps longer when park/coaster are closed (refresh wake at configurable 04:00).
- Wakes on RTC alert pin (GPIO7), resyncs time via SNTP on refresh wakes, and writes time back to RTC.
- Loads Wi‑Fi credentials from `config.txt` on SD card (falls back to menuconfig credentials). SD power is switched on/off via GPIO2; card detect is active low on GPIO47.

## Hardware Notes (ESP32-S3)
- **MCU**: ESP32-S3.
- **E-Paper**: 1.54", LVGL (1 bpp), display power enable on GPIO1.
- **RTC**: PCF85263A; alert/wake on GPIO7 (active low, ext0).
- **SD Card (SDMMC 4-bit)**:
  - CLK: IO39, CMD: IO40, D0: IO38, D1: IO48, D2: IO42, D3: IO41
  - Card detect: IO47 (active low)
  - SD power enable: IO2 (switched on during read, off afterward)
- **Logos/Branding**: Displays Voltron and Europa-Park logos; default coaster target is Voltron Nevera.
- **Wake Scheduling**: Default 0h 1min during open hours; daily refresh at 04:00 (configurable in `main.c`).

## SD Card Wi‑Fi Config
Place `config.txt` on the SD card root:
```
WIFI_SSID=Your Network Name
WIFI_PASS=Your Password
```
If the card/file/keys are missing, the app uses the Wi‑Fi credentials set in menuconfig.

## Build & Flash
- ESP-IDF project; typical workflow:
  ```
  idf.py set-target esp32
  idf.py build
  idf.py -p /dev/ttyUSB0 flash monitor
  ```
- Keep `sdkconfig` under version control if you want to share the exact menuconfig settings.

## Repository Hints
- Ignore build artifacts (`build/`, `sdkconfig.old`, `sdkconfig.ci`, logs); keep `sdkconfig` if desired.
- Sources live in `main/`; dependencies are tracked via `dependencies.lock`/`managed_components/`.

## Behavior Overview
1. Power/boot or RTC wake.
2. (Cold boot) optional USB delay for flashing; load SD Wi‑Fi creds; init Wi‑Fi.
3. Decide if this is a “refresh wake” (time-of-day match); run SNTP + write RTC when needed.
4. Fetch API data; render to e-paper; signal completion.
5. Choose next alarm based on park/coaster state: short interval while open, park-open time if before open, or next-day refresh time when closed.
6. Enter deep sleep.

Enjoy fast, low-power updates on your e-paper display!***

## Credits & Disclaimer
- Wait time data provided by [api.wartezeiten.app](https://api.wartezeiten.app) — check it out!
- Logos/branding are displayed for personal, non-commercial use; no copyright infringement intended. If there is any issue, please contact me and I will address it.
