# Building a cursor theme from Figma artwork

smooly auto-discovers any folder of `.cur`/`.ani` files under `cursors/`. This
pipeline turns cursor **PNGs** (e.g. exported from a Figma cursor pack) into a
proper `cursors/<Theme>/<Size>/` theme with an `install.inf`, using a
self-contained converter (`png2cur.exe`) — **no Python/ImageMagick needed**.

## 1. Export the artwork from Figma

Open the cursor file, e.g.
[Cursors for PC (mac + win)](https://www.figma.com/community/file/1218903777001900990/cursors-for-pc-mac-win)
(CC BY 4.0 — see licensing note below). Then **Duplicate to your drafts** so you
can select and export frames.

For each cursor role you want, select its frame and **Export → PNG** at a high
scale (**4x**, or set width to 256px). Transparent background. High resolution
matters — the build downscales to each size crisply.

Save the PNGs **into this folder** (`tools/cursor-art/`) using the names in
`manifest.ini`:

| Role file      | What it is                    | Windows cursor |
|----------------|-------------------------------|----------------|
| `pointer.png`  | the normal arrow              | Normal Select  |
| `link.png`     | pointing hand                 | Link Select    |
| `text.png`     | I-beam                        | Text Select    |
| `help.png`     | arrow + question mark         | Help Select    |
| `cross.png`    | crosshair / precision         | Precision      |
| `unavailable.png` | "no" / not-allowed         | Unavailable    |
| `move.png`     | 4-way move                    | Move           |
| `vert.png`     | ↕ resize                      | Vertical Resize|
| `horz.png`     | ↔ resize                      | Horizontal     |
| `dgn1.png`     | ⤡ resize (NW-SE)              | Diagonal 1     |
| `dgn2.png`     | ⤢ resize (NE-SW)              | Diagonal 2     |
| `alternate.png`| up arrow                      | Alternate      |

Only include what the pack has — **any missing role falls back to the Windows
default**, so a partial pack is fine.

Animated roles (spinner `busy`, working `work`) can't come from a PNG. Drop
ready-made `.ani` files in and list them under `[copy]` in `manifest.ini`, or
leave them out to keep Windows' default spinners.

## 2. Set names + hotspots

Edit `manifest.ini`:
- `[theme] name` — the theme name shown in smooly.
- `[roles]` — `role = file.png, hotspotX, hotspotY`. The **hotspot** is the pixel
  that actually points (arrow tip, I-beam centre, fingertip), given in a **32px
  reference space** and scaled automatically per size. Defaults are pre-filled.

## 3. Build

```powershell
powershell -ExecutionPolicy Bypass -File tools\build-cursors.ps1
```

This writes `cursors/<name>/Regular|Large|Extra-Large/*.cur` + `install.inf`.
Restart smooly (or re-open Settings) and pick the theme on the **Pointer** page.

## Licensing (important)

Free Figma Community files default to **CC BY 4.0**: you may bundle and
redistribute them, including commercially, **but you must credit the author**.
Before shipping a theme:
1. Confirm the file's own license chip says CC BY 4.0 (creators can set custom terms).
2. Add a credit line to `cursors/ATTRIBUTION.txt` (the build prints a reminder).
3. If the designs imitate Apple's macOS cursors, prefer a neutral theme name
   (e.g. "Aqua") — Apple's cursor *design/trademark* is separate from the Figma
   file's license.
