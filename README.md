# TaikoNauts NULM Background

English | [日本語](README.ja.md) | [Install guide](https://36536pu4jt98g3ju095jk.github.io/taikonauts-nulm-background-mod/)

A TaikoNauts ModLoader mod that plays NULM (`.nulm`, LMB) animations as the
upper and lower backgrounds of the play screen. It switches to a fever
background when the soul gauge clears and back again when it drops.

The mod replaces the skin's own background at the point where the game draws
it, so the lane, notes, score and every other element stay on top.

## Requirements

- TaikoNauts ModLoader v1.2.0 or later (base API version 1; no extensions).
- Tested with TaikoNauts 2026.10.01.1 and ModLoader v1.3.1. The fever hook is matched by code, so it can stop matching after a game update.
- NULM animation packs that you provide yourself. **No NULM data or textures are
  included**; they belong to their owners.

## Install

1. Close TaikoNauts.
2. Extract the release ZIP into `TaikoNauts\mods`, which creates
   `mods\nulm-background`.
3. Put NULM packs in the **`Lumens` folder of the skin you use**, for example
   `TaikoNauts\Skins\K-Style\Lumens`. Each pack has its own folder:

   ```text
   Skins\K-Style\Lumens\bg_nomal_a_01\bg_nomal_a_01.nulm
   Skins\K-Style\Lumens\bg_nomal_a_01\bg_nomal_a_01_0.png      (atlas 0)
   Skins\K-Style\Lumens\bg_nomal_a_01\bg_nomal_a_01_1.png      (atlas 1, if used)
   ```

   The folder, the `.nulm` file and the atlas prefix share one name. Atlas
   `_0` to `_15` are loaded when present.
4. Start the game. `modloader.log` reports each loaded layer.

The mod does not include any NULM data or textures. Because the packs live in
the skin, each skin can carry its own set, and switching skins switches the
backgrounds.

## How packs are chosen

When a song starts, the mod looks into the selected skin's `Lumens` folder and
picks one pack at random for each layer, by the pack's name:

| Name starts with | Layer |
| --- | --- |
| `donbg_` | upper background |
| `bg_nomal_` (or `bg_normal_`) | lower background |
| `bg_fever_` | fever background, shown while the gauge is cleared |
| `bg_dai_` | stand drawn over the lower background |

A different pack is chosen from song to song whenever the skin has more than one
for a layer. A layer without a pack in the skin keeps the game's own
background, unless `config.json` names one (below). Packs are loaded while the
song loads, not during play.
## Configuration (optional)

`config.json` is optional. It names fixed packs, used for any layer the skin's
`Lumens` folder has nothing for, and holds the other settings. Packs named here
live in `mods\nulm-background\packs\<pack>\`. Copy `config.example.json` to
`mods\nulm-background\config.json`:

```json
{
  "upper": { "pack": "donbg_a_01_1p", "x": 0, "y": 0 },
  "lower": { "pack": "bg_nomal_a_01", "x": 0, "y": 540 },
  "fever": { "pack": "bg_fever_a_01", "x": 0, "y": 540 },
  "dai": { "pack": "bg_dai_a_01", "x": 0, "y": 540 },
  "clearDetection": "game"
}
```

| Key | Meaning |
| --- | --- |
| `upper` | Upper background. Drawn at `x`, `y` (default `0`, `0`). |
| `lower` | Lower background (default `y` is `540`). Required for `fever` and `dai`. |
| `fever` | Fever background, drawn over `lower` and shown after the gauge clears. |
| `dai` | Stand drawn over `lower` and `fever`. |
| `pack` | Folder name under `packs`. Optional `root` selects a root track; by default the only root track is used. |
| `clearDetection` | How the fever state is decided: `game` (default), `gauge` or `off`. See below. |
| `gauge` | Only for `clearDetection` `gauge`: `tileWidth`, `tileHeight` and `clearTiles` of the drawn gauge. |

Every layer is optional. Leave out `lower` to keep the game's lower
background. Coordinates are in the game's 1920x1080 layout.

Pack naming follows the arcade data: `donbg_a_NN_1p` (upper),
`bg_nomal_a_NN` (lower), `bg_fever_a_NN` (fever) and `bg_dai_a_NN` (stand).
A pack's `upper` and `fever` timelines must carry the labels `init`,
`normal_fever` and `fever_normal`.

## How it works

TaikoNauts draws the play scene into a 1920x1080 render texture and draws each
skin background as one 1920-pixel-wide texture: about 540 pixels tall for the
lower background and about 276 pixels tall for the upper one. The mod watches
`DrawTexturePro`, skips those draws and plays the NULM at the same point of the
frame. Playback restarts at the beginning of every song.

## Fever detection

The fever background follows the game's own clear decision, so it does not
depend on the skin. TaikoNauts calls `Gauge.IsGaugeClear` for the soul gauge;
the mod locates that function by its machine code (not by a fixed address),
wraps it, and records each result the game computes. It never calls game code
itself. If the code is not recognised exactly once, which can happen after a
game update, nothing is patched, the log says so and the fever background is not
used.

`clearDetection` can be changed:

- `game`: the default described above.
- `gauge`: infer the state from the drawn soul gauge, one small tile per filled
  segment. At `gauge.clearTiles` tiles (default 40, the Oni border) the fever
  background is shown. This depends on the skin's gauge artwork and is only a
  fallback.
- `off`: never switch to fever.

Because it follows the game, the background changes every time the gauge
crosses the clear border, as the game's own clear state does.
## Limitations

- Clip masks (`ClipDepth`) and text fields inside a NULM are not reproduced.
- Only the 1P layout is handled.
- Skins that draw their backgrounds in another size are not detected.

## Inspect a NULM

```powershell
build\inspect.exe packs\bg_nomal_a_01\bg_nomal_a_01.nulm
```

prints the frame rate, size, atlas count, root tracks and their labels.

## Build

Requires MinGW-w64 GCC and PowerShell.

```powershell
.\scripts\build.ps1
```

With a game folder that already contains `mods\nulm-background` (config and
packs), `-OriginalRaylib <path to raylib_original.dll>` also renders a frame
offscreen through the real raylib and writes `build\render_smoke.png`. The
script writes `dist\TaikoNauts-NULM-Background-v1.0.0.zip`.

## License

MIT. See [LICENSE](LICENSE).
