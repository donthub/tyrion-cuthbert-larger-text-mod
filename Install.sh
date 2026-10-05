#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
if [[ "${1:-}" == "--help" || "${1:-}" == "-h" ]]; then
    printf 'Usage: bash Install.sh [scale]\nDefault: 1.5 (50%% larger); range: 1.1 to 2.0.\n'
    exit 0
fi
if (( $# > 1 )); then
    printf 'Usage: bash Install.sh [scale]\n' >&2
    exit 2
fi
if [[ "$(uname -s)" != "Linux" ]]; then
    printf 'This script is for Linux / Steam Deck. On Windows use Install.bat.\n' >&2
    exit 1
fi
if ! command -v python3 >/dev/null 2>&1; then
    printf 'Python 3 is required. Run this script in Steam Deck Desktop Mode.\n' >&2
    exit 1
fi
scale="${1:-1.5}"
python3 -c 'import math, sys; s = float(sys.argv[1]); sys.exit(0 if math.isfinite(s) and 1.1 <= s <= 2.0 else "Scale must be between 1.1 and 2.0")' "$scale"
if [[ ! -f "$script_dir/../Tyrion1_Data/StreamingAssets/aa/catalog.bin" ]]; then
    printf 'Place DialogueTextMod directly inside the game installation folder first.\n' >&2
    exit 1
fi

# Keep all packages local; SteamOS system packages and its read-only root stay intact.
venv_dir="$script_dir/.venv-linux"
if [[ ! -x "$venv_dir/bin/python" ]]; then
    printf 'Creating a local Python environment...\n'
    python3 -m venv --without-pip "$venv_dir"
fi
venv_python="$venv_dir/bin/python"
if ! "$venv_python" -c 'import UnityPy; assert UnityPy.__version__ == "1.25.4"' >/dev/null 2>&1; then
    printf 'Installing Linux dependencies (internet required on first use)...\n'
    if ! "$venv_python" -m pip --version >/dev/null 2>&1; then
        if ! "$venv_python" -m ensurepip --upgrade; then
            # Some Linux distributions omit ensurepip. Use pip's official bootstrap.
            command -v curl >/dev/null 2>&1 || { printf 'curl is required to bootstrap pip.\n' >&2; exit 1; }
            bootstrap_file="$(mktemp "$venv_dir/get-pip.XXXXXX.py")"
            trap 'rm -f -- "$bootstrap_file"' EXIT
            curl --fail --show-error --location --proto '=https' --proto-redir '=https' \
                https://bootstrap.pypa.io/get-pip.py --output "$bootstrap_file"
            "$venv_python" "$bootstrap_file" --disable-pip-version-check
        fi
    fi
    "$venv_python" -m pip install --disable-pip-version-check 'UnityPy==1.25.4'
fi

"$venv_python" "$script_dir/mod.py" install --scale "$scale"
printf '\nDone. Launch the game through Steam as usual.\n'
