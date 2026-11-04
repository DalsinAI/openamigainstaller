# OpenInstaller's extras

OpenInstaller runs Installer scripts as the OS 3.2 Installer (47.19) does,
and adds three commands. A script that must still run on Commodore's
Installer tests for them first: `@openinstaller` is OpenInstaller's version
(`1<<16|0` for 1.0) there, and 0 on the Installer.

```
(if @openinstaller (panel "Pictures/Welcome.iff"))
```

## (download)

```
(download (source "url") (dest "file or drawer")
          [(ifmissing)] [(checksum "sha256")]
          [(prompt "text")] [(help "text")] [(confirm [level])]
          [(optional "nofail")] [(safe)])
```

Fetches a file over HTTP or HTTPS with the built-in libcurl
(gfopentls.library gives HTTPS). The page shows the Installer's own bar,
in the window, with the bytes so far against the size and the Abort
button, so the install stays in sequence: the next statement runs when the
file is there.

- `dest` a drawer: the file keeps the name the address ends with.
- The file is written as `NAME.part` and renamed when it's all there; a
  download that stops is taken up where it stopped, up to four tries.
- `(ifmissing)`: nothing is fetched when the file is already there.
- `(checksum "...")`: the file's SHA-256 in hex must match, or it's
  deleted and the download fails.
- As with `copyfiles`, a failure stops the install unless
  `(optional "nofail")`; then `@ioerr` says why and the result is 0.
- In pretend mode nothing is fetched unless `(safe)`.

Returns 1 when the file is there afterwards.

## (netavailable)

1 when a TCP/IP stack is running (bsdsocket.library opens), so a script
can offer the download or leave it out.

## (panel)

```
(panel "picture" [(caption "text")])
```

The window has a panel down its left side, a quarter of its width. It shows
the AmigaChrome logo until a script names a picture, and again after
`(panel "")`. Pictures are read with datatypes: IFF ILBM, PNG, GIF and any
other picture a datatype reads. An animated GIF plays when
gfopengif.datatype is installed (it gives the frames); with another GIF
datatype its first frame shows. The picture is scaled to fit, keeping its
shape. The caption is shown under it.

Change the picture as the install goes on: one for each stage, or any
pictures you like.

Returns 1, or 0 when the picture isn't there (the panel stays as it was).
