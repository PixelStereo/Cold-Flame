# Feature coverage and development criteria

The reference is the Eos family, rather than Element. Documentation review and the feature inventory remain partial. No feature parity is claimed.

| Area | Implemented and tested in 0.7 | Remaining work |
|---|---|---|
| Selection | Channels, ranges, groups, manual levels | Query, filters, complex ordering, subfixtures |
| Palettes | Intensity, referenced values | Position, color, beam, by-type, locking, nested references |
| Cues | Shared cue library, repeated sequence occurrences, Record look, Update, deletion, automatic numbering, up/down/wait, Link, Follow/Hang, single Execute target, per-channel times | Multipart, per-attribute timing, loop counts, multiple Execute targets, mark |
| Cue Only | Engine, Record/Update checkbox, Blind level edits | Full category filters and Eos range-update semantics |
| Sequences | Simultaneous playback, priority, Go/Back/Pause, release, numbered defaults, movable tabs, stable step IDs, insertion, grip drag-and-drop, context-menu deletion, inline triggers and row Block/Assert controls, exact step links, one-shot Set Next | full multi-attribute Move Fade ownership, Assert, HTP, configurable background |
| Editing | Live parameters, channel/parameter tracking spreadsheet, source navigation, channel Block, Trace/Trace zeros, Cue Only, show-definition Undo/Redo, full-cue Block/Assert | Per-parameter Assert, selective/live Undo, tracking change preview |
| Files | Versioned JSON, atomic save, validated loading | Migration, autosave, recovery, selective merge |
| Patch | Editable channel overview, footprint-aware batch stride, complete footprint overlap checks, multiple fixture starts, unpatching, 8/16-bit dimmers and generic fixtures, patch transfer | Geometry, MVR, fixture replacement/migration |
| GDTF | Import/export, preserved original archives, direct single-break modes, 8/16/24/32-bit offsets, default/Snap, cue parameter recording/output | Physical units, curves, relations, multi-break, mode masters and geometry/cells |
| Network | Multiple ArtDmx/sACN outputs, interface selection, multi-IP unicast/broadcast/multicast, route ranges, rates, sACN priority/source/termination, packet counters, UDP tests | ArtPoll, sACN sync, RDM, redundancy, dedicated output thread |
| OSC | Float messages and three local receive actions | Full dictionary, feedback, bundles, timing |
| Effects | 1D intensity wave generator | Absolute/relative/step effects, phases, variables, curves, tempo |
| Pixel mapping | 1D generator | RGB/RGBW matrices, free geometry, video, masks, layers, GPU |
| Usability | 120-channel ALL/USED/EDIT pages, balanced button targets, grouped sequence actions, palette/sequence/library/tracking/playback views, contextual footer help, NyxMap numeric dragging; virtual keypad | Workspaces, interactive magic sheets, touch, accessibility, 3D visualization |
| Submasters | Record selected/live intensity, HTP card bank, channel/level editor, Update/Delete/Out/Full, names, stable numbers, saved definitions | Inhibitive masters, timed bump/flash, configurable buttons |
| Hardware | On-screen actions, computer keyboard | USB HID/MIDI interface, firmware, mapping, encoders, indicator feedback |
| Reliability | Unit tests, UDP, Qt interaction test, offscreen launch | Endurance, load, physical rig, network loss, multiple users, backup |

Sequential Go now changes ownership only for move instructions and preserves tracked manual values and ongoing tracked fades. Go To and Link recall the complete tracked state. These tested intensity cases do not establish full Eos ownership equivalence for other attributes, asserts, blocks or all playback configurations.

## Official references consulted

- [Palette options](https://www.etcconnect.com/WebDocs/Controls/EosFamilyOnlineHelp/en/Content/10_Palettes/Palette_Options.htm)
- [Concepts: tracking, ownership, Block / Assert](https://www.etcconnect.com/WebDocs/Controls/EosFamilyOnlineHelp/en/Content/04_System_Basics/Important_Concepts.htm)
- [Multiple cue lists](https://www.etcconnect.com/WebDocs/Controls/EosFamilyOnlineHelp/en/Content/12_Cues_and_Cue_Lists/11_Multiple_Cue_Lists/Multiple_Cue_Lists.htm)
- [Cue list properties](https://www.etcconnect.com/WebDocs/Controls/EosFamilyOnlineHelp/en/Content/12_Cues_and_Cue_Lists/09_Using_the_Cue_List_Index/Cue_List_Properties.htm)
- [Effects](https://www.etcconnect.com/WebDocs/Controls/EosFamilyOnlineHelp/en/Content/14_Effects/About_Effects.htm)
- [Magic Sheets](https://www.etcconnect.com/webdocs/Controls/EosFamilyOnlineHelp/en/Content/18_Magic_Sheets/About_Magic_Sheets.htm)
- [Show Control](https://www.etcconnect.com/WebDocs/Controls/EosFamilyOnlineHelp/en/Content/23_Show_Control/About_Eos_Show_Control.htm)

## Measuring “better”

User scenarios to measure: create a 30-cue show; correct a referenced color; insert a cue without changing later cues; identify why a parameter does not respond; replace a fixture; resume a sequence after a jump; operate without a mouse. Metrics: action count, duration, errors, recovery, and visibility of the controlling source.


## Workspace references for the 0.2 redesign

The layout uses compact channel tiles, a persistent cue sheet and a single-column spreadsheet patch overview. This is an interpretation informed by the official [grandMA3 Fixture Sheet](https://help2.malighting.com/grandMA3/2.5/HTML/operate_fixture_sheet.html), [grandMA3 DMX Sheet](https://help2.malighting.com/grandMA3/2.1/HTML/patch_dmx_sheet.html) and [Eos Playback Status Display](https://www.etcconnect.com/WebDocs/Controls/EosFamilyOnlineHelp/en/Content/04_System_Basics/04_Playback_Status_Display_%28PSD%29_%5BTab_2%5D/PLAYBACK_STATUS_DISPLAY_%28PSD%29_%5BTAB_2%5D.htm). No comparative usability superiority has been measured.

Record stores changed intensity values from the live look while unchanged values track. Full recording filters, update propagation preview and comprehensive palette reference handling remain necessary for full Eos-style editing.

## Workspace and submaster references

The tabbed target views and explicit stored-content editor are informed by ETC’s [Submasters display](https://www.etcconnect.com/WebDocs/Controls/EosFamilyOnlineHelp/en/Content/15_Submasters/Submasters_%5BTab_15%5D.htm) and [Submaster Properties](https://www.etcconnect.com/WebDocs/Controls/EosFamilyOnlineHelp/en/Content/15_Submasters/Submaster_Properties.htm). Cold Flame uses an explicit Save in its submaster editor; Eos Blind edits store immediately. Only additive HTP intensity is implemented here, without Eos’s other submaster modes and timing options. No comparative usability superiority has been measured.

The supported fixture subset, file formats and tracking edit behavior are documented in [FIXTURES-AND-TRACKING.md](FIXTURES-AND-TRACKING.md).
