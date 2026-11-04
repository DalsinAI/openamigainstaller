#!/bin/sh
# OpenInstaller's own commands: (download) from a web server on this
# machine (python3's), and (panel). Prints PASS or FAIL for each check.
set -u
: "${PRG:?}" "${WORK:?}"
V=$WORK/vol
rm -rf "$WORK"
mkdir -p "$V/Work" "$V/RAM" "$WORK/www/files" "$WORK/run"
cd "$WORK/run"
export OI_VOLUMES="$V"

check() {
    name=$1
    shift
    if "$@"; then echo "PASS $name"; else echo "FAIL $name"; fi
}

has() {     # has TEXT PATTERN
    printf '%s\n' "$1" | grep -q -- "$2"
}

head -c 300000 /dev/urandom > "$WORK/www/files/big.lha"
printf 'hello\n' > "$WORK/www/files/small.txt"
SUM=$(sha256sum "$WORK/www/files/small.txt" | cut -d' ' -f1)
printf 'GIF89a' > "$V/RAM/pic.gif"

if ! "$PRG" --has-curl 2>/dev/null | grep -q yes; then
    echo "PASS download (skipped: built without curl)"
else
    PORT=$(python3 -I -c 'import socket; s=socket.socket(); s.bind(("127.0.0.1",0)); print(s.getsockname()[1])')
    (cd "$WORK/www" && exec python3 -I -m http.server "$PORT" --bind 127.0.0.1) > "$WORK/http.log" 2>&1 &
    SRV=$!
    trap 'kill $SRV 2>/dev/null' EXIT
    i=0
    while ! python3 -I -c "import urllib.request; urllib.request.urlopen('http://127.0.0.1:$PORT/')" 2>/dev/null; do
        i=$((i + 1)); [ $i -gt 50 ] && break; sleep 0.1
    done
    U=http://127.0.0.1:$PORT/files
    cat > "$V/RAM/Dl" <<SCRIPT
(debug "a" (download (source "$U/big.lha") (dest "Work:")))
(debug "b" (download (source "$U/small.txt") (dest "Work:s.txt") (checksum "$SUM")))
(debug "c" (download (source "$U/small.txt") (dest "Work:s.txt") (ifmissing)))
(debug "d" (download (source "$U/small.txt") (dest "Work:bad.txt") (checksum "00") (optional "nofail")) @ioerr)
(debug "e" (download (source "$U/missing.txt") (dest "Work:m.txt") (optional "nofail")))
(download (source "$U/missing.txt") (dest "Work:m.txt"))
(debug "not reached")
SCRIPT
    out=$("$PRG" RAM:Dl NOLOG 2>&1); rc=$?
    check "download to a drawer" cmp -s "$WORK/www/files/big.lha" "$V/Work/big.lha"
    check "download checksum" sh -c "cmp -s '$WORK/www/files/small.txt' '$V/Work/s.txt' && printf '%s\n' \"\$1\" | grep -q '^b 1'" _ "$out"
    check "download ifmissing" sh -c "printf '%s\n' \"\$1\" | grep -q '^c 1' && [ \$(grep -c small.txt '$WORK/http.log') = 2 ]" _ "$out"
    check "download bad checksum" sh -c "printf '%s\n' \"\$1\" | grep -q '^d 0 1' && [ ! -e '$V/Work/bad.txt' ] && [ ! -e '$V/Work/bad.txt.part' ]" _ "$out"
    check "download missing nofail" has "$out" '^e 0'
    check "download missing fails" sh -c "[ $rc = 10 ] && ! printf '%s\n' \"\$1\" | grep -q 'not reached'" _ "$out"
fi

cat > "$V/RAM/Panel" <<'SCRIPT'
(debug "p" (panel "RAM:pic.gif" (caption "Hello")))
(debug "q" (panel "RAM:nothere.png"))
(debug "r" (panel ""))
SCRIPT
out=$("$PRG" RAM:Panel NOLOG 2>&1)
check "panel shows" has "$out" '^\[panel\] RAM:pic.gif - Hello'
check "panel missing picture" has "$out" '^q 0'
check "panel back to the logo" sh -c "printf '%s\n' \"\$1\" | grep -q '^\[panel\] (none)' && printf '%s\n' \"\$1\" | grep -q '^r 1'" _ "$out"
