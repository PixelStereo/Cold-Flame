# Eos behavior audit — development version 0.2

**Result: Cold Flame does not have full Eos feature or behavioral parity.** This is a partial, source-based audit of the workflows implemented so far. The entire Eos manual has not been read or exhaustively tested. Tests verify specific scenarios; they do not establish equivalence of a whole feature family. Reference: the current public Eos Family online manual (consulted 28 October 2026).

## Verified scenarios and remaining differences

| Topic | Documented Eos behavior | Cold Flame result / evidence |
|---|---|---|
| Channel/address relationship | A channel can drive several addresses; each address belongs to only one channel. | Implemented for 8-bit dimmers. `multiPatchOutputAndRoundtrip` receives channel 12 at 22.013, 3.018 and 3.512 over sACN. Patch dialog rejects duplicate addresses. |
| Record | Store the stage look; unchanged data tracks. Record Only stores manual data. | Record captures changed live intensity and references; manual-only recording exists in the engine. Full recording filters and Record Only UI are missing. |
| Tracking | Unchanged values continue until a new move or block. | Basic intensity tracking tested; block flags and full Cue Only editing are incomplete. |
| Sequential Go | Tracked channels do not automatically take over manually modified values. | `eosTrackedManualAndOwnership` tests that a tracked channel stays manual; incoming move instructions take over their channels. |
| Go To Cue | Recall both moves and tracked values for an out-of-sequence jump. | Same intensity test verifies that Go To recalls the tracked recorded value. |
| Sequence ownership | Playback ownership follows move instructions rather than every Go. | `sequentialGoDoesNotStealTrackedChannels` verifies basic LTP ownership; `unchangedFadeContinuesAcrossNextCue` verifies fade continuity. Full attributes, asserts, background priorities and exclusions remain incomplete. |
| Common / split intensity time | Up and Down can share a time or use different times. | Shared default and separate Down implemented. Discrete channel up/down/delay tested. Category timings for focus/color/beam are absent. |
| Delay | Delay precedes a transition; it is distinct from Follow/Hang. | UI currently calls this Wait. One shared cue delay and individual channel delays are implemented; separate category/up/down delays are missing. |
| Link | Change the pending cue. Follow/Hang can trigger that pending cue automatically. | `eosLinkAndFollow` tests pending Link and automatic Follow. Cross-sequence fader selection is simplified. Loop counts and Go After Loop are missing. |
| Follow / Hang | Follow counts from start; Hang from completion. Negative Hang permits overlap. | Positive Hang and Follow tested; negative Hang is accepted. All timing edge cases, multipart interactions and loop behavior are not exhaustively validated. |
| Execute | Trigger a cue at source playback. Another list runs concurrently; the same list jumps. | One target supported. `eosHangAndExecute` tests another list. Multiple targets, macros, snapshots and external trigger actions are missing. |
| Submasters | HTP by default, with additive/inhibitive/effect modes and many configurable properties. | Additive HTP intensity faders only. Recording, scaling and persistence tested. Other modes, timed bump, priorities and shielded behavior are absent. |
| Intensity palettes | References allow shared changes across cues. | Basic references tested. Other palette categories, by-type/default/discrete values, locked/absolute modes and full Update behavior are missing. |

The **After cue** field is a Cold Flame extension. It launches a side cue at completion without advancing the main cursor. It is explicitly separate from Eos Link and Execute. One side slot is maintained per originating list; side cues do not recursively execute their own triggers.

Numbering new cues and sequences in increments of ten is the requested Cold Flame default; it is not claimed to be the Eos factory default.

## Major feature families without parity

- Fixture parameters beyond 8-bit intensity; multicell and compound fixture interpretation; full GDTF mode patching; RDM and device health.
- Focus, Color and Beam palettes; presets; by-type and embedded reference handling; color tools and calibration.
- Full tracking editing, Blocks, Assert, Mark/AutoMark, multipart cues, preheat, curves, rate and scene attributes.
- Blind, spreadsheet editing, transactional Undo/Redo, Query, filters, partitions and user profiles.
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
