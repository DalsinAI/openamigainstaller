#!/bin/sh
# OpenInstaller's tests on the host: the language (lang.txt), then whole
# installs (installs/*.sh). Each runs with no window.
#   tests/run.sh ./openinstaller
set -u
PRG=$(CDPATH= cd -- "$(dirname -- "$1")" && pwd)/$(basename -- "$1")
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
pass=0
fail=0

# the language
n=0
while IFS= read -r line; do
    case $line in ''|'#'*) continue ;; esac
    n=$((n + 1))
    script=${line%%;=>*}
    want=${line#*;=> }
    mkdir -p "$WORK/lang"
    printf '%s\n' "$script" > "$WORK/lang/s"
    got=$(cd "$WORK/lang" && OI_VOLUMES="$WORK/lang" "$PRG" "$WORK/lang/s" NOLOG 2>&1 | grep -v '^\[[a-z-]*\]' | grep -v '^  ' | tr '\n' ' ' | sed 's/ $//')
    if [ "$got" = "$want" ]; then
        pass=$((pass + 1))
    else
        fail=$((fail + 1))
        printf 'FAIL lang %d: %s\n  want [%s]\n  got  [%s]\n' "$n" "$script" "$want" "$got"
    fi
done < "$HERE/lang.txt"

# whole installs: each script prints PASS or FAIL lines
for t in "$HERE"/installs/*.sh; do
    [ -f "$t" ] || continue
    out=$(PRG="$PRG" WORK="$WORK/$(basename "$t" .sh)" sh "$t" 2>&1)
    p=$(printf '%s\n' "$out" | grep -c '^PASS')
    f=$(printf '%s\n' "$out" | grep -c '^FAIL')
    pass=$((pass + p))
    fail=$((fail + f))
    printf '%s\n' "$out" | grep -E '^(FAIL|  )' || true
done

echo "$pass passed, $fail failed"
[ "$fail" -eq 0 ]
