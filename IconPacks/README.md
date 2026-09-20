# DC++ icon packs

Copyright (C) 2026 iceman50

## Collection

![Dark, Aurora, Copper, Paper and Neon Circuit previews](../docs/icon-packs/Collection.png)

| Pack | Appearance | Design | Full preview |
| --- | --- | --- | --- |
| **Dark 2.0** | Dark | Cool slate, soft blue, restrained two-tone fills | [86 icons](../docs/icon-packs/Dark.png) |
| **Aurora 1.0** | Dark | Teal and violet with richer translucent fills | [86 icons](../docs/icon-packs/Aurora.png) |
| **Copper 1.0** | Dark | Warm copper, sage and opaque matte fills | [86 icons](../docs/icon-packs/Copper.png) |
| **Paper 1.0** | Light | Deep blue ink, muted accents and light washes | [86 icons](../docs/icon-packs/Paper.png) |
| **Neon Circuit 2.0** | Dark | Electric cyan, magenta and violet; luminous strokes and circuit silhouettes | [86 icons](../docs/icon-packs/Neon-Circuit.png) |

All five packs cover all **86 application icons**. Their
85 interface glyphs are original geometric artwork with consistent rounded
strokes, semantic accent colors, transparent backgrounds and separate status
badges. Every application logo uses the same enlarged three-orb motif without
the white ring or backing. Two glossy orbs match the pack's palette, alongside
one graphite orb, on a transparent background. Dark uses soft blue, Aurora teal,
Copper warm copper, Paper deep ink blue, and Neon Circuit electric blue.
All application logos have the same eleven 32-bit sizes as the interface glyphs.
[Compare all five logos at native sizes](../docs/icon-packs/Orb-Logos.png).

Neon Circuit 2.0 replaces the old overlapping glyphs with larger, consistent
silhouettes, separate state badges, and distinct connected/disconnected symbols.
Its custom folder, document, network, chat, transfer and plugin shapes use cyan
with violet and magenta details. At 24 px and above, a pale stroke center and a
single composited halo give a luminous finish. The 16-22 px frames omit the halo
and highlight so tiny symbols remain crisp. Offline states use muted slate;
green, amber and pink identify success, caution and errors.

[Compare Neon Circuit 1.3 and 2.0 at native sizes](../docs/icon-packs/Neon-Circuit-Comparison.png).

Every redesigned interface icon contains **16, 20, 22, 24, 28, 32, 40, 48, 64,
128 and 256 px** frames. Small sizes use heavier main strokes, lighter badge
strokes and simplified detail. Frames through 128 px are 32-bit BGRA DIBs with
matching transparency masks; 256 px frames are lossless RGBA PNGs. Each frame is
rendered independently at its target size with supersampled antialiasing.

The full previews show the actual packaged images at 16, 24 and 32 px. View them
at 100% zoom when assessing small-size clarity.

All five packs use two curved arrows for **Reconnect**, a window with a clock
badge for **Recent windows**, and a single circular arrow for **Refresh file
list**. Each arrow is a filled silhouette with a constant-width band flowing
into a solid triangular head, avoiding overlapping stroked joins.
[Compare these actions at 16-64 px](../docs/icon-packs/Window-Actions.png).

`UserBot.ico` is a transparent **top-left robot overlay**. `UserOp.ico` and
`UserReg.ico` occupy the top-right, `UserNoCon.ico` the bottom-left, and
`UserNoSlot.ico` the bottom-right. They contain no full user silhouette, so
`WinUtil::mergeIcons` can layer them over a user or hub at the same origin.
`User.ico` and `UserAway.ico` remain complete base images; the user image list
composes `User.ico` with the bot overlay for bot entries. Rebuild the application
to include that bot-composition change.
[Inspect standalone and composed overlays](../docs/icon-packs/User-Overlays.png).

## Selection

Open **Settings > Appearance > Icons**, select the **Application icons** tab, and choose an
**Icon package**. Dark is selected automatically when automatic icon selection
and dark appearance are active. Select **Embedded icons (EXE)** to use the
executable's original icons in either appearance, overriding automatic pack
selection. Choose Paper explicitly for light appearance;
Aurora, Copper and Neon Circuit are intended for dark appearance. High-contrast mode continues
to use compiled icons and Windows system colors.

The application searches the per-user `IconPacks` directory first, then the
application-local directory. A per-user package with the same filename overrides
the bundled one. Existing native image lists may retain copied icons until
controls are recreated; restart DC++ to refresh every control after switching or
replacing a pack.

Hub user lists have an **Icons** size selector beside the user filter, with
16, 20, 24, 28, 32, 40 and 48 px choices (scaled for the display DPI).
The global default is set under **Experimental > Interface and themes > Hub user
lists**. Each favorite hub stores its own override, editable in Favorite Hub
Properties or directly in its user-list dropdown. **Default** restores inherited
settings (global, or the hub group's override if present). Changes apply immediately;
changing one hub's size does not change other hubs or the global default. A
non-favorite hub retains its choice for the current window; adding it to Favorites
also saves that choice. Rows grow to accommodate the icons while status overlays
retain their corners.
Native light-mode menus display package and embedded icons, including after
switching between light and dark appearance.

## Regeneration and verification

Run from the repository root using **Windows PowerShell 5.1**:

```powershell
# Rebuild all five packs, or choose one with -Pack Neon-Circuit.
powershell -NoProfile -File scripts/generate-icon-packs.ps1

# The original Dark generator entry point is also supported.
powershell -NoProfile -File scripts/generate-dark-icon-pack.ps1

# Render contact sheets from the ZIP/ICO output.
powershell -NoProfile -File scripts/preview-icon-packs.ps1
powershell -NoProfile -File scripts/preview-icon-actions.ps1
powershell -NoProfile -File scripts/preview-icon-overlays.ps1
powershell -NoProfile -File scripts/preview-neon-logo.ps1
powershell -NoProfile -File scripts/preview-orb-logos.ps1

# Python 3, standard library only; runs the native Windows ICO loader.
python scripts/verify-icon-packs.py
```

The artwork and palettes live in
[`scripts/icon-packs/IconPackRenderer.cs`](../scripts/icon-packs/IconPackRenderer.cs).
Generation requires only Windows PowerShell and System.Drawing. Unknown source
icons fail generation so missing artwork cannot silently enter a pack. ZIP entry
order, timestamps and metadata are fixed; rebuilding on the same Windows graphics
runtime produces identical archives. The validator checks resource coverage,
manifest mappings, frame bounds, alpha/mask agreement, transparent margins,
application-logo transparency and palette, and Windows `LoadImageW` at every supplied size.

The dedicated `scripts/generate-neon-circuit-icon-pack.ps1` entry point also
rebuilds Neon Circuit. Its additional artwork and lighting live in
[`NeonCircuitArtwork.cs`](../scripts/icon-packs/NeonCircuitArtwork.cs), and its
shared application-icon packaging lives in
[`OrbApplicationIcon.cs`](../scripts/icon-packs/OrbApplicationIcon.cs).
Use `python scripts/verify-icon-packs.py --pack Neon-Circuit` to validate only
that pack, including the logo's transparency, frame sizes and orb colors.
The five editable PNG masters live in
[`artwork`](../scripts/icon-packs/artwork), named `<Pack>-DCPlusPlus.png`
(Neon Circuit uses `Neon-DCPlusPlus.png`). They were edited from the original orb
artwork using imagegen; regeneration uses these checked-in assets and does not
call image generation again.

## Package format

A `.dcico` file is a ZIP archive directly under `IconPacks`, containing `info.xml`
and multi-image Windows `.ico` assets. The manifest root is `<dcico>` with `Name`,
`Version`, optional `Scheme`, and an `Icons` list. Each entry maps a compiled
resource filename to an archive member:

```xml
<Icon Name="Search.ico" File="icons/Search.ico"/>
```

A missing, oversized, invalid or incomplete asset falls back to its compiled
resource. Distribution scripts require all bundled packs, and the installer
removes only explicitly named bundled packs during uninstall.
