# Eos behavior audit — development version 0.10

**Result: Cold Flame does not have full Eos feature or behavioral parity.** This is a partial, source-based audit of the workflows implemented so far. The entire Eos manual has not been read or exhaustively tested. Tests verify specific scenarios; they do not establish equivalence of a whole feature family. Reference: the current public Eos Family online manual (consulted 28–29 October 2026).

## Verified scenarios and remaining differences

| Topic | Documented Eos behavior | Cold Flame result / evidence |
|---|---|---|
| Channel/address relationship | A channel can drive several addresses; each address belongs to only one channel. | Implemented for dimmers and supported multi-parameter fixture modes, with whole-footprint overlap checks. `multiPatchOutputAndRoundtrip` receives channel 12 at 22.013, 3.018 and 3.512 over sACN. Patch dialog rejects duplicate addresses. |
| Record | Store the stage look; unchanged data tracks. Record Only stores manual data. | Library Record stores a full live intensity/parameter snapshot; Record Only stores manual values. The legacy inline recording API tracks unchanged intensities. Full category/parameter recording filters are missing. |
| Tracking | Unchanged values continue until a new move or block. | Intensity/fixture-parameter tracking, local move removal, channel/parameter Block, Trace with inactive-level handling, forced trace through zero, Cue Only and source navigation are implemented. Full category filters and range-update semantics remain incomplete. See `channelTrackingTraceAndSharedCueSuppression` and `traceInactiveAndCueOnly`. |
| Sequential Go | Tracked channels do not automatically take over manually modified values. | `eosTrackedManualAndOwnership` tests that a tracked channel stays manual; incoming move instructions take over their channels. |
| Go To Cue | Recall both moves and tracked values for an out-of-sequence jump. | Same intensity test verifies that Go To recalls the tracked recorded value. |
| Sequence ownership | Playback ownership follows move instructions rather than every Go. | `sequentialGoDoesNotStealTrackedChannels` verifies basic LTP ownership; `unchangedFadeContinuesAcrossNextCue` verifies fade continuity. Full attributes, asserts, background priorities and exclusions remain incomplete. |
| Common / split intensity time | Up and Down can share a time or use different times. | Shared default and separate Down implemented. Discrete channel up/down/delay tested. Category timings for focus/color/beam are absent. |
| Delay | Delay precedes a transition; it is distinct from Follow/Hang. | UI currently calls this Wait. One shared cue delay and individual channel delays are implemented; separate category/up/down delays are missing. |
| Link | Change the pending cue. Follow/Hang can trigger that pending cue automatically. | Current library sequences link to explicit step IDs; legacy `eosLinkAndFollow` tests pending Link and automatic Follow using inline cue numbers. Cross-sequence fader selection is simplified. Loop counts and Go After Loop are missing. |
| Follow / Hang | Follow counts from start; Hang from completion. Negative Hang permits overlap. | Positive Hang and Follow tested; negative Hang is accepted. All timing edge cases, multipart interactions and loop behavior are not exhaustively validated. |
| Execute | Trigger a cue at source playback. Another list runs concurrently; the same list jumps. | One target supported. `eosHangAndExecute` tests another list. Multiple targets, macros, snapshots and external trigger actions are missing. |
| Submasters | HTP by default, with additive/inhibitive/effect modes and many configurable properties. | Additive HTP and inhibitive intensity, Up/Dwell/Down bump timing, Manual/Hold/Timed dwell, fader override, stable-number commands, editing and persistence are implemented. See SUBMASTERS.md for tested scenarios and overlap policy. The editor requires Save rather than immediate Eos Blind storage. Attribute/effect/LTP modes, priorities, exclusions and shielded behavior remain absent. |
| Intensity palettes | References allow shared changes across cues. | Basic references tested. Other palette categories, by-type/default/discrete values, locked/absolute modes and full Update behavior are missing. |

Additional verified intensity cases in 0.3:

- Blind edits change stored data while leaving live output untouched (`blindEditsDoNotRecallLive`, `blindSpreadsheetEditsStoredOnly`).
- Full-cue Block preserves values against upstream edits; Assert separately takes over playback (`blockIsEditingAndAssertIsPlayback`). Channel/parameter Block is added in 0.7; per-parameter Assert remains incomplete.
- Capture protects manual values against recall (`captureSurvivesPlayback`). Capture latch is missing.
- Temporary channel Park bypasses master and blackout and is withheld from recordings (`parkBypassesMastersAndIsNotRecorded`). Address Park, scaling and persistence are missing.
- Show-definition Undo/Redo retains current transport progress (`editUndoDoesNotRewindTransport`). Live/manual/selective Undo is missing.
- The command line distinguishes cue selection from cue recall (`commandProgrammingDoesNotAccidentallyFire`). This is a limited grammar, not the full Eos command language.
- Automatic follow overlap and bounded zero-time loops are tested; configurable loop counts remain missing.

The **After cue** field is a Cold Flame extension. It launches a side cue at completion without advancing the main cursor. It is explicitly separate from Eos Link and Execute. One side slot is maintained per originating list; side cues do not recursively execute their own triggers.

Numbering new cues and sequences in increments of ten is the requested Cold Flame default; it is not claimed to be the Eos factory default.

## Major feature families without parity

- Full GDTF interpretation: geometry/cells, compound fixtures, mode masters, multiple breaks, physical-unit conversion; MVR, RDM and device health. Supported direct modes now provide 8/16/24/32-bit parameter output.
- Focus and Beam palettes; presets; full by-type and embedded reference handling; color tools and calibration. Basic normalized Color Palette references are implemented in 0.8.
- Complete tracking range/update editing, per-parameter Assert, Mark/AutoMark, multipart cues, preheat, curves, rate and scene attributes.
- Full target/category editing, complete selective Undo/Redo, Query, filters, partitions and user profiles.
- Full playback/fader configuration, HTP cue lists, inhibitive/effect subs, background rules, shielded controls and backup operation.
- Step, absolute and relative effects, effects variables, parameter phasing and complete effect playback rules.
- Magic Sheets, workspaces, snapshots, macros, Augment3d and complete pixel/media layers.
- MIDI/MSC/MTC/SMPTE, complete OSC dictionary and feedback, RTC and network event lists.
- Show-file merging, migration, recovery, multiconsole/multiuser synchronization and redundancy.

These entries are requirements, not working controls. See FEATURES.md for implementation scope and HARDWARE.md for the still-unbuilt physical surface.

## Official sources used for this audit

- [Important concepts: channel/address, tracking and ownership](https://www.etcconnect.com/WebDocs/Controls/EosFamilyOnlineHelp/en/Content/04_System_Basics/Important_Concepts.htm)
- [Using output address vs port/offset](https://www.etcconnect.com/WebDocs/Controls/EosFamilyOnlineHelp/en/Content/06_Patch/01_Patching_Conventional_Fixtures/Using_Output_Address_vs_Port_Offset.htm)
- [Patch overview](https://www.etcconnect.com/WebDocs/Controls/EosFamilyOnlineHelp/en/Content/06_Patch/Patch_%5BTab_12%5D.htm)
- [Record Only in Live](https://www.etcconnect.com/WebDocs/Controls/EosFamilyOnlineHelp/en/Content/12_Cues_and_Cue_Lists/01_Recording_Cues_in_Live/Using_Record_Only_Live.htm)
- [Go To Cue](https://www.etcconnect.com/WebDocs/Controls/EosFamilyOnlineHelp/en/Content/13_Cue_Playback/%5BGo_To_Cue%5D.htm)
- [Timing](https://www.etcconnect.com/WebDocs/Controls/EosFamilyOnlineHelp/en/Content/12_Cues_and_Cue_Lists/03_Timing/About_Timing.htm)
- [Discrete channel and parameter timing](https://www.etcconnect.com/WebDocs/Controls/EosFamilyOnlineHelp/en/Content/12_Cues_and_Cue_Lists/03_Timing/Discrete_Channel_Param.htm)
- [Link / Loop](https://www.etcconnect.com/WebDocs/Controls/EosFamilyOnlineHelp/en/Content/12_Cues_and_Cue_Lists/04_Cue_Attributes/Link_Loop.htm)
- [Follow / Hang](https://www.etcconnect.com/WebDocs/Controls/EosFamilyOnlineHelp/en/Content/12_Cues_and_Cue_Lists/04_Cue_Attributes/Follow_Hang.htm)
- [Cue triggers / Execute](https://www.etcconnect.com/WebDocs/Controls/EosFamilyOnlineHelp/en/Content/12_Cues_and_Cue_Lists/Cue_Triggers.htm)
- [Submaster properties](https://www.etcconnect.com/WebDocs/Controls/EosFamilyOnlineHelp/en/Content/15_Submasters/Submaster_Properties.htm)

Remaining verification includes physical fixtures, long-duration runs and native macOS/Windows application launches. Local validation so far is Linux Qt, network loopback, and mocked macOS build-script orchestration.

Additional sources consulted for 0.3: [Capture](https://www.etcconnect.com/WebDocs/Controls/EosFamilyOnlineHelp/en/Content/08_Manual_Control/04_Advanced_Manual_Control/Capture.htm), [Park](https://www.etcconnect.com/WebDocs/Controls/EosFamilyOnlineHelp/en/Content/08_Manual_Control/05_Park_%5BTab_20%5D/PARK_%5BTAB_20%5D.htm), [Block vs Assert / Live and Blind](https://www.etcconnect.com/WebDocs/Controls/EosFamilyOnlineHelp/en/Content/04_System_Basics/Important_Concepts.htm).

## Version 0.5: reusable cues and sequence occurrences

Stored cues and sequence steps are now separate. This is deliberately the reusable-look model documented for [Congo/Cobalt](https://support.etcconnect.com/ETC/Consoles/Congo_Cobalt/Software_and_Programming/Preset_vs_Cue_in_Congo), rather than a claim that Eos stores the same cue number several times in one list. A stored look may be used in many occurrences. Each step has an independent ID, timing, triggers, Block/Assert and local intensity overrides. Links target the step ID, so repeated cue numbers never determine an arbitrary occurrence. UI exposes an exact-occurrence picker. Reordering preserves IDs and links.

The older Eos-style Link/Follow and tracking tests still cover legacy inline steps; they do not establish identical shared-library semantics. Full-look library recordings contain explicit values for all channels, while Record Only stores manual channels. Library edits affect future recalls, with active fades keeping their existing targets. Blind edits create local step overrides. Format 2 introduced library references and arbitrary step order; format 3 adds fixture parameters and local tracking suppressions; format 1 inline shows remain readable without automatic conversion to shared cues.

Tests cover three occurrences of a shared cue, independent step times, exact links after reorder, shared updates without live recall, rename and reference propagation, deletion guards, save/load, rejection of missing references, Undo/Redo, and legacy playback.

Version 0.7 adds the [fixture and tracking workflow](FIXTURES-AND-TRACKING.md). Reviewed references include Eos Patch/About Patch, overlap restrictions, channel Block and Update Using Trace. The reusable-library model remains distinct from Eos cue lists.

Version 0.8 adds independent Color Palettes referenced by cue parameter data, direct cue assignment and local overrides. The immutable hidden white default is a Cold Flame policy requested by the user, not an Eos factory behavior. See [PALETTES.md](PALETTES.md) for source references and differences.

## Version 0.9: submaster operation

Additive/inhibitive intensity and Manual/Hold/Timed bump follow the documented home/target and Up/Dwell/Down concepts. Tests cover partial release, zero-time flash, timed overshoot, fader override, Park, recording, definition Undo, legacy loading and Qt operation. Inhibitors reset to Full on show load; additive subs reset to Out. The lowest overlapping inhibitive fader applies in Cold Flame; equivalent overlap behavior has not been verified against an Eos console. Full priority/exclusion and non-intensity behavior remain outside this implementation. See [SUBMASTERS.md](SUBMASTERS.md).

## Version 0.10: fixture definitions and patch conflicts

The fixture-definition editor follows the Eos New/Copy, parameter, coarse/fine address, raw Home and Snap workflow. It supports multiple direct DMX modes and 8/16/24/32-bit parameters. Copying an assigned definition preserves the original parameter identities; fixture replacement with stored parameter data still requires migration support. See [FIXTURE-EDITOR.md](FIXTURE-EDITOR.md) for implemented scope and the official reference.

Patch conflict resolution previews conflicting starts and preserves unaffected copies, channels and cue data. Self-overlapping new fixtures and universe crossings remain errors. This is an explicit Cold Flame workflow; identical Eos conflict-dialog behavior is not claimed.
