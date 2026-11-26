# ChainRig

A live-performance host for guitarists: a 10-slot VST3 chain, 4 large touch footswitches,
a tuner, MIDI foot controller support, presets, an output meter and a built-in recorder.
Built with JUCE 9 and C++20 for Windows (ASIO) on touch tablets. It also runs on macOS and Linux.

The interface text is in Italian.

## Features

- **10-slot serial chain.** Tap a slot to pick a plugin from your list. The same menu opens the plugin's editor, bypasses, replaces, moves or removes it. Empty slots are skipped.
- **Drag to reorder.** Drag a slot onto an empty slot to move it there, or onto an occupied slot to insert it and shift the others. Footswitch assignments follow the plugins.
- **Slot name and type.** From the slot menu: **Rinomina...** (rename; leave empty to go back to the plugin name) and **Tipo** (type): amp, cab/IR, drive, compressor, EQ, modulation, delay, reverb, other. The type sets the slot's icon and tag. "Automatic" guesses it from the plugin's category and name. Name and type are stored in the preset.
- **4 large footswitches.** Each one turns its assigned slots on and off, and shows their names (for example "DRIVE + DELAY"). A slot assigned to a footswitch only plays while that footswitch is on.
- **Tuner** (YIN). It works on the input, before the chain, and mutes the output while you tune. It shows the note, octave, cents, frequency and the nearest standard-tuning string.
- **Gapless preset switching.** Plugins already in the chain are kept (even if they move to another slot) and only receive the new settings. New plugins are created while the old chain keeps playing, then the chain is swapped in one step.
- **Presets.** One readable JSON file (`.chainrig`) per preset, in `Documents/ChainRig/Preset`. A preset stores every plugin with its full state, the bypass states, slot names and types, footswitch assignments and the tempo. Switch presets with `<` / `>` or over MIDI.
- **Unsaved changes.** An amber dot and "NON SALVATO" next to the preset name show changes since the last save: plugins loaded, moved or removed, bypass, names, types, footswitch assignments, tempo, and parameter changes inside a plugin. Turning footswitches on and off does not count. Switching preset from the touch screen asks whether to save first. Switching from a MIDI controller never asks, so it cannot block you on stage.
- **Crash recovery.** Every 10 s, if something changed, the full chain is written to a recovery file next to the settings. A clean exit deletes it. If ChainRig closed unexpectedly, the next launch restores the chain as it was and tells you. If restoring itself crashes (for example a plugin that crashes on load), the following launch skips it and opens the last preset instead.
- **Tempo (BPM).** Each preset sends a tempo to the plugins, so tempo-synced delays and reverbs behave. Set it from the preset menu: **Tempo: 120 BPM...**.
- **Plugin chooser.** Tick only the plugins you use live. The others are never loaded, which matters if you have hundreds of plugins installed.
- **Crash-safe plugin scan.** Each plugin is scanned in a separate process. If one hangs for more than 30 s or crashes, it is excluded and ChainRig carries on.
- **Input strip** (left): input level after gain, with a latching **CLIP** light, an input **gain** knob (-24 to +24 dB, double-tap for 0 dB, saved in the settings) and an input **MUTE**. Muting the input lets delay and reverb tails ring out.
- **Output meter** (right) with peak hold and a latching **CLIP** light. Tap either meter to reset it.
- **Recorder.** **REC** writes the output (channels 1-2, i.e. the whole chain) to a 24-bit stereo WAV file in your Downloads folder.
- **Low latency.** ASIO on Windows, Core Audio on macOS. The top bar shows the estimated latency, the CPU load, the peak load of a single audio block and the xrun count.

## Building

JUCE 9.0.6 is downloaded automatically with CMake FetchContent. Use `-DJUCE_PATH=/path/to/JUCE` to build against a local copy instead.

### Windows

Requires Visual Studio 2022 or newer with the "Desktop development with C++" workload, and Git.

```
cmake -B build -A x64
cmake --build build --config Release
```

The executable is `build\ChainRig_artefacts\Release\ChainRig.exe`. It is linked against the static C++ runtime, so no redistributable is needed.

The GitHub Actions workflow in `.github/workflows/build-windows.yml` builds it on every push. Download it from *Actions → latest run → Artifacts → ChainRig-Windows*.

### macOS

Requires the Xcode command line tools (`xcode-select --install`) and CMake (`brew install cmake`).

```
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j8
open build/ChainRig_artefacts/Release/ChainRig.app
```

On first launch macOS asks for microphone permission. Allow it, otherwise the guitar input stays silent. You can change it later in *System Settings → Privacy & Security → Microphone*.

Only VST3 plugins are hosted. Audio Units are disabled (`JUCE_PLUGINHOST_AU=0`).

## First run

1. **IMPOSTAZIONI → Scheda audio** (Settings → Audio device): choose **ASIO** on Windows or **CoreAudio** on macOS, then your interface. 48 kHz with a 64-256 sample buffer is a good start.
2. **IMPOSTAZIONI → Ingresso chitarra** (guitar input): pick the input your guitar is plugged into.
3. **IMPOSTAZIONI → Scegli plugin...** (choose plugins): filter by name, tick the plugins you use live and press **APPLICA** (apply). **Nessuno** unticks everything visible. **Avanzate...** opens JUCE's full plugin list, where you can add custom folders.
4. Tap an empty slot → **Scegli plugin** to load a plugin.
5. **MODIFICA** (edit) → tap a footswitch to choose the slots it controls. Tap **MODIFICA** again to leave edit mode.
6. Tap the preset name → **Salva come...** (save as).

For the stage: **IMPOSTAZIONI → Schermo intero** (full screen). On Windows you can put a shortcut to `ChainRig.exe` in the startup folder (Win+R → `shell:startup`), so it starts full screen on the last preset.

## Preparing a Windows machine for the stage

1. **Audio interface driver.** Install the manufacturer's ASIO driver and choose it in ChainRig (type **ASIO**). Avoid the generic Windows audio driver for live use.
2. **Power.** *Settings → System → Power*: power mode "Best performance", and screen/sleep "Never" while plugged in. In the classic Control Panel power options, set *USB settings → USB selective suspend* to *Disabled*.
3. **System sounds.** Set the Windows default output to the built-in speakers, not to your interface, and choose the "No sounds" scheme. Turn on *Do not disturb*. Otherwise notifications, browser tabs and other apps play through your rig.
4. **Plugins.** Install your VST3 plugins in `C:\Program Files\Common Files\VST3`, then use **Scegli plugin...** to pick only the ones you need.
5. **Presets from another computer.** Copy the `.chainrig` files to `Documents\ChainRig\Preset`. Reload any amp captures or IR files that use file paths inside the plugin, then save the preset again.
6. **First launch.** Windows SmartScreen may warn about an unrecognised app: choose *More info → Run anyway*.
7. **Before the gig.** Play for 20-30 minutes with your real presets and watch "picco" and "xrun" in the top bar. If xruns appear, raise the buffer one step. Consider airplane mode (unless you use Bluetooth MIDI) and close other applications.

## MIDI foot controllers (USB or Bluetooth)

All MIDI inputs are enabled automatically, including those connected after launch.

| Action | Default message |
|---|---|
| Footswitches 1-4 | CC 80, 81, 82, 83 |
| Tuner | CC 84 |
| Next / previous preset | CC 85 / CC 86 |
| Preset number N (alphabetical order) | Program Change N |

In **IMPOSTAZIONI → Pedaliera MIDI** choose how CC values are read:
- **Momentary**: each press toggles. For controllers that send 127 on press and 0 on release.
- **Absolute**: the value decides (127 = on, 0 = off). For controllers that alternate 127 and 0.

To change the CC numbers, quit ChainRig and edit `cc_button1`…`cc_button4`, `cc_tuner`, `cc_nextPreset` and `cc_prevPreset` in the settings file:
- Windows: `%APPDATA%\ChainRig\ChainRig.settings`
- macOS: `~/Library/Application Support/ChainRig/ChainRig.settings`

Bluetooth: controllers sold with their own USB dongle work out of the box. For native Windows Bluetooth LE MIDI, build with `-DCHAINRIG_BLUETOOTH_MIDI=ON` and pair the controller in Windows first.

## Troubleshooting

- **Clicks or dropouts.** Watch "picco" (the peak load of one audio block) and "xrun" in the top bar. Above 100%, a block took longer than the buffer allows. Some plugins have occasional CPU spikes even when the average load is low. Try a larger buffer or another plugin. Events are logged to `scan.log`, next to the settings file. Start ChainRig with `CHAINRIG_LEVEL_LOG=1` to also log input and output levels every 500 ms.
- **A tempo-synced delay repeats far too late.** Set the preset tempo. Without a tempo, VST3 plugins receive 0 BPM.
- **Sounds you didn't play.** Check that no other application is playing to, or recording from, the same interface. Your OS system sounds are best sent to a different output.

## Known limitations

- Loading a plugin that is not already in the chain still takes a moment (the old chain keeps playing meanwhile). Plugins that load large files in their state, such as amp captures, may briefly glitch while switching. Bypass and footswitches are click-free.
- A plugin crash while playing takes down the whole application.
- If a mono-in/stereo-out plugin is bypassed, the right channel of that slot is silent.
- File paths stored inside plugin states (amp captures, IRs) differ between machines and must be reloaded after moving a preset to another computer.

## Code structure

- `Source/Engine.*`: audio device, processing graph (input → tuner → slots → output), bypass, footswitches, MIDI, presets, tempo, audio health checks.
- `Source/Recorder.h`: the audio callback wrapper. It runs the graph, then feeds the meter, the diagnostics and the WAV writer.
- `Source/Tuner.h`: tuner node, lock-free FIFO to the UI, YIN pitch detection.
- `Source/PluginScanner.h`: out-of-process plugin scanning.
- `Source/PluginChooser.*`: the "choose plugins" window.
- `Source/Look.*`: colours, fonts, icons, look and feel, top-bar buttons.
- `Source/MainComponent.*`: the touch UI (preset selector, slots, footswitches, meter, tuner overlay, plugin windows).
- `Source/Main.cpp`: application, main window and the scanner child process entry point.

## License

ChainRig is free software, released under the [GNU Affero General Public License v3.0](LICENSE) (AGPL-3.0).

It is built on [JUCE](https://juce.com), used under JUCE's open-source (AGPLv3) licence. If you distribute a modified version, you must make its source code available under the same licence. Plugins you load in ChainRig are separate products with their own licences.
