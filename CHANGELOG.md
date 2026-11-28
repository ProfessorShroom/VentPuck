# Changelog

## 1.1.8.0

### Added
- Bluetooth and Wi-Fi icons at the top of the screen. Each one has a red line through it when it isn't connected. Bluetooth shows the connection to the OBD adapter. Wi-Fi is only on while VentPuck checks for updates, so its icon only shows during the check: crossed out while VentPuck connects, normal once it's on Wi-Fi, and gone when the check finishes. If automatic updates are off or no Wi-Fi is set up, it doesn't show at all.
- The automatic update check shows what it's doing in the text at the top of the screen, where it normally says "Looking for adapter...": connecting to Wi-Fi, checking for updates, and how far the download has got. When it's done it says "Up to date", "Update ready, restart to use it" or what went wrong, for 5 seconds.

### Fixed
- The automatic update check could find a new version but fail to download it, because it ran out of memory. The secure connection to GitHub now uses VentPuck's larger spare memory, so there's plenty of room for the download.
- The automatic update check now pauses the search for the OBD adapter while it runs, as well as the car readings. Before, it only paused once the car readings had started, so with no adapter found the Bluetooth search carried on during the check and used up memory and radio time.

### Changed
- VentPuck uses less memory to draw the screen, and Bluetooth only sets memory aside for one connection, since VentPuck only ever connects to one adapter. This leaves more room for downloading updates.
- On 1.1.7.0 or older, the automatic update check may still run out of memory downloading this version, because the fix only takes effect once 1.1.8.0 is installed. If it doesn't update on its own, use **Check for updates now** in the menu.

## 1.1.7.0

### Added
- When the automatic update check has installed an update, a download badge appears at the top of the screen, so you know there's one waiting. Tap the badge, or choose **Restart into** the new version in the menu (where **Check for updates now** usually is), to restart straight into it. Otherwise the new version runs the next time VentPuck starts, as before.

## 1.1.6.0

### Added
- If an update keeps crashing, VentPuck goes back to the version you had before. A new version is on trial until it has run for 5 minutes in one go. If it crashes 3 times before then, VentPuck switches back to the previous version and won't install that version again, only a newer one. Turning the ignition off, a missing adapter or no car connection don't count as crashes, so they never cause a rollback. This protects updates from 1.1.6.0 onwards.
- If the car connection locks up for a minute, VentPuck restarts itself instead of freezing with the last readings on screen.

### Changed
- Updates are only installed if GitHub provides a checksum for them, so a download can always be checked before it's installed.
## 1.1.5.0

### Added
- **Auto update** in the menu, which turns the automatic update check on or off. It was only in `settings.ini` before and was off by default, so the background check never ran unless you had set `update=true`. It takes effect the next time VentPuck starts.

### Changed
- The page dots at the bottom of the screen now follow the theme colour.

## 1.1.4.0

### Changed
- **Brightness** in the menu now steps every 10%, from 10% to 100%, instead of every 20%.
- The home screen readings, labels and icons are bigger and easier to read at a glance.

## 1.1.3.0

### Added
- Two new themes, **BMW** and **BMW Modern**. BMW makes all the text and icons the orange of a classic BMW instrument cluster. BMW Modern makes the labels, units and icons orange and the readings white.

### Changed
- **Accent** is now called **Theme**, in the menu and in `settings.ini` (`theme`). The colours work as before under the same names, and an existing `accent` setting is still read.
- The VENT PUCK wordmark in the boot animation now follows the theme.
- Dark mode is now on by default. If you've already set `dark_mode`, your setting is kept.
- The automatic update check now pauses the car readings while it runs, and tries Wi-Fi up to three times. Bluetooth and Wi-Fi share the same radio, and with the readings running the check often couldn't connect or download, so updates only worked from **Check for updates now**. The readings show `--` during the check, then carry on as normal.

## 1.1.2.0

### Added
- The outside temperature turns ice blue with a snowflake at 3°C (37°F) or below, as a frost warning.
- If the car doesn't report an outside temperature, the battery voltage is shown at the top instead. It's left out on the Voltage screen, on the home screen if Voltage is one of its readings, and on the 0-60 screen so the timer keeps its full speed.
- **Top reading** in the menu (`top_reading` in `settings.ini`), which turns off the outside temperature and voltage at the top of the screen. It's on by default, and when it's off they aren't requested from the car for the top of the screen either.
- **Shut down** in the menu, which disconnects from the adapter, turns the screen off and puts VentPuck to sleep. Tap the screen or cycle the power to start it again.

### Changed
- The outside temperature has moved from the top right to the top middle of every screen, with a thermometer icon, so it no longer crowds the large readings.
- The β badge on beta builds has moved to the top middle too, next to the outside temperature.


## 1.1.1.0

### Added
- **Flip screen** in the menu (`flip_screen` in `settings.ini`), which turns the display and touch by 180 degrees for a gauge that is mounted upside down. It changes straight away, and the boot and update screens are flipped too.


## 1.1.0.0

### Added
- New `adapter_name` setting, so adapters other than the OBDLink can be used. It matches part of the adapter's Bluetooth name (default `OBDLink`). Leave it empty to use the first BLE OBD adapter it finds.
- Support for adapters that use the FFE0/FFE1 Bluetooth serial service, which many ELM327 clones use, as well as the FFF0 one.

### Changed
- The accent colour now works in colour mode too, not just dark mode. It changes the background gradient, text, icons, buttons, menu, boot animation and update screen. The purple accent keeps the classic look and still uses `colour1` and `colour2` from `settings.ini`. The **Accent** setting is now in the menu in both modes.
- IAT is now labelled "Intake Air" on the home screen, its own screen and the start screen setting, to match Oil and Coolant.
- Oil, coolant, Intake Air and ambient temperatures are now read every 2 seconds and voltage every second, instead of on every cycle. That leaves more of the Bluetooth link for RPM, boost and speed, so they update faster, which also helps the 0-60 timer. A reading that's missing is retried sooner, so a new screen fills straight away.
- The automatic update check no longer holds up start up. It used to take up to about 35 seconds before the car connection started. Now it runs in the background once the adapter is connected (or after 30 seconds without a car). It installs any update it finds, and the new version runs the next time VentPuck starts. **Check for updates now** in the menu still works as before, with progress on screen.
- On the BMW, oil temperature (which takes about 10 adapter commands per read) no longer slows down every other reading.

### Fixed
- If the adapter disconnects, for example on a stall-start or a flaky adapter, VentPuck now reconnects in place instead of restarting. You no longer get the boot animation, sound and update check again.
- A reading that stops updating now shows `--` after 10 seconds, instead of freezing on the last value as if it were live. All readings clear when the adapter disconnects.
- One glitchy low throttle reading can no longer be saved as the closed-throttle position. The zero point now only moves after 5 low readings in a row.
- The serial console no longer calls itself "VentPuck OBDLink CX test".
- Fixed a possible random restart when a reply from the adapter arrived late. The reply buffer was shared between two tasks without a lock.
- A late reply to one command can no longer be read as the answer to the next one.


## 1.0.1.0

### Changed
- Throttle position now learns your car's closed position and shows it as 0%, instead of around 15% with your foot off.
- Load and throttle readings are now included in the serial and SD card log, along with the raw throttle value.

## 1.0.0.0 - Initial Release

### Added
- Initial Release of VentPuck
