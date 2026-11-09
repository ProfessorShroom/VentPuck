<p align="center">
  <img src="VentPuck Logo.png" alt="VentPuck" height="300"/>
</p>

<p align="center">
<a href="https://github.com/ProfessorShroom/VentPuck/releases"><img alt="Release" src="https://img.shields.io/github/v/release/ProfessorShroom/VentPuck?include_prereleases&label=release&style=for-the-badge"></a>
<img alt="ESP32-S3" src="https://img.shields.io/badge/ESP32-S3-E7352C?style=for-the-badge&logo=espressif&logoColor=white">
<img alt="LVGL" src="https://img.shields.io/badge/UI-LVGL-8A2BE2?style=for-the-badge">
<img alt="OBD2" src="https://img.shields.io/badge/OBD2-BLE-0078D4?style=for-the-badge">
<a href="https://github.com/ProfessorShroom/VentPuck/blob/main/LICENSE.md"><img alt="License" src="https://img.shields.io/badge/license-Personal_Use-blue?style=for-the-badge"></a>

## VentPuck

**_Copyright © Charlie Howard 2026 All rights reserved_**

A small round touch-screen gauge for your car's air vent. It reads live data from the OBD2 port over Bluetooth LE and shows it on a 1.75 inch AMOLED round display, running on an ESP32-S3.

Built for my Renault Megane 3 RS, with a generic OBD2 mode for other cars. Support for more cars can be added as people test it.

### Currently being tested on

- **Renault Megane RS 3**
- **BMW E8x**


Releases and betas are on the [Releases](https://github.com/ProfessorShroom/VentPuck/releases) page.

### Features

- Swipe left and right between screens, with a smooth slide. The screen you were on is remembered as long as you have an SD card inserted.
- **Home** screen with four readings you choose, by default oil, coolant, boost and RPM.
- Single reading screens for boost, oil temperature, coolant temperature, RPM, speed and battery voltage, each with oil and coolant temperature always shown at the bottom.
- **0-60 mph** timer (or 0-100 km/h if you use km/h). It arms itself when you scroll to it and reads speed at the full rate. Tap to reset, and your best time is saved.
- **Fault codes** screen. Scan only runs when you press the button, and clearing codes is a tap and hold with a progress bar. It refuses to clear while the car is moving.
- Press and hold on any screen for the settings menu: car, speed, boost and temperature units, brightness, start screen, home screen readings, boot animation, dark mode, update channel and version.
- Dark mode with a pure black background, which suits the AMOLED, and a choice of accent colour for the text, icons and buttons.
- Colour mode if you prefer which by default uses my classic blue to purple gradient. You can configure any colours you want in the .ini file.
- Boot animation with your own logo. Put a `logo.png` on the SD card to replace the default. This must be no larger than 1024x1024 and 1MB in size. I would suggest around 700x700 maximum for speed.
- OTA updates over Wi-Fi from GitHub, with a Release or Beta channel.

### Supported cars

| Setting | Car | What you get |
| --- | --- | --- |
| Megane 3 RS | Renault Megane 3 RS (F4Rt) | Standard OBD2 readings plus oil temperature, boost, battery voltage and barometric pressure from the engine ECU. |
| Clio 4 RS | Renault Clio 4 RS (X98) | Same requests as the Megane 3 RS: oil temperature, boost, battery voltage and barometric pressure from the engine ECU. Untested so far. |
| Megane 4 RS | Renault Megane 4 RS (XFB) | Same requests as the Megane 3 RS: oil temperature, boost, battery voltage and barometric pressure from the engine ECU. Untested so far. |
| BMW E8x | BMW 1 Series E8x (E81 / E82 / E87 / E88) | Standard OBD2 readings for now, BMW specific readings to come. |
| Generic | Any car with OBD2 | Standard OBD2 readings only, so some screens such as oil temperature may show nothing. |

Changing the car restarts the gauge.

### Hardware

- **Display / controller** - a Waveshare ESP32-S3-Touch-AMOLED-1.75, which is an ESP32-S3 with a 1.75 inch AMOLED 466x466 round capacitive touch display.
- **OBD2 adapter** - any Bluetooth OBD2 adapter that supports **BLE** ELM327-style adapter. The ESP32-S3 doesn't support Bluetooth Classic, so Classic-only adapters won't work. I used the OBDLink CX as it supports a sleep function to reduce power drain when the car is parked for longer than a few days, but the cheaper ones will work fine.
- **Power** - 12 V to 5 V buck converter on a switched fuse tap, so the gauge turns on and off with the ignition. You could also use a USB C cable plugged in to the 12v socket like a phone charger, but this is a lot less neat.
- **SD card (optional)** - for `settings.ini` and a custom `logo.png`. VentPuck works fine without one, and the on-screen menu saves your settings to the gauge itself.

### Settings

Everything is optional. If there's an SD card, VentPuck creates a `settings.ini` on the first run, and the on-screen menu keeps it up to date.

```ini
vehicle=meganers3
update=true
update_channel=release
ssid=YourWifi
password=YourPassword
ssid2=YourWifi2
password2=YourPassword2
ssid3=YourWifi3
password3=YourPassword3
ssid4=YourWifi4
password4=YourPassword4
ssid5=YourWifi5
password5=YourPassword5
colour1=#1A0650
colour2=#3A12A8
speed_unit=mph
boost_unit=bar
temp_unit=C
start_screen=last
home_items=oil,coolant,boost,rpm
brightness=80
boot_animation=true
dark_mode=false
accent=purple
```

| Key | Values |
| --- | --- |
| `vehicle` | `generic`, `meganers3`, `clio4rs`, `megane4rs`, `bmwe8x` |
| `update` | `true` or `false`, whether to check for updates when it starts |
| `update_channel` | `release` or `beta` |
| `ssid`, `password` | Your Wi-Fi, only used to check for updates, you can provide up to 5 different networks |
| `colour1`, `colour2` | Dial gradient as hex `#RRGGBB`, outer edge and middle |
| `speed_unit` | `mph` or `kph` |
| `boost_unit` | `bar`, `psi` or `kpa` |
| `temp_unit` | `C` or `F` |
| `start_screen` | `last`, `home`, `boost`, `oil`, `coolant`, `rpm`, `speed`, `voltage`, `zerosixty` or `faults` |
| `home_items` | Four of `boost`, `oil`, `coolant`, `rpm`, `speed`, `voltage` |
| `brightness` | 5 to 100 |
| `boot_animation` | `true` or `false` |
| `dark_mode` | `true` or `false` |
| `accent` | Dark mode accent colour: `purple`, `blue`, `cyan`, `green`, `yellow`, `orange`, `red`, `pink` or `white` |

The Wi-Fi password is stored as plain text, and Wi-Fi is only ever used to check for updates.

### Updates

Flash VentPuck over USB once, then it can update itself over Wi-Fi from the releases on this page. Set `update=true` and your Wi-Fi details in `settings.ini`. The check happens quietly at start up and the screen only changes if it finds a newer version. You can also run a check from the menu with **Check for updates now**.

Release versions look like `1.0.0.0` and betas look like `1.0.0.0b1`. On the Release channel only releases are used. On the Beta channel a beta is only used if it's newer than the latest release, so if the release and beta are the same version, or the release is newer, you get the release. Betas are test builds that may be unstable, so choosing Beta in the menu shows a warning first.

### Building

VentPuck is written in C++ with PlatformIO, LVGL and NimBLE-Arduino.

```bash
pio run -e amoled175 -t upload
```

### Changelog

#### Latest Update