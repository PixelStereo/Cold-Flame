# Programming surface — €150 excluding VAT target

The budget covers the control surface only, excluding the computer and network equipment. Supplier prices and an assembled prototype have not yet been validated. Feasibility within €150 still needs costing, particularly enclosure fabrication.

Target: USB HID keys, followed by a bidirectional protocol for encoders and indicators. Use a diode per key to prevent ghosting, debounce inputs, and test simultaneous key presses. Essential programming functions should remain accessible without switching banks.

Proposed layout:

- Full keypad: digits, decimal point, +, −, Thru, At, Full, Enter, Clear.
- Selection: Channel, Group, Next, Previous, Last, Active, Manual.
- References: Intensity, Position, Color, Beam, Preset, Effect.
- Editing: Record, Record Only, Update, Delete, Copy, Move, Label, Undo.
- Playback: Cue, Sequence, Load, Go To Cue, Time, Delay, Go, Pause, Back.
- Control: Live, Blind, Track/Cue Only, Block, Assert, Mark, Home, Release, Capture, Park, Highlight, Fan, Shift, Escape.
- Push encoders with fine adjustment, a level wheel, and banked direct-access keys. Encoder and fader counts depend on costing.

Avoid multiple displays and motorized faders within this budget. Contextual labels can appear in a fixed software area near the parameters. Hardware must not report success for functions that the software does not implement.

Remaining work: schematic, bill of materials and pricing, key matrix, enclosure, firmware, versioned protocol, configurable mapping, prototype, and testing on all three operating systems. No purchases or manufacturing orders have been placed.
