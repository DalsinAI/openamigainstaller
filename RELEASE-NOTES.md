# OpenInstaller 1.0.0-rc2

Fixes from running rc1 on AmigaOS 3.2.3:

- Pictures in the panel came out as stripes on RTG screens. They were
  written with the wrong pixel format (LUT8 for ARGB).
- With no `(panel)` the panel was a black box: the logo built into
  OpenInstaller now loads. It is written to `T:` for the datatype and
  deleted when the window closes.
- After a few pages (or after a Help page) the panel stopped being drawn,
  or the picture was drawn over the page. Each page's panel box is now
  handed back before the next page is laid out.

# OpenInstaller 1.0.0

The first release of OpenInstaller, the AmigaChrome Team's Installer for
AmigaOS 3.2 (68040, 68060 or 68080). It runs the Installer scripts you
already have, and adds:

- **(download)**: fetches a file over HTTP or HTTPS with a progress bar
  inside the Installer window, with resume, `(ifmissing)` and a SHA-256
  `(checksum)`. **(netavailable)** says whether there is a network.
- **(panel)**: a picture in the left quarter of the window, in IFF, PNG or
  animated GIF. Without one it shows the AmigaChrome logo.
- A window built with GFOpenLayout, or with GadTools when
  gfopenlayout.library isn't there, in your Font prefs' screen font.

## Installing

Unpack `OpenInstaller-<version>-amiga-68040.lha` and double-click
**Install-OpenInstaller**. The Installer AmigaOS 3.2 already has runs it.
It copies OpenInstaller to `C:`, gfopentls.library to `LIBS:` and the
certificate list to `S:GFOpenTLS/`, then puts the guide and the examples in
a drawer you choose. The Installer you have stays as it is.

## Getting started

Read `Guide.md`, then try the three scripts in `Examples`: each icon
starts `C:OpenInstaller`.

Designed, tested and delivered by The AmigaChrome Team.
