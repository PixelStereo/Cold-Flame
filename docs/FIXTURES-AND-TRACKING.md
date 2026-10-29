# Fixtures, patch and tracking (0.7)

A console channel represents one fixture. Its intensity remains visible in the channel grid; its other parameters belong to that same channel in Parameters and Tracking. This follows the Eos [channel/fixture concept](https://www.etcconnect.com/WebDocs/Controls/EosFamilyOnlineHelp/en/Content/06_Patch/About_Patch.htm). Shared reusable looks and repeated sequence occurrences retain Cold Flame's Congo/Cobalt-style model.

## Patch a rig

1. Open **Patch**. Select a fixture/mode in its row, then enter its DMX start address, such as `2.001`. Several comma-separated starts drive identical copies of that fixture. Clear the address to unpatch, retaining the channel and its stored data.
2. Use **Batch…** to add or repatch a range. Its stride is the fixture footprint; the next fixture moves to the next universe when it will not fit. Batch is a draft operation.
3. Check the validation message. Both owners of an overlap are highlighted. **Apply patch** is unavailable until the whole draft is valid. Failed validation leaves the active patch unchanged.
4. **Art-Net broadcast is active at application startup**. In **Network**, adjust its interface/range or use Stop all. Existing broadcast settings are reused; a default route is added if needed. Other saved routes remain off. Art-Net uses broadcast; sACN supports multicast. Opening another show later disables outputs.

Profiles include Dimmer 8-bit, Dimmer 16-bit, RGB, RGBW, Dimmer + RGB and an explicitly generic 11-slot RGBW moving head. The generic mover is a layout example, not a claim that every moving head uses those slots. Choose the actual manufacturer's GDTF profile and the exact mode configured on the fixture.

Validation reserves every slot from a start address to the end of the mode's footprint, including gaps and fine bytes. It detects collisions between channels and between a channel's own duplicate starts. A fixture cannot cross a universe boundary. Patch universes range from 1 to 63999; network routes separately validate their Art-Net or sACN wire range. Show loading applies the same checks. These addressing rules follow Eos's documented [overlap and universe-boundary restrictions](https://www.etcconnect.com/WebDocs/Controls/EosFamilyOnlineHelp/en/Content/06_Patch/01_Patching_Conventional_Fixtures/Patching_By_Address.htm).

## GDTF and patch transfer

**Patch tools → Import GDTF profile…** adds a `.gdtf` profile to the patch draft's library. Choose its mode per channel or in Batch, then Apply patch. GDTF is the shared industry fixture format used by grandMA3; this is not an importer for proprietary MA show files.

Supported modes have direct single-break offsets, one logical attribute per DMX channel, at most one physical intensity parameter, and 8/16/24/32-bit resolution. Byte order follows the GDTF Offset list, including nonconsecutive offsets. Defaults and Snap are honored. Parameters use normalized DMX percentages; physical-unit conversion, channel-function selection tools and fixture-specific calibration are not implemented.

Modes requiring geometry-reference expansion, multiple DMX breaks, virtual channel relations, multiplexed logical channels, mode masters or multiple intensity cells are marked unsupported and cannot be applied. This is a supported subset of GDTF, not full GDTF or Eos fixture coverage. Import preserves the original archive; exporting an imported profile returns those original bytes. Built-in generic profiles can also be exported as GDTF.

**Export applied patch…** writes a versioned `.cfp` JSON patch with channel names, addresses, assignments and fixture profiles. **Import patch…** loads a compatible file into the draft for review and Apply. It preserves existing channel IDs and rejects profile identity conflicts or changes that would reinterpret stored fixture parameters. This is Cold Flame patch transfer, not MVR interchange. `.cfl` show format 3 stores the same fixture data plus cue/sequence content and tracking overrides. Format 1 and 2 shows remain readable; older builds cannot open format 3.

Changing start addresses keeps parameter identities and is supported with existing cues. Replacing a fixture type/mode once it has stored or live non-intensity data is rejected: use a new channel. Parameter migration between different fixture types remains future work. Undoing a fixture-type definition releases incompatible live non-intensity controls so their internal IDs cannot be reinterpreted as other parameters.

## Program fixture parameters

Open **Parameters**, then select the fixture. Double-clicking a dimmer tile also opens Parameters. The panel exposes intensity and each DMX parameter, its resolution and manual status. Drag a numeric field sideways, or click to type. Home parameters loads non-intensity defaults; Release fixture clears its manual values except captured controls.

Record stores the unmastered look, including fixture parameters and their defaults; Record Only stores manual controls. Update, shared-cue content editing, local tracking edits, fades and show files preserve parameter data. Grand master and blackout affect intensity, leaving position/color/beam values unchanged. RGB/RGBW profiles without a physical dimmer use the channel intensity to scale additive colors. Submasters remain HTP intensity only. Capture, Park, Home and selected-channel Release include the fixture's parameters; Park remains excluded from recordings.

## Choose a color from the channel grid

RGB/RGBW and CMY fixtures show a color swatch on their channel tile. Click that swatch, double-click the tile or right-click and select **Choose color…**. Normal clicks elsewhere still select channels. **Fixture parameters…** in the context menu opens the numeric parameter editor.

The picker previews manual color changes live without changing intensity, shutter, position or other controls. Cancel restores the previous manual color values and palette references; OK retains the chosen color for Record/Update. Swatches show the current programmed color independently of intensity and blackout. A warm border identifies manual intensity or color control.

RGBW mixing extracts the common RGB component into White. CMY converts to complementary percentages. On hybrid RGB/CMY fixtures the additive system is used with CMY open; other additive emitters are cleared. Repeated emitter attributes receive the same color. This is normalized DMX mixing, without vendor color calibration. Fixtures with only discrete color wheels remain editable through Parameters; their wheel slots are not mapped to arbitrary RGB colors.

## Track a channel through a sequence

Open **Tracking** beside Sequences, choose the sequence using its selector (synchronized with the sequence tabs). Rows are channel/parameter controls; columns are sequence steps with the shared cue number and name. Filter to all channels, selected channels or controls used in this sequence.

- **M**: a move instruction in this occurrence or its shared cue.
- **T**: a value inherited from a previous step.
- **B**: a channel/parameter block.
- **—**: no instruction; the control uses its home value.

Double-click a cell to edit its percentage. Edits save immediately to the occurrence and affect future recalls, leaving running fades unchanged. A normal edit tracks forward until the next explicit move or block. **Cue Only** preserves the following step if it was tracking that control.

Right-click cells to **Track from previous**, **Block channel / parameter**, **Unblock channel / parameter**, or **Go to source instruction**. Clearing a cell also removes its move. Removing a move inherited from a shared cue creates a local suppression, without deleting data from other uses of that cue. A full-look library cue explicitly stores all controls, so it stops incoming tracking until those moves are removed locally. Record Only can create sparse shared cues.

Enable **Trace edit** to edit the move or block that supplies the chosen cell. It traces into a block, but stops there. A newly active level on a zero/null track is written at the selected step; **Trace zeros** forces it backwards (the counterpart of Eos Trace Trace). Cue Only preserves the step after the selected occurrence even when the edited source is earlier. These behaviors are based on [Eos Tracking and Block](https://www.etcconnect.com/WebDocs/Controls/EosFamilyOnlineHelp/en/Content/04_System_Basics/Important_Concepts.htm) and [Update Using Trace](https://www.etcconnect.com/WebDocs/Controls/EosFamilyOnlineHelp/en/Content/12_Cues_and_Cue_Lists/07_%5BUpdate%5D/Update_Using_Trace.htm). Category filters, per-parameter Assert and full Eos range-edit/update semantics remain incomplete.
