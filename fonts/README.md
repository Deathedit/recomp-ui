# The fonts

Embedded into the library at build time (`cmake/embed_files.cmake`) and loaded by
`src/style.cpp`.

| File | Source | Licence |
|---|---|---|
| `IBMPlexSans-Regular.ttf`, `IBMPlexSans-SemiBold.ttf`, `IBMPlexMono-Medium.ttf` | IBM Plex v6.4.2 (github.com/IBM/plex, tag `v6.4.2`, `IBM-Plex-*/fonts/complete/ttf/`), **unmodified** | SIL Open Font License 1.1, `OFL.txt` |
| `MaterialSymbolsRounded-Subset.ttf` | Material Symbols Rounded (github.com/google/material-design-icons, commit `49d4db35df873165d6bd6ba09b063c7dafbac2f4`, `variablefont/MaterialSymbolsRounded[FILL,GRAD,opsz,wght].ttf`), **modified**: see below | Apache License 2.0, `LICENSE-Apache-2.0.txt` |

- Plex is shipped as released, not subset: the OFL reserves the name "Plex", and a
  subset is a Modified Version that couldn't keep it. The three faces are about 560 KB.
- Material Symbols Rounded was changed (Apache 2.0, section 4b): a static instance of the
  variable font (stb_truetype reads only the default instance), cut to 22 icons, without
  ligatures:

  ```
  fonttools varLib.instancer "MaterialSymbolsRounded[FILL,GRAD,opsz,wght].ttf" FILL=0 GRAD=0 opsz=24 wght=400 -o static.ttf
  pyftsubset static.ttf --unicodes=U+EF5B,U+E050,U+F0C6,U+EA28,U+E9E4,U+E868,U+E5CB,U+E5CC,U+F053,U+E88E,U+F0BE,U+E412,U+E3AF,U+E14D,U+E2C8,U+E312,U+F083,U+E5CD,U+EA23,U+E899,U+E323,U+EB2F --no-layout-closure --layout-features= --output-file=MaterialSymbolsRounded-Subset.ttf
  ```

  (fontTools 4.55.3.) The icons: monitor, volume_up, flag, sports_esports, speed,
  bug_report, chevron_left, chevron_right, restart_alt, info, check_circle, photo_camera,
  camera, content_copy, folder_open, keyboard, warning, close, emoji_events, lock, mouse,
  lan (`include/rui/style.h`, `ICON_*`).

## Other icons

A game that wants icons the subset doesn't have makes its own subset the same way (the
codepoints are in the font's `.codepoints` file at the same commit; keep the 22 above, the
framework draws some of them) and points `RUI_ICON_FONT` at it before `add_subdirectory`
(`CMakeLists.txt`). Its codepoints must lie in U+E000-U+F8FF. Ship its licence with it.

A release ships both licence texts with the program's third-party notices.
