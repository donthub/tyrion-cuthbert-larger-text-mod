#!/bin/sh
# Tyrion Cuthbert Larger Text Mod - Uninstall launcher for Linux / Steam Deck.
# Usage: ./Uninstall.sh
# The whole script is one { ... } block ending in exit, so the shell has read all of it
# before it starts running.
{
usage() {
    cat <<'EOF'
Usage: ./Uninstall.sh
Removes everything Install added to the game folder (config and log included).
Files that existed before Install, or were changed by something else, are kept.
EOF
}

case "$(uname -s 2>/dev/null)" in
    Linux) ;;
    *)
        echo "ERROR: Uninstall.sh is for Linux / Steam Deck only." >&2
        echo "Hint: On Windows, double-click Uninstall.bat instead." >&2
        exit 1
        ;;
esac

case "${1:-}" in
    -h|--help) usage; exit 0 ;;
esac
if [ "$#" -ne 0 ]; then
    echo "ERROR: Wrong arguments." >&2
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

echo "Tyrion Cuthbert Larger Text Mod - Uninstall"
echo
"$PY" "$SCRIPT_DIR/larger_text_mod.py" uninstall "$@"
rc=$?
echo
if [ "$rc" -ne 0 ]; then
    echo "Uninstall FAILED - see the message above." >&2
fi
exit "$rc"
}
