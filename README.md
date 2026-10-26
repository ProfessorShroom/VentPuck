<p align="center">
  <img src="VentPuck Logo.png" alt="VentPuck" height="300"/>
</p>

<p align="center">
  <img alt="Status" src="https://img.shields.io/badge/status-work%20in%20progress-orange?style=for-the-badge">
  <img alt="ESP32-S3" src="https://img.shields.io/badge/ESP32-S3-E7352C?style=for-the-badge&logo=espressif&logoColor=white">
  <img alt="LVGL" src="https://img.shields.io/badge/UI-LVGL-8A2BE2?style=for-the-badge">
  <img alt="OBD2" src="https://img.shields.io/badge/OBD2-BLE-0078D4?style=for-the-badge">
<a href="https://github.com/ProfessorShroom/VentPuck/blob/main/LICENSE.md"><img alt="License" src="https://img.shields.io/badge/license-Personal_Use-blue?style=for-the-badge"></a>

## VentPuck

**_Copyright © Charlie Howard 2026 All rights reserved_**

A small round touch-screen gauge for your car's air vent. It reads live data from the OBD2 port over Bluetooth LE and shows it on a 1.75 inch AMOLED round display, running on an ESP32-S3.

Built for my Renault Megane RS 250, but the car-specific parts are kept in a profile file so it can be adapted to other cars.

This is an early work in progress, so there's nothing to download yet.

### Features

- One big main reading, swipe left/right to change it.
- Oil and coolant temperature always shown at the bottom.
- Main readings: coolant temperature, oil temperature, boost (with peak boost), RPM, speed, battery voltage, 0-60 timer, reading and clearing DTC codes and intake air temperature.
- Car-specific PIDs, gauge ranges and warning colours live in a profile file.

### Hardware

- **Display / controller** - an ESP32-S3 board with a 1.75 inch AMOLED 466x466 round capacitive touch display.
- **OBD2 adapter** - any Bluetooth OBD2 adapter that supports **BLE** ELM327-style adapter. The ESP32-S3 doesn't support Bluetooth Classic, so Classic-only adapters won't work. I used the OBDLink CX as it supports a sleep function to reduce power drain when the car is parked for longer than a few days, but the cheaper ones will work fine.
- **Power** - 12 V to 5 V buck converter on a switched fuse tap, so the gauge turns on and off with the ignition. You could also use a USB C cable plugged in to the 12v socket like a phone charger, but this is a lot less neat.

### Changelog

#### Latest Update
