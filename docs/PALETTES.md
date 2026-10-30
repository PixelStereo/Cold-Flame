# Palette types and cue references

Eos palettes are independent, referenced show objects. A cue stores references to their parameter data. Changing the palette changes every place that references it. It is not a separate palette copied into each sequencer step. Official reference: [Eos About Palettes](https://www.etcconnect.com/WebDocs/Controls/EosFamilyOnlineHelp/en/Content/10_Palettes/About_Palettes.htm).

| Eos type | Typical content | Cold Flame 0.12 |
|---|---|---|
| Intensity Palette | Dimmer levels | Basic intensity references |
| Focus Palette | Position: pan and tilt | Not implemented; absolute position parameters can already be recorded in cues |
| Color Palette | Color parameters, such as RGB, CMY and color wheels | Normalized RGB/RGBW/CMY references; no calibrated color or wheel-slot interpretation |
| Beam Palette | Gobo, zoom, iris and other beam parameters | Not implemented; supported fixture parameters can already be recorded as absolute data |

An Eos **Preset** can combine parameter categories. It is not a fifth palette type. Cold Flame's reusable shared cues are complete looks following its Congo/Cobalt-inspired model; they are not a complete Eos Preset implementation.

## Record and update RGB fixtures

A newly programmed RGB fixture starts with white (`#FFFFFF`) and independent intensity. White comes from an immutable hidden reference: it has no row, edit button or delete action in the palette bank. This is a requested Cold Flame default, not a claim about the Eos factory configuration. Legacy shows retain their recorded absolute color values.

Click a channel's color swatch or right-click **Choose color…**. Preview is live and Cancel restores previous manual data. OK creates or reuses a shared color palette; white uses the hidden default. Preview does not create palette entries.

**Record** and **Record Only** store intensity and color together for touched RGB fixtures. Their values remain separate: intensity is a level; color parameters reference a Color Palette. Choosing a color does not raise intensity. An intensity-only Record Only operation includes the fixture's current color, including hidden default white.

To change an existing cue, choose the fixture color and press **Update** on the selected step, or **Update from manual** in the Cue library. Only manually changed controls are merged, so a color-only update retains intensity. Choosing a new color rebinds the cue to the chosen palette; it does not silently recolor another palette used by other cues. Numeric parameter or Blind edits can make individual controls absolute.

A shared stored cue carries the color reference. Every sequence occurrence uses it by default. **Cue Only** writes local step overrides and preserves the next step's tracked values. An existing local color override remains local on subsequent Update. The source/reference can be inspected in **Parameters**, the cue content editor and Tracking cell help. Hidden white is not presented as a numbered user palette.

## Assign a palette directly to a cue

In **Palettes → Color**, select a palette and choose **Assign to cue…**. Choose the stored cue and whether to limit the change to selected fixtures. Without a fixture selection, all color-capable fixtures already represented in the cue receive the palette. The cue's intensity and unrelated parameters remain unchanged.

The same dialog is available by right-clicking a cue in the Cue library or a referenced step in a sequence. It changes the shared stored cue, so all its occurrences use that assignment. Existing local step overrides retain priority. To apply a palette only to one occurrence, use **Apply to selection**, select the step, enable **Cue Only**, then **Update**.

## Edit a shared palette

**Palettes → Color → Edit color…** changes the selected palette's definition. All stored references, including in other cues and sequences, resolve to the new color on future recall. Manual controls referencing it change immediately. Running sequence output and fade targets retain their current numeric targets until recalled; this is an implementation limit. Rename a palette in the Name cell. **Delete unused** refuses to remove referenced palettes.

Eos offers several Update styles, including updating references or making data absolute. Their defaults depend on console/user configuration; Cold Flame does not implement the full Update dialog. See [Eos Update Dialogue Box](https://www.etcconnect.com/WebDocs/Controls/EosFamilyOnlineHelp/en/Content/12_Cues_and_Cue_Lists/07_%5BUpdate%5D/Update_Dialogue_Box.htm).

Color references survive recordings made from a completed playback, save/load, cue/step edits, channel blocks and definition Undo/Redo. Recording a fade in progress captures intermediate values rather than substituting the final palette target. Show format 4 introduced color palette definitions and references. The current format 7 also stores submaster mode/timing, fixture cell identities and original OFL sources. Formats 1–6 remain readable; earlier application versions cannot read format 7.

### References in cue and sequence lists

The **PALETTES** column beside the cue name shows each stored color (`CP`) and intensity (`IP`) palette once. Color references include the palette name. Sequence rows distinguish **Cue:** references from the shared look and **Step:** references from local overrides or blocks. A fully overridden or suppressed shared reference is omitted; partial overrides show both. These columns show instructions stored at that step, rather than palettes tracked from earlier steps. Hover a cell to read the affected controls in the footer. Reserved white remains hidden.

For multicell fixtures, the channel-grid picker applies a palette to all color-capable cells; its swatch represents the first color-capable group. **Parameters → cell selector → Choose color…** applies only to that cell or group. References still live in cue parameter data, with ordinary local step override behavior.
