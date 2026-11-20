# Clocky PCB — JLCPCB Fabrication Instructions

Bare PCB fabrication only. No assembly, and no stencil: every part is
through-hole except the touch plate, and all of it is hand-soldered.

## Files to upload

`Clocky-JLCPCB-gerbers.zip` (in this folder). It contains:

| File | Layer |
|---|---|
| `Clocky-F_Cu.gtl` | Top copper |
| `Clocky-B_Cu.gbl` | Bottom copper |
| `Clocky-F_Mask.gts` | Top solder mask |
| `Clocky-B_Mask.gbs` | Bottom solder mask |
| `Clocky-F_Silkscreen.gto` | Top silkscreen |
| `Clocky-B_Silkscreen.gbo` | Bottom silkscreen (blank) |
| `Clocky-Edge_Cuts.gm1` | Board outline |
| `Clocky-PTH.drl` | Plated drill holes (Excellon, metric) |
| `Clocky-NPTH.drl` | Non-plated holes (empty: the board has none) |

## Order form settings

| Setting | Value |
|---|---|
| Base material | FR-4 |
| Layers | 2 |
| Dimensions | 100 × 35 mm |
| PCB quantity | 5 (minimum) |
| Product type | Industrial/Consumer electronics |
| Different design | 1 |
| Delivery format | Single PCB (no panel) |
| PCB thickness | 1.6 mm |
| PCB color | Any (green is cheapest and fastest) |
| Silkscreen | White (default for the chosen mask color) |
| Surface finish | HASL lead-free, or ENIG (see note 3) |
| Outer copper weight | 1 oz |
| Via covering | Tented (the board has no vias, so this has no effect) |
| Min via hole size / diameter | Default (no vias) |
| Board outline tolerance | ±0.2 mm (regular) |
| Confirm production file | No |
| Mark on PCB | Remove mark (see note 2) |
| Electrical test | Flying probe, fully test |
| Gold fingers, castellated holes, impedance control, edge plating | No |
| PCB Assembly | No |
| SMT stencil | No |

## Design summary (for checking the upload)

| Parameter | Design value | JLCPCB 2-layer minimum |
|---|---|---|
| Board size | 100 × 35 mm, rectangular, sharp corners | 6 × 6 mm |
| Narrowest track | 0.8 mm (power tracks 1.2 mm) | 0.127 mm |
| Smallest copper clearance | 0.25 mm (ground pour 0.3 mm) | 0.127 mm |
| Copper to board edge | ≥ 0.5 mm | 0.3 mm |
| Smallest drilled hole | 0.8 mm plated | 0.2 mm |
| Drill sizes | 0.8, 0.9, 1.0 mm (26 plated holes in total) | — |
| Vias | None | — |
| Smallest silkscreen text | 1.0 mm high | 0.8 mm |

The KiCad design-rule check reports no errors and no unconnected items.

## Notes

1. **The large copper area at the right-hand end is a touch plate. It must stay
   covered by solder mask.** It is the clock's capacitive touch button, and the
   mask over it is intended. The only mask opening in that area is the 4 mm
   round pad (TP1) inside it, which is also intended. Do not open the mask
   over the rest of the plate.
2. **Order number:** choose "Remove mark". The front silkscreen is full of
   artwork (a clock face, "CLOCKY", "TOUCH"), and a randomly placed order
   number could land on the touch plate or the artwork. The board has no
   "JLCJLCJLCJLC" placeholder text, so "Specify a location" will not work.
3. **Surface finish:** HASL lead-free is fine. ENIG gives a flatter, more even
   surface on the exposed TP1 pad and solders well. Either works electrically.
4. **No vias.** All connections between the two layers go through the plated
   holes of the through-hole parts. This is intended.
5. **Copper-free areas.** The ground pour stops deliberately in two places:
   - a strip at x ≈ 127–134 mm, under the plug-in Wi-Fi module's antenna;
   - around the touch plate and its trace.

   These gaps are intended.

## Text for the order "Remark" field

Paste this in if the order form offers a remark or notes field:

> Bare PCB only, no assembly. Please keep solder mask over the large copper area at the right end of the board (capacitive touch plate). The only mask opening there is the 4 mm round pad, which is intended. No vias by design. Please remove the order number mark.

## Before you pay

- [ ] JLCPCB's Gerber viewer shows the board at 100 × 35 mm, with both copper layers, both masks and the top silkscreen.
- [ ] The touch plate shows as copper **under** mask, with only the 4 mm round opening.
- [ ] The drill view shows 26 plated holes and no non-plated holes.
- [ ] Mark on PCB is set to "Remove mark".
- [ ] PCB Assembly and stencil are both off.
