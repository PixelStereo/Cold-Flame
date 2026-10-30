# Parameters: edit a fixture's live controls

Choose a channel at the top of **Parameters**. For a multicell fixture, choose
the master, all cells or one cell below it.

- **Intensity / Master intensity** controls the fixture's overall brightness.
  Raise it above zero to see colour changes. **Cell intensity** is relative to
  the fixture master: a cell at 50% with its master at 50% gives 25% intensity.
- **Value (%)** is editable. Click the number, type a value from 0 to 100 and
  press **Enter** or **Tab**. The field keeps your unfinished input during live
  refresh. Arrow keys adjust the value using the keyboard.
- **Drag the dotted grip** beside a number to change it live. Shift gives fine
  adjustment; Ctrl/Command gives coarse adjustment. Dragging inside the number
  selects text, as in an ordinary text field.
- **Red / Green / Blue / White, Pan, Tilt, Shutter…** are fixture controls.
  Their numeric values are normalized DMX percentages, rather than degrees or
  calibrated physical units. **Choose color…** offers visual colour selection
  for a supported RGB/RGBW/CMY scope without changing its intensity.
- **Source** shows Manual or Playback / home, plus colour-palette references
  where present. Record / Update uses the console's usual cue-recording behavior;
  live parameter edits do not automatically rewrite a stored cue.

The status line shows **Intensity** (the control value) and **Output** (the
resulting channel intensity). For example, an intensity of 75% and a grand master
at 50% give 37.5% output. An inhibitive submaster at Out gives zero output while
the editable intensity stays at 75%. Active Blackout, Park and inhibition are
identified in the status line. Park overrides the resulting output; editing the
underlying control does not change the parked value.

**Release manual** returns this fixture to playback/home, preserving captured
controls. **Home parameters** restores colour, position, beam and cell controls
to their home values while retaining the fixture's master intensity.

Version 0.12.1 separates native text entry from dragging, prevents table editors
from intercepting input, and fixes editable intensity being replaced by inhibited
output. Input tests cover real window mouse targeting, decimal entry, live
refresh, dragging, compact windows, 200% scaling and Qt's Windows style.

Version 0.12.2 explicitly sizes and colours the numeric text editor, so native
spin-box insets cannot hide its digits. Visual tests compare the rendered field
against the same field with its text cleared, checking visible, contrasting glyphs
for zero, fractional and maximum values, including 32-bit precision. macOS CI also
runs these checks under Cocoa's native Mac style at normal and 200% scaling.
