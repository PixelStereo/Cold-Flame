# Cold Flame

Desktop lighting console written in C++17 / Qt 6 for macOS, Windows and Linux.
**Development version 0.4.0: full Eos coverage is a goal, not an existing capability.**

![Interface](cold-flame-preview.png)

![Patch overview](patch-preview.png)

![Network outputs](network-preview.png)

## Downloads and automated releases

Each published GitHub release triggers native application builds for a universal macOS app for Apple Silicon and Intel, Linux x86_64, and Windows x86_64. Downloads include bundled Qt libraries, native platform plugins and SHA-256 checksums. Windows includes an installer `.exe` and portable ZIP; macOS includes `.app` ZIPs and DMGs; Linux includes a runnable archive. See [release instructions and signing status](docs/RELEASING.md).

## macOS: install, build and launch

From Terminal, in the repository root:

```sh
./build.sh
```

The script installs Homebrew if absent (its installer may request your macOS password), then installs missing Qt 6, CMake, Ninja and zlib packages, builds a Release `.app`, and opens it. Existing Homebrew packages are reused. You can use `./build.sh --no-run` to build without launching, or `./build.sh -- "$PWD/examples/theatre.cfl"` to launch the sample show. The default build directory is `build-macos`; use `--build-dir PATH` to override it. `--no-deps` skips package installation when dependencies are already available.

## Build and test

Requirements: CMake 3.21+, a C++17 compiler, Qt 6.2+ (Core, Widgets, Network, Test), and zlib.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

Run `build/cold-flame` (Windows multi-configuration: `build/Release/cold-flame.exe`).
Native release packaging is enabled with `-DCOLD_FLAME_DEPLOY=ON`; it deploys the required Qt runtime and plugins. CI builds and tests all target operating systems; a configured job is not evidence of success until it has run.

## Your first show

1. A new show contains 60 dimmer channels patched to addresses 1–60. The live **Channels** tab shows numbers, levels and state marks (P for Park, C for Capture, a warm dot for manual), without patch addresses. Use the range selector for pages of 60; **+ Page** adds the next 60 channels, assigning unused DMX addresses. Selection can span pages. **Patch** is the adjacent tab with an editable single wide spreadsheet. Edit labels and comma-separated DMX target lists directly, or add/repatch a range. Conflicts are highlighted and block Apply; Revert draft discards edits. The DMX targets column takes the available width for channels with many destinations; scroll vertically through channels. Full view opens a larger patch editor.
2. Select channels with the mouse (Ctrl / Shift for multiple selections), or type `Channel 1 Thru 12 At 50` and press Enter.
3. Set the common fade time and click **Record**. Cues receive numbers 10, 20, 30… and names cue-10, cue-20… automatically. Record captures changes in the current unmastered look, preserves palette references, and leaves unchanged values tracked. Double-click cue cells to edit numbers, names or timing.
4. Select a cue and click **Set Next** to queue it for the next GO without changing live output or stored links. **Go To** plays it immediately. After the queued cue plays, normal cue order resumes. Go takes over channels with move instructions; unchanged tracked channels retain their manual values. Go To explicitly recalls the full tracked cue, including manually overridden tracked channels. `Ctrl+Space` triggers Go; `Ctrl+S` saves.
5. Use **+** above the cue sheet to add sequences (1 / sequence-1, 2 / sequence-2…). Use **−** to delete the selected sequence; one must remain. Double-click a sequence tab to rename it. Drag sequence tabs, or the Sequences / Submasters / Palettes tabs, to arrange them. Each retains its own state and fade. Higher priority wins; at equal priority, the most recently triggered sequence wins for the channels it owns. Release sequence restores other active contributions.
6. Intensity palettes are recorded from the selection and recalled by reference. Editing a palette updates its resolution in cues on their next recall. **Update** changes the selected cue using manual values, retaining its timing.
7. Save as `.cfl`. Files contain the patch, palettes, groups and sequences, without transient playback states. Opening a show disables outputs.

Commands include `Group 1`, `Intensity 2`, `Cue 1/20` (select without firing), `Go To Cue 1/20` (recall), `Sequence 1` (select the sequence), `Go`, `Back`, `Pause` and `Resume`. Channel selection supports addition and subtraction: `1 Thru 12 + 20 - 5 At 50`. Use `At +10` or `At -10` for relative levels on the current selection. `Record Cue 1/30`, `Record Only Cue 1/40` and `Update Cue 1/30` operate on stored cues. The virtual keypad is available under **Programming**. It provides initial access to actions; configurable hardware integration is not yet implemented.

## Core editing and manual control

- **Record Only** stores manual values; Record includes the changed stage look. **Cue Only** prevents a stored edit from changing the following cue.
- **Blind** opens a stored cue spreadsheet. Editing a level saves immediately without recalling it on stage. Warm cells are blocked, sage cells are moves and grey cells are tracked. Blind also has a Cue Only option.
- **Block** protects the selected cue against upstream tracking edits. It does not itself assert playback. **Assert** makes a cue take back its tracked intensity values when played.
- **Capture** toggles manual protection on selected channels. Playback and submasters cannot override captured channels; manual level changes still work. Release manual leaves captured values alone.
- **Park** locks selected channel outputs; **Unpark** restores normal output. As in Eos, Park overrides grand master and blackout and is excluded from recordings. The tile displays PARK. These are temporary channel parks; address parks, scaling and park persistence are not implemented.
- **Edit → Undo/Redo show edit** restores up to 100 stored-definition states, including cue, palette, group and patch edits. It preserves running transport and manual levels. This is not Eos's complete selective Undo system and does not undo Go, live manual changes or hardware effects.

Equivalent selection commands include `Select Active`, `Select Manual`, `Capture`, `Park`, `Unpark`, `Home`, `Release`, `Block` and `Assert` (the last two use the selected cue).

## Multiple DMX destinations per channel

In Patch, the **DMX targets** cell accepts comma-separated `universe.address` destinations. For example, channel 12 can have:

```text
22.013, 3.018, 3.512
```

All three destinations receive the same channel level, fades and blackout. Multiple targets are saved with the show; old single-address show files still load. Duplicate DMX addresses are rejected. Universe numbers in the patch are relative to the configured first output universe: with first universe 1, the example sends to sACN universes 22 and 3; with Art-Net first universe 0, their wire universe numbers are 21 and 2. Set the first Art-Net universe to 1 if you want the displayed universe numbers to match its wire numbers.

## Cue timing and submasters

The cue sheet shows Up, Down, Wait and Link. Times are in seconds.

- **Up** is the common transfer time. **Down** defaults to `= Up`; enter a number to split rising and falling fades. Clear Down to restore the shared time.
- **Wait** delays the start of the transition; it does not automatically trigger another cue.
- **Link** changes the pending cue, as in Eos. It does not fire automatically without Follow or Hang. It accepts a cue number or `sequence/cue`.
- **Triggers…** exposes **Follow** (seconds from cue start) and **Hang** (seconds after cue completion, including negative values for overlap). They are mutually exclusive and automatically fire the pending cue, honoring Link.
- **Execute** fires one cue when the source starts. Another sequence runs independently; executing within the same sequence jumps that sequence.
- **After cue** is a separate Cold Flame extension: fire a side playback when the step finishes, keeping the main next step unchanged. It uses one side-playback slot per originating sequence and does not chain triggers from the side cue. It must not be confused with Eos Link.
- Automatic zero-time chains are bounded to prevent a looping show from blocking the application. Configurable Eos loop counts and Go After Loop are not implemented.
- **Channel timing** applies separate up/down/wait times to the selected channels in the selected cue. Use **Use cue timing** to remove overrides. The INDIV. column counts overrides.
- The **Submasters** tab contains a card bank, numbered 1, 2, 3… Each card has a fader, Out, Full, Edit, Update and Delete. **Edit** changes the name, channel membership and stored intensities; Save commits the edit. **Update** replaces the contents with selected live channels (all if none are selected), excluding that submaster’s own contribution. Manual values have precedence; use Release Manual below the channel grid to hear the submaster. Definitions are saved; faders reset to zero on load. Deleting a running submaster removes its contribution immediately; Edit → Undo restores its definition.

A sample show is included: `build/cold-flame examples/theatre.cfl`. It contains four lighting looks, two palettes and two submasters.

## Networking and interaction

Outputs are **disabled at startup**. **Network** opens the tab next to **Channels** and **Patch**, inspired by the [NyxMap output panel](https://github.com/PixelStereo/NyxMap/blob/main/src/ui/DmxOutputsPanel.cpp). Add independent outputs with **+ Output**, configure them, tick **On**, and choose **Apply outputs**. Changes stay in a draft until applied. **Stop all** immediately disables every route and terminates active sACN streams.

Each output has a name, Art-Net/sACN protocol, local IPv4 interface (or automatic system routing), Unicast with one or more addresses, Art-Net Broadcast or sACN Multicast, 1–44 Hz rate, and sACN priority/source name. The table shows packet counts and send errors. UDP send success does not confirm reception by a fixture.

Routing maps **First patch universe** to **First network universe**, preserving offsets. **Universe count** limits the range; 0 means all patched universes from the first patch universe. Only patched universes are sent. For example, patch universe 3 can map to sACN universe 22 on one output and Art-Net universe 7 on another. Multiple outputs may run together. Configuration is saved with the show, with On switches reset when opened. Applied route settings are outside the show-definition Undo history. Reapplying restarts routes and their packet counters.

Art-Net universe numbering is zero-based; sACN accepts 1–63999. Patch addresses are 1-based and split into blocks of 512. Target refresh rate: approximately 43.5 Hz. Fades, UI updates and output currently share the Qt event loop; separate engine/output threads remain to be implemented.

Enable OSC under **Interaction**, bound to `127.0.0.1:9000`. Supported messages use a single float argument:

- `/coldflame/sequence/1/go` with a positive value: Go.
- `/coldflame/master` from 0 to 1: grand master.
- `/coldflame/blackout`: positive enables blackout; zero disables it.

OSC bundles, feedback and configurable remote reception are not yet supported.

**Pixel mapping** provides a sinusoidal intensity wave across selected channels in ascending numerical order, with adjustable speed and spread. Its layer overrides sequences and is overridden by manual values. This is an initial 1D generator; RGB matrices, video layers, masks and GPU processing remain to be developed.

**Fixtures → Inspect GDTF profile** reads a `.gdtf` archive (stored or Deflate ZIP), checks its CRC, and displays manufacturer, modes, footprint and attributes. It does not yet convert those attributes into intelligent fixture parameters. The current patch supports 8-bit channels; no proprietary grandMA format support is claimed.

## Status and next steps

See the [Eos behavior audit](docs/EOS-COMPATIBILITY.md), [feature matrix](docs/FEATURES.md) and [hardware surface proposal](docs/HARDWARE.md). This alpha is intended for preparation and experimentation. It has not yet been validated against a physical lighting rig, through endurance testing, or during a live show.

The interface palette follows [DMXPulse](https://github.com/PixelStereo/DMXPulse/blob/main/src/window.cpp): charcoal surfaces, neutral text, sage accents and warm state indicators. Channel tiles use compact numeric information; patch details remain in their own tab.
