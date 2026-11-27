# Beta Changelog

Release notes for beta versions. Full releases are in [CHANGELOG.md](CHANGELOG.md).

## 1.1.7.0b2

### Added
- When the automatic update check has installed an update, a download badge appears at the top of the screen, so you know there's one waiting. **Check for updates now** in the menu becomes **Restart to update to** the new version, which restarts straight into it. Otherwise the new version runs the next time VentPuck starts, as before.

### Changed
- The automatic update check is back to how it worked in 1.1.6.0, as the 1.1.7.0b1 change held up start up. It runs in the background once the car is connected (or after 30 seconds without a car), pauses the readings while it checks, and installs the update quietly.

## 1.1.7.0b1

### Changed
- The automatic update check runs when VentPuck starts again, before it connects to the car, instead of in the background once the car is connected. The screen no longer freezes while it checks. The dashboard comes straight up and the text at the top shows what it's doing ("Connecting to Wi-Fi...", "Checking the time...", "Checking for updates..."). The car connection starts as soon as the check is done, which takes up to about 10 seconds when there's no Wi-Fi in range.
- If the check finds an update, it now shows the update screen with progress, installs it and restarts straight into the new version, like **Check for updates now**. Before, it installed silently and the new version only ran the next time VentPuck started, so it often looked like nothing had happened.
- The car readings are no longer paused for the update check, because the check is finished before the car connection starts.
- The automatic check tries Wi-Fi once instead of three times, so start up is never held up for long.

## 1.1.0.0b2

### Added
- Beta builds show a small β badge in the top left corner of every screen, as a reminder that you're running a test build.

### Changed
- The accent colour now works in colour mode too, not just dark mode. It changes the background gradient, text, icons, buttons, menu, boot animation and update screen. The purple accent keeps the classic look and still uses `colour1` and `colour2` from `settings.ini`.
- The **Accent** setting is now in the menu in both modes.

## 1.1.0.0b1

### Added
- New `adapter_name` setting, so adapters other than the OBDLink can be used. It matches part of the adapter's Bluetooth name (default `OBDLink`). Leave it empty to use the first BLE OBD adapter it finds.
- Support for adapters that use the FFE0/FFE1 Bluetooth serial service, which many ELM327 clones use, as well as the FFF0 one.

### Changed
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
