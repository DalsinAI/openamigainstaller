#!/usr/bin/env python3
"""The archive's GlowIcons, with the AmigaChrome logo as their badge.

    python3 package/amiga/make_icons.py AMICRADLE

AMICRADLE is an AmiCradle checkout (DalsinAI/amicradle): its
scripts/port_icons.py draws the Team's drawer, page and installer icons.
Writes package/amiga/files/Icons/, which build-ci.sh puts in the archive:

  OpenInstaller.info           the drawer (beside it)
  Install-OpenInstaller.info   starts SYS:System/Installer, the classic one
  README.md.info, Guide.md.info, Extras.md.info, LICENSE.info
  Examples.info                the examples' drawer
  Install-Stages.info, Install-Download.info, Install-Both.info
                               start C:OpenInstaller
  Pictures.info                the example pictures' drawer

MIT, Copyright (c) 2026 AmiCradle Team."""
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent
OUT = HERE / "files" / "Icons"


def main() -> int:
    sys.path.insert(0, str(Path(sys.argv[1]).resolve() / "scripts"))
    import amiga_icon
    import port_icons as pi
    badge = pi.Picture.load(ROOT / "assets" / "ac-logo.png")
    nopos = amiga_icon.NO_POSITION
    OUT.mkdir(parents=True, exist_ok=True)

    def icon(name, kind, itype, tool=None, tooltypes=(), stack=4096, drawer=None, x=nopos, y=nopos):
        meta = {"type": pi.TYPES[itype], "defaultTool": tool, "toolTypes": list(tooltypes),
                "toolWindow": None, "stack": stack, "x": x, "y": y, "drawer": drawer}
        p = pi._write(OUT / name, meta, pi.frames(kind, badge))
        print(f"{p.name}: {p.stat().st_size} bytes")

    def window(w, h):
        return {"left": 80, "top": 60, "width": w, "height": h, "flags": 0, "viewModes": 0,
                "currentX": 0, "currentY": 0}

    icon("OpenInstaller.info", "drawer", "drawer", drawer=window(440, 150))
    icon("Examples.info", "drawer", "drawer", drawer=window(420, 130), x=20, y=100)
    icon("Pictures.info", "drawer", "drawer", drawer=window(360, 130), x=330, y=10)
    icon("Install-OpenInstaller.info", "installer", "project", tool="SYS:System/Installer",
         tooltypes=["APPNAME=OpenInstaller", "MINUSER=NOVICE", "DEFUSER=AVERAGE",
                    "LOGFILE=T:Install-OpenInstaller.log"], stack=65536)
    for i, n in enumerate(("README.md", "Guide.md", "Extras.md", "LICENSE")):
        icon(f"{n}.info", "readme", "project", tool=pi.VIEWER, x=20 + i * 100, y=10)
    for i, n in enumerate(("Install-Stages", "Install-Download", "Install-Both")):
        icon(f"{n}.info", "installer", "project", tool="C:OpenInstaller",
             tooltypes=[f"APPNAME={n[8:]}", "DEFUSER=AVERAGE"], stack=65536, x=20 + i * 100, y=10)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
