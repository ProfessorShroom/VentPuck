# Changelog

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
