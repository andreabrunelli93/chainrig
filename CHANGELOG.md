# Changelog

All notable changes to ChainRig. Versions follow [semantic versioning](https://semver.org): small improvements bump the last number (0.2.1), new major features the middle one.

## [0.2.1] - 2026-11-26

### Added
- Update notice: a few seconds after launch ChainRig checks the latest GitHub release in the background. If a newer version exists, a tappable line in the status area offers Download, Skip this version or Later. It never shows a popup on its own.
- Settings menu: "Check for updates now", "Check for updates at startup", and the current version.

## [0.2.0] - 2026-11-26

First public release.

### Added
- 10-slot VST3 chain with drag-to-reorder, rename and slot types with icons.
- 4 footswitches that show the names of the slots they control, MIDI foot controller support.
- Gapless preset switching (plugins already loaded are reused).
- Tempo (BPM) per preset, sent to tempo-synced plugins.
- Input strip (level, gain, mute) and output meter with latching clip lights.
- Built-in recorder (24-bit WAV to Downloads).
- YIN tuner that mutes the output.
- Plugin chooser and crash-safe, out-of-process plugin scanning.
- Unsaved-changes indicator and crash recovery.
- Audio health diagnostics (peak block load, xruns).
- Windows (ASIO) and universal macOS builds on GitHub Releases.
