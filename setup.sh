#!/bin/sh
# genrecomp quick start (Linux). Same steps as README "Step by step";
# reruns skip finished steps. Asks before installing anything.
# Details go to setup.log.   Usage: ./setup.sh [--yes] [path/to/rom.gen]
set -u
ROOT=$(cd "$(dirname "$0")" && pwd)
LOG="$ROOT/setup.log"; BUILD="$ROOT/build"
GPGX_URL=https://github.com/ekeeke/Genesis-Plus-GX.git
GPGX_COMMIT=c3df2d2   # the submodule pin; keep in step with ext/Genesis-Plus-GX
YES=0; ROM=""
for a in "$@"; do case "$a" in --yes) YES=1 ;; *) ROM=$a ;; esac; done
echo "genrecomp setup $(date)" > "$LOG"

say()  { echo "$1"; echo "$1" >> "$LOG"; }
fail() { echo; echo "Setup stopped: $1"; echo "Details are in $LOG"; exit 1; }
run()  { what=$1; shift; echo "> $*" >> "$LOG"; "$@" >> "$LOG" 2>&1 || fail "$what failed."; }
ask()  { [ $YES = 1 ] && return 0; printf '%s [y/N] ' "$1"; read -r r; case "$r" in y|Y*) return 0 ;; esac; return 1; }

say "1/4 Checking prerequisites"
missing=""
command -v git >/dev/null || missing="$missing git"
command -v cmake >/dev/null || missing="$missing cmake"
command -v cc >/dev/null || missing="$missing build-essential"
pkg-config --exists sdl2 2>/dev/null || missing="$missing libsdl2-dev"
if [ -n "$missing" ]; then
    command -v apt-get >/dev/null || fail "Install these with your package manager, then rerun:$missing (plus pkg-config)"
    ask "Missing:$missing. Install with apt (needs sudo, under 100 MB)?" || fail "Install$missing and rerun ./setup.sh."
    run "apt install" sudo apt-get install -y $missing pkg-config
fi
say "    toolchain and SDL2 found"

say "2/4 Fetching Genesis Plus GX"
GPGX="$ROOT/ext/Genesis-Plus-GX"
if [ ! -f "$GPGX/core/system.c" ]; then
    if [ -e "$ROOT/.git" ]; then
        run "submodule update" git -C "$ROOT" submodule update --init
    else   # plain zip download: no submodules
        rm -rf "$GPGX"
        run "downloading Genesis Plus GX" git clone "$GPGX_URL" "$GPGX"
        run "pinning Genesis Plus GX" git -C "$GPGX" checkout "$GPGX_COMMIT"
    fi
fi

say "3/4 Building"
[ -f "$BUILD/CMakeCache.txt" ] || run "configure" cmake -S "$ROOT" -B "$BUILD" -DCMAKE_BUILD_TYPE=Release
run "build" cmake --build "$BUILD" -j
run "self-check" ctest --test-dir "$BUILD" --output-on-failure
say "    built and self-checked"

say "4/4 Launcher"
printf '#!/bin/sh\n# Plays a Genesis ROM on the reference runner.\nexec "$(dirname "$0")/build/genrecomp_ref" "$@"\n' > "$ROOT/reference-runner.sh"
chmod +x "$ROOT/reference-runner.sh"
if [ -n "$ROM" ]; then
    [ -f "$ROM" ] || fail "No file at $ROM."
    run "smoke test" "$BUILD/genrecomp_ref" --headless --frames 600 "$ROM"
    say "    your ROM ran 600 frames headless"
fi
say "Done. Run ./reference-runner.sh path/to/rom.gen"
