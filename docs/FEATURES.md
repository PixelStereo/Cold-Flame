# Feature coverage and development criteria

The reference is the Eos family, rather than Element. Documentation review and the feature inventory remain partial. No feature parity is claimed.

| Area | Implemented and tested in 0.1 | Remaining work |
|---|---|---|
| Selection | Channels, ranges, groups, manual levels | Query, filters, complex ordering, subfixtures |
| Palettes | Intensity, referenced values | Position, color, beam, by-type, locking, nested references |
| Cues | Record, Update, deletion, decimal numbers, tracking | Multipart, delays, per-parameter timing, follow/hang, links, mark |
| Cue Only | Tested engine operation | UI controls and complex insertion cases |
| Sequences | Simultaneous playback, priority, Go/Back/Pause, release | Eos-style per-parameter Move Fade ownership, Assert, HTP, configurable background |
| Editing | Live programming | Blind, spreadsheet, transactional Undo/Redo, tracking change preview |
| Files | Versioned JSON, atomic save, validated loading | Migration, autosave, recovery, selective merge |
| Patch | 8-bit channels, multiple universes | Intelligent GDTF patching, 16/24/32-bit parameters, geometry, MVR, fixture replacement |
| GDTF | ZIP/XML inspection and CRC validation | Full interpretation of functions, units, curves, relations and cells |
| Network | ArtDmx and sACN, local UDP test | ArtPoll, sACN sync/termination, network interface selection, RDM, redundancy |
| OSC | Float messages and three local receive actions | Full dictionary, feedback, bundles, timing |
| Effects | 1D intensity wave generator | Absolute/relative/step effects, phases, variables, curves, tempo |
| Pixel mapping | 1D generator | RGB/RGBW matrices, free geometry, video, masks, layers, GPU |
| Usability | Channel, palette, sequence and playback views; virtual keypad | Workspaces, interactive magic sheets, touch, accessibility, 3D visualization |
| Hardware | On-screen actions, computer keyboard | USB HID/MIDI interface, firmware, mapping, encoders, indicator feedback |
| Reliability | Unit tests, UDP, Qt interaction test, offscreen launch | Endurance, load, physical rig, network loss, multiple users, backup |

Output ownership is simplified in this version: a sequence asserts its entire tracked state on each Go. Eos uses finer semantics based on move instructions. The engine must evolve before claiming behavioral compatibility.

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
