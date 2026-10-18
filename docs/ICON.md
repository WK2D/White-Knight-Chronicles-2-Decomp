# Project icon

The approved project icon is an imagegen-created WKCII lettermark: silver
fantasy serif WKC, a red Roman II, a vivid blue glow, a polished gold
rounded-square border and a matching gold divider. No downloaded game artwork
or font files are included.

`assets/wkcii.png` is the approved master image. `assets/wkcii.ico` contains
16, 24, 32, 48, 64, 128 and 256-pixel Windows icons. Repackage the master PNG
on Windows with:

```powershell
./tools/make-icon.ps1
```

Windows builds embed the ICO through `app.rc`. Explorer and the SDL window
use that resource automatically. Other platforms can use the PNG;
platform-specific application bundles remain future work.

The generation direction was: a centered silver WKC over a red Roman II,
refined classical fantasy serifs, luminous cobalt/cyan glow behind the text,
a midnight navy tile, polished gold border and divider, and transparent exterior corners.
Only the WKCII lettering appears; no characters, weapons, subtitle or watermark.
