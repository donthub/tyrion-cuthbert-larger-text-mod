#!/bin/sh
# Tyrion Cuthbert Larger Text Mod - Install launcher for Linux / Steam Deck.
# Usage: ./Install.sh [scale] [opacity]
# The whole script is one { ... } block ending in exit, so the shell has read all of it
# before it starts running.
{
usage() {
    cat <<'EOF'
Usage: ./Install.sh [scale] [opacity]
  scale    text size multiplier, 1.1 to 2.0 (default 1.5)
  opacity  dialogue panel background opacity, 0.0 to 1.0 (default 0.99)
Example: ./Install.sh 1.3 0.8
Running Install again keeps your config.ini and only changes the values you give.
EOF
}

case "$(uname -s 2>/dev/null)" in
    Linux) ;;
    *)
        echo "ERROR: Install.sh is for Linux / Steam Deck only." >&2
        echo "Hint: On Windows, double-click Install.bat instead." >&2
        exit 1
        ;;
esac

case "${1:-}" in
    -h|--help) usage; exit 0 ;;
esac
if [ "$#" -gt 2 ]; then
    echo "ERROR: Too many arguments." >&2
    usage >&2
    exit 2
fi

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd) || {
    echo "ERROR: Could not determine the folder of this script." >&2
    exit 1
}

PY=""
for candidate in python3 python; do
    if command -v "$candidate" >/dev/null 2>&1 &&
        "$candidate" -c 'import sys; sys.exit(0 if sys.version_info >= (3, 8) else 1)' >/dev/null 2>&1; then
        PY=$candidate
        break
    fi
done
if [ -z "$PY" ]; then
    echo "ERROR: Python 3.8 or newer (python3) was not found." >&2
    echo "Hint: SteamOS includes python3. On other distributions install it with the package manager," >&2
    echo "      e.g. 'sudo apt install python3' or 'sudo dnf install python3', then run this again." >&2
    exit 1
fi
SCALE=${1:-}
OPACITY=${2:-}
set --
if [ -n "$SCALE" ]; then set -- "$@" --scale "$SCALE"; fi
if [ -n "$OPACITY" ]; then set -- "$@" --opacity "$OPACITY"; fi

echo "Tyrion Cuthbert Larger Text Mod - Install"
echo
"$PY" "$SCRIPT_DIR/larger_text_mod.py" install "$@"
rc=$?
echo
if [ "$rc" -ne 0 ]; then
    echo "Install FAILED - see the message above." >&2
fi
exit "$rc"
}
