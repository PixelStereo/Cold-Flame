# Code and UX review: OFL fixtures and searchable fixture selection

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

The CTest suite includes engine, OFL fixtures, full interface, high-DPI interface,
Windows-style interface, macOS build script, packaging and offline OFL-schema
checks. The schema suite validates eight generated fixtures plus two input
regressions against OFL 12.6.0. Runtime parsing is bounded but is not full JSON
Schema validation. See [OFL fixtures](OFL-FIXTURES.md) for supported mappings and
conversion limits. OSC, network scheduling, complete Eos behavior and broader
application architecture were not reimplemented by this change.
