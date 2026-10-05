#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
if [[ "${1:-}" == "--help" || "${1:-}" == "-h" ]]; then
    printf 'Usage: bash Uninstall.sh\nRestores the original game files from this mod\047s backups.\n'
    exit 0
fi
if (( $# != 0 )); then
    printf 'Usage: bash Uninstall.sh\n' >&2
    exit 2
fi
if [[ "$(uname -s)" != "Linux" ]]; then
    printf 'This script is for Linux / Steam Deck. On Windows use Uninstall.bat.\n' >&2
    exit 1
fi
if ! command -v python3 >/dev/null 2>&1; then
    printf 'Python 3 is required to restore the backups.\n' >&2
    exit 1
fi

# Restoring backups only uses Python's standard library: no downloads or venv needed.
python3 "$script_dir/mod.py" uninstall
