# Cold Flame

Desktop lighting console written in C++17 / Qt 6 for macOS, Windows and Linux.
**Development version 0.1: full Eos coverage is a goal, not an existing capability.**

![Interface](cold-flame-preview.png)

## Build and test

Requirements: CMake 3.21+, a C++17 compiler, Qt 6.2+ (Core, Widgets, Network, Test), and zlib.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

Run `build/cold-flame` (Windows multi-configuration: `build/Release/cold-flame.exe`).
Bundle the Qt libraries when distributing the application. Development executables are not yet installers. CI is configured to build and test on all three operating systems; only observed results count as validation.

## Your first show

1. A new show contains 48 dimmer channels patched to addresses 1–48. **Show → Patch** adds or repatches a channel.
2. Select channels with the mouse (Ctrl / Shift for multiple selections), or type `Channel 1 Thru 12 At 50` and press Enter.
3. Set the fade time, click **Record**, then enter a number and label. Manual values are recorded in the selected sequence. Unchanged parameters track forward.
4. Click **Release manual**, then **Go**. Manual values retain control until released. `Ctrl+Space` triggers Go; `Ctrl+S` saves.
5. Add more sequences. Each retains its own state and fade. Higher priority wins; at equal priority, the most recently triggered sequence wins for the channels it owns. Release sequence restores other active contributions.
6. Intensity palettes are recorded from the selection and recalled by reference. Editing a palette updates its resolution in cues on their next recall. **Update** changes the selected cue using manual values and the displayed fade time.
7. Save as `.cfl`. Files contain the patch, palettes, groups and sequences, without transient playback states. Opening a show disables outputs.

Additional commands: `Group 1`, `Intensity 2`, `Cue 2/15`, `Sequence 2` (Go for that sequence). The virtual keypad is available under **Programming**. It provides initial access to actions; configurable hardware integration is not yet implemented.

## Networking and interaction

Outputs are **disabled at startup**. Configure the protocol, destination and first universe under **Network**. Art-Net universe numbering is zero-based; sACN accepts 1–63999 and supports `multicast` as the destination. Absolute patch addresses are split into blocks of 512; the first universe is added to each block index. Target refresh rate: approximately 43.5 Hz. Fades, UI updates and output currently share the Qt event loop; separate engine/output threads remain to be implemented.

Enable OSC under **Interaction**, bound to `127.0.0.1:9000`. Supported messages use a single float argument:

- `/coldflame/sequence/1/go` with a positive value: Go.
- `/coldflame/master` from 0 to 1: grand master.
- `/coldflame/blackout`: positive enables blackout; zero disables it.

OSC bundles, feedback and configurable remote reception are not yet supported.

**Pixel mapping** provides a sinusoidal intensity wave across selected channels in ascending numerical order, with adjustable speed and spread. Its layer overrides sequences and is overridden by manual values. This is an initial 1D generator; RGB matrices, video layers, masks and GPU processing remain to be developed.

**Fixtures → Inspect GDTF profile** reads a `.gdtf` archive (stored or Deflate ZIP), checks its CRC, and displays manufacturer, modes, footprint and attributes. It does not yet convert those attributes into intelligent fixture parameters. The current patch supports 8-bit channels; no proprietary grandMA format support is claimed.

## Status and next steps

See the [feature matrix](docs/FEATURES.md) and [hardware surface proposal](docs/HARDWARE.md). This alpha is intended for preparation and experimentation. It has not yet been validated against a physical lighting rig, through endurance testing, or during a live show.
