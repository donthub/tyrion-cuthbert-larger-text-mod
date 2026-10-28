#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
if [[ "$(uname -s)" != "Linux" ]]; then
    printf 'This script is for Linux / Steam Deck. On Windows use Update.bat.\n' >&2
    exit 1
fi
if ! command -v python3 >/dev/null 2>&1; then
    printf 'Python 3.10 or newer is required to download the update.\n' >&2
    exit 1
fi

# exec avoids reading this launcher after the updater replaces it.
exec python3 "$script_dir/update_mod.py" "$@"
