"""Prepare local Windows dependencies, then run the mod with the same Python."""
from __future__ import annotations

import argparse
import math
from pathlib import Path
import struct
import subprocess
import sys
import venv

HERE = Path(__file__).resolve().parent
DEPENDENCY = 'UnityPy==1.25.4'


def prepare_python():
    # A separate directory for each Python ABI avoids reusing incompatible wheels.
    tag = f'windows-py{sys.version_info.major}{sys.version_info.minor}'
    environment = HERE.parent / '.mod-tools' / tag
    python = environment / 'Scripts' / 'python.exe'
    if not python.is_file():
        print(f'Creating local Python environment: {environment}', flush=True)
        venv.EnvBuilder(with_pip=True).create(environment)
    probe = subprocess.run([str(python), '-I', '-c',
                            'import UnityPy; assert UnityPy.__version__ == "1.25.4"'],
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    if probe.returncode:
        print('Installing Windows dependencies (internet required on first use)...', flush=True)
        pip_probe = subprocess.run([str(python), '-I', '-m', 'pip', '--version'],
                                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        if pip_probe.returncode:
            subprocess.run([str(python), '-I', '-m', 'ensurepip', '--upgrade'], check=True)
        subprocess.run([str(python), '-I', '-m', 'pip', 'install',
                        '--disable-pip-version-check', '--upgrade', '--force-reinstall', DEPENDENCY], check=True)
        subprocess.run([str(python), '-I', '-c',
                        'import UnityPy; assert UnityPy.__version__ == "1.25.4"'], check=True)
    return python


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=('install', 'uninstall', 'status'))
    parser.add_argument('--scale', type=float, default=1.5)
    parser.add_argument('--setup-only', action='store_true',
                        help='Prepare dependencies without modifying game assets (install only).')
    args = parser.parse_args()
    if sys.platform != 'win32' or sys.implementation.name != 'cpython' or struct.calcsize('P') != 8:
        parser.error('Use 64-bit CPython on Windows, or the .sh scripts on Linux.')
    if sys.version_info < (3, 10):
        parser.error('Python 3.10 or newer is required (64-bit).')
    if not math.isfinite(args.scale) or not 1.1 <= args.scale <= 2.0:
        parser.error('--scale must be between 1.1 and 2.0')
    if args.setup_only and args.action != 'install':
        parser.error('--setup-only requires install')
    script = HERE / 'mod.py'
    if args.action == 'install':
        if not (HERE.parent / 'Tyrion1_Data/StreamingAssets/aa/catalog.bin').is_file():
            parser.error('Place DialogueTextMod directly inside the game installation folder.')
        if not args.setup_only:
            # Check the running game and any existing backup before downloading.
            sys.path.insert(0, str(HERE))
            import mod
            mod.game_closed()
            state = mod.load_state(HERE)
            if state:
                mod.check_files(HERE.parent, HERE, state)
        python = prepare_python()
        if args.setup_only:
            print('Windows dependencies are ready. Game files were not changed.')
            return 0
    else:
        # Uninstall/status only need the standard library, including offline use.
        python = Path(sys.executable)
    return subprocess.run([str(python), '-I', str(script), args.action,
                           '--scale', str(args.scale)]).returncode


if __name__ == '__main__':
    try:
        sys.exit(main())
    except (OSError, RuntimeError, subprocess.CalledProcessError) as error:
        print(f'ERROR: {error}', file=sys.stderr)
        print('Check the Python installation, internet connection, and game-folder write permissions.',
              file=sys.stderr)
        sys.exit(1)
