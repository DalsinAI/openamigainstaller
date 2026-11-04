#!/bin/sh
# Whole installs on host folders standing in for Amiga volumes: copying,
# libraries, drawers, S:User-Startup, text files, protection, pretending,
# errors and (onerror). Prints PASS or FAIL for each check.
set -u
: "${PRG:?}" "${WORK:?}"
V=$WORK/vol
rm -rf "$WORK"
mkdir -p "$V/Work" "$V/LIBS" "$V/S" "$V/T" "$V/SYS" "$V/ENV" "$V/RAM/pkg/App/Data/Sub" "$WORK/run"
cd "$WORK/run"
export OI_VOLUMES="$V"

check() {   # check NAME CONDITION...
    name=$1
    shift
    if "$@"; then echo "PASS $name"; else echo "FAIL $name"; fi
}

printf 'prog' > "$V/RAM/pkg/App/App"
printf 'icon' > "$V/RAM/pkg/App/App.info"
printf 'icon' > "$V/RAM/pkg/App.info"
printf 'd1' > "$V/RAM/pkg/App/Data/one.dat"
printf 'd2' > "$V/RAM/pkg/App/Data/Sub/two.dat"
printf 'x\0$VER: my.library 2.5 (1.1.2026)\0' > "$V/RAM/pkg/my.library"
printf 'x\0$VER: old.library 1.0 (1.1.2020)\0' > "$V/RAM/pkg/old.library"
printf 'x\0$VER: old.library 3.0 (1.1.2025)\0' > "$V/LIBS/old.library"
printf 'echo hi\n' > "$V/S/User-Startup"

cat > "$V/RAM/pkg/Install" <<'SCRIPT'
(set @app-name "App")
(welcome "Test install.")
(set #dest (askdir (prompt "Where?") (help @askdir-help) (default "Work:")))
(set #dest (tackon #dest "App"))
(copyfiles (prompt "Copying") (help @copyfiles-help) (source "RAM:pkg/App") (dest #dest) (all))
(copyfiles (source "RAM:pkg") (dest "Work:") (choices "App.info"))
(copylib (prompt "lib") (help @copylib-help) (source "RAM:pkg/my.library") (dest "LIBS:"))
(copylib (prompt "lib") (help @copylib-help) (source "RAM:pkg/old.library") (dest "LIBS:"))
(makedir "Work:Empty")
(startup "App" (prompt "s") (help @startup-help) (command "Assign App: " #dest "\n"))
(startup "App" (prompt "s") (help @startup-help) (command "Assign App: " #dest "\nPath App: ADD\n"))
(textfile (dest "T:note") (append "line1\n" "line2\n"))
(protect (tackon #dest "App") "+s")
(debug (protect (tackon #dest "App")))
(delete (tackon #dest "Data/Sub/two.dat"))
(copyfiles (source "RAM:pkg/App/Data") (dest "Work:Pat") (pattern "#?.dat"))
(set @default-dest #dest)
(debug "done" (exists #dest) (exists (tackon #dest "App")) (getsize (tackon #dest "App")))
SCRIPT
printf -- '-\n-\n' > "$WORK/answers"
out=$(OI_ANSWERS="$WORK/answers" "$PRG" RAM:pkg/Install 2>&1)
check "copyfiles (all)" test -f "$V/Work/App/App" -a -f "$V/Work/App/Data/one.dat" -a -f "$V/Work/App/App.info"
check "copyfiles (choices)" test -f "$V/Work/App.info"
check "copylib new" grep -q "my.library 2.5" "$V/LIBS/my.library"
check "copylib keeps newer" grep -q "old.library 3.0" "$V/LIBS/old.library"
check "makedir" test -d "$V/Work/Empty"
check "startup replaced" sh -c "[ \$(grep -c ';BEGIN App' '$V/S/User-Startup') = 1 ] && grep -q 'Path App: ADD' '$V/S/User-Startup' && head -1 '$V/S/User-Startup' | grep -q 'echo hi'"
check "textfile" sh -c "printf 'line1\nline2\n' | cmp -s - '$V/T/note'"
check "protect" sh -c "printf '%s\n' \"\$1\" | grep -q '^64$'" _ "$(printf '%s\n' "$out" | grep -v '^\[' | grep -v '^ ' | sed -n 1p)"
check "delete" test ! -f "$V/Work/App/Data/Sub/two.dat"
check "copyfiles (pattern)" test -f "$V/Work/Pat/one.dat" -a ! -d "$V/Work/Pat/Sub"
check "result" sh -c "printf '%s\n' \"\$1\" | grep -q '^done 2 1 4$'" _ "$out"
check "final page" sh -c "printf '%s\n' \"\$1\" | grep -q 'App has been installed in Work:App'" _ "$out"
check "log" grep -q 'Copy file "RAM:pkg/App/App" to "Work:App/App"' install_log_file

# pretending: nothing changes but what is (safe)
rm -rf "$V/Work"/* install_log_file
printf -- '1 1 2\n-\n' > "$WORK/answers"
out=$(OI_ANSWERS="$WORK/answers" "$PRG" RAM:pkg/Install 2>&1)
check "pretend copies nothing" test ! -e "$V/Work/App"
check "pretend logs" grep -q 'pretending' install_log_file

# errors: (onerror) runs, the run ends with 10
cat > "$V/RAM/pkg/Bad" <<'SCRIPT'
(onerror (debug "cleanup" @error-msg))
(copyfiles (source "RAM:pkg/nothere") (dest "Work:x"))
(debug "not reached")
SCRIPT
out=$("$PRG" RAM:pkg/Bad NOLOG 2>&1); rc=$?
check "error rc" test "$rc" = 10
check "onerror" sh -c "printf '%s\n' \"\$1\" | grep -q '^cleanup Line 2: \"RAM:pkg/nothere\" isn.t there'" _ "$out"
check "error stops" sh -c "! printf '%s\n' \"\$1\" | grep -q 'not reached'" _ "$out"
cat > "$V/RAM/pkg/NoFail" <<'SCRIPT'
(copyfiles (source "RAM:pkg/nothere") (dest "Work:x") (optional "nofail"))
(debug "went on" @ioerr)
SCRIPT
out=$("$PRG" RAM:pkg/NoFail NOLOG 2>&1)
check "optional nofail" sh -c "printf '%s\n' \"\$1\" | grep -q '^went on 1'" _ "$out"

# the user aborts at a page; Back runs (back ...)
cat > "$V/RAM/pkg/Ask" <<'SCRIPT'
(set n 0)
(message "one" (back (set n 5)))
(debug "n" n)
(askbool (prompt "q") (help "h"))
(debug "not reached")
SCRIPT
printf -- '-\nback\nabort\n' > "$WORK/answers"
out=$(OI_ANSWERS="$WORK/answers" "$PRG" RAM:pkg/Ask NOLOG 2>&1); rc=$?
check "back" sh -c "printf '%s\n' \"\$1\" | grep -q '^n 5'" _ "$out"
check "abort rc" test "$rc" = 5
check "abort page" sh -c "printf '%s\n' \"\$1\" | grep -q 'final-aborted'" _ "$out"

# a mistake in the script is reported with its line
printf '(debug 1)\n(debug "x"\n' > "$V/RAM/pkg/Broken"
out=$("$PRG" RAM:pkg/Broken NOLOG 2>&1); rc=$?
check "parse error" sh -c "[ $rc = 20 ] && printf '%s\n' \"\$1\" | grep -q 'Line 2: missing'" _ "$out"

# foreach and comments across lines
mkdir -p "$V/RAM/each"; : > "$V/RAM/each/a.txt"; : > "$V/RAM/each/b.txt"; : > "$V/RAM/each/c.dat"
cat > "$V/RAM/pkg/Each" <<'SCRIPT'
; a comment
(set n 0)
(foreach "RAM:each" "#?.txt"   ; another
    (set n (+ n 1)))
(debug "each" n @each-type)
SCRIPT
out=$("$PRG" RAM:pkg/Each NOLOG 2>&1)
check "foreach" sh -c "printf '%s\n' \"\$1\" | grep -q '^each 2 -3'" _ "$out"
