# Fixture and patch review — 0.10

Reviewed: native GDTF export, fixture parameter identities, patch draft/application and the new definition/conflict workflows. This is a focused review, not an audit of the entire console.

| Finding | Resolution | Verification |
|---|---|---|
| Native GDTF export could shrink a declared footprint when its final slots had no parameter. Re-import derives footprint from occupied offsets. | Reject a lossy export; the definition editor calculates footprint from offsets and supports explicit NoFeature reservations. | `gdtfExportRejectsLossyAndInvalidNativeDefinitions` tests refusal and reserved-slot round trip. |
| Native export assumed valid parameter widths and intensity semantics. Invalid models could reach byte-width arithmetic or change intensity interpretation on re-import. | Validate before generating XML/DMX defaults; reject inconsistent Dimmer/intensity data. | Invalid width/offset and intensity-semantic export tests. |
| Editing a profile under the same ID could reinterpret ordinal parameter keys. | Assigned, vendor and built-in profiles are copied; patch validation independently rejects changes to assigned definitions under an existing ID. Compare archives as bytes instead of repeatedly base64-encoding them per channel. | Used-profile copy test preserves the original and rejects reassignment when parameter data exists. |
| Overlaps had only an error message, with no way to offer a controlled unpatch operation. | Add an explicit preview and confirmation for the most recently entered patch or Batch. Remove whole conflicting starts, preserving other copies and channel/cue data. | Conflict preview, Cancel, confirmation, draft isolation, surviving copy and cue preservation tests. |

Editor tests also cover duplicate byte offsets, raw home limits, multiple modes, reversed/nonconsecutive 32-bit offsets, Snap, show/GDTF round trips and recovery after deleting all parameter rows.

Remaining fixture-engine limitations are listed in [FIXTURE-EDITOR.md](FIXTURE-EDITOR.md). No full Eos or full GDTF conformance is claimed.
