# Code and UX review: OFL fixtures and searchable fixture selection

## 0.12.1: Parameters input follow-up

- Parameter text fields now use native focus and editing, with a separate dotted
  drag grip. Table editing cannot open a competing editor over the field, and
  the global NumberDrag filter explicitly leaves these editors alone.
- Enter/Tab commits typed values; active text survives live refresh. Mouse
  targeting is exercised through the application's actual window at desktop and
  compact sizes, in addition to field-level typing, drag and DMX-output checks.
- Editable intensity uses values before inhibition. A reproduced regression had
  reset the editor to zero when an inhibitive submaster was at Out. The status
  line now separates control intensity and output and names active output limits.
- Dragging is cancelled on lost mouse grab or window deactivation, preventing
  later hover events from changing levels. The panel uses readable names for
  common colour controls and explains master/cell intensity.
- Live value/source changes do not resize the input columns. The drag regression
  fixes its pointer path in screen coordinates, including across a moved window,
  instead of recomputing the pointer path from a potentially moving widget.
- Tests cover normal DPI, 200% scaling and Qt Windows style. The illustrated
  fixture/parameters preview was regenerated and visually inspected.

The sections below record the original 0.12.0 fixture-library review.

Scope: OFL JSON parsing/export, profile persistence/identity, fixture chooser and
patch import/export integration. This is a focused review of this change, not a
claim of full Eos parity or a physical-rig qualification.

## Findings addressed

- **Unbounded patch menus:** every channel and Batch previously listed every
  mode. Each now holds six generic choices and its current custom selection.
  A catalogue is built once when opening search, with case-insensitive multi-word
  matching and at most 200 displayed results. Tested with 1,000 custom profiles.
- **Ambiguous mode selection:** the chooser shows manufacturer, fixture, mode,
  footprint and readiness in separate columns. Details explain unsupported
  semantics and raw controls. Unavailable modes cannot be confirmed.
- **Accidental patch edits:** scrolling cannot change a closed fixture picker;
  cancellation preserves the assignment. Search supports keyboard selection,
  Enter, double-click and clear-search. Stored-parameter protections still apply.
- **OFL byte/default loss:** tests cover non-adjacent/reversed-position fine aliases,
  high-byte defaults in reduced-resolution modes, percentage defaults, unused
  slots, sparse matrices, both insertion orders and independent colour cue recall.
- **Wrong intensity interpretation:** partial-range/reversed intensity,
  conditional switching, incompatible precedence and constants are unavailable
  rather than mapped to an incorrect linear dimmer. Other unmodelled capability
  channels are explicitly exposed as raw DMX controls.
- **Lossy export:** imported OFL JSON is retained byte for byte through save/load.
  Native conversion rejects semantics it cannot preserve. Schema tests caught
  missing mandatory range fields for native Pan/Tilt/Zoom exports; normalized
  percentage ranges now satisfy the schema without inventing physical angles.
- **Profile identity and metadata:** editing an OFL source creates a native copy;
  assigned profiles remain immutable, and conflicting simultaneous GDTF/OFL
  sources are rejected. Show/patch format versions prevent older readers from
  silently discarding the new source data. Duplicate imports reuse definitions.
- **Repeated import work:** template/channel definitions compile once per profile;
  multi-file import refreshes patch pickers once after the batch.
- **Visual sizing:** actual application screenshots caught squeezed manufacturer
  and mode columns. Resize policies are now set before column widths. Field
  height, buttons, search behavior and cancellation are exercised at normal DPI,
  200% scaling and with Qt's Windows style.

## Validation and limits

Reviewing the previous GitHub run also identified a macOS UDP test race and
unequal native Windows submaster button minimum widths. The packet test now
waits for both expected universe packets for Full and Blackout. Submaster action
columns share the largest native button minimum, and card reflow uses actual
minimum sizes. Controls inherit the application style before measurement.
The first 0.12 native builds also exposed an ambiguous C++ collator declaration
accepted by GCC but rejected by Clang/MSVC. Explicit brace initialization fixes
the declaration, with a numeric/alphabetical pixel-order regression added.
Qt 6.9 native testing then caught nested `QJsonArray` brace initializers choosing
copy construction instead of adding array levels. Explicit append calls now
preserve the required matrix Z/Y/X structure across Qt versions. Windows
offscreen GUI tests use installed system fonts rather than placeholder glyphs;
the packaged application still launches with the native Windows platform.
OFL tests write explicit text reports, including when invoked from Python, so
Windows failures remain diagnosable even without a usable stdout handle.

The CTest suite includes engine, OFL fixtures, full interface, high-DPI interface,
Windows-style interface, macOS build script, packaging and offline OFL-schema
checks. The schema suite validates eight generated fixtures plus two input
regressions against OFL 12.6.0. Runtime parsing is bounded but is not full JSON
Schema validation. See [OFL fixtures](OFL-FIXTURES.md) for supported mappings and
conversion limits. OSC, network scheduling, complete Eos behavior and broader
application architecture were not reimplemented by this change.
