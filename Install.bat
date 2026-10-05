@echo off
setlocal EnableExtensions DisableDelayedExpansion
pushd "%~dp0" || exit /b 1
set "DIALOGUE_SCALE=1.5"
if not "%~1"=="" set "DIALOGUE_SCALE=%~1"
set "DIALOGUE_PY=py"
set "DIALOGUE_PY_ARGS=-3"
py -3 -c "import sys,struct; sys.exit(0 if sys.version_info >= (3,10) and struct.calcsize('P') == 8 and sys.implementation.name == 'cpython' else 1)" >nul 2>&1
if not errorlevel 1 goto run
set "DIALOGUE_PY=python"
set "DIALOGUE_PY_ARGS="
python -c "import sys,struct; sys.exit(0 if sys.version_info >= (3,10) and struct.calcsize('P') == 8 and sys.implementation.name == 'cpython' else 1)" >nul 2>&1
if not errorlevel 1 goto run
echo Install 64-bit Python 3.10 or newer from https://www.python.org/downloads/windows/
echo Enable the Python launcher or Add python.exe to PATH, then run this file again.
goto failed
:run
%DIALOGUE_PY% %DIALOGUE_PY_ARGS% windows_setup.py install --scale "%DIALOGUE_SCALE%"
if errorlevel 1 goto failed
echo.
echo Done. Start the game normally.
popd
pause
exit /b 0
:failed
echo.
echo Installation failed. Read the error above.
popd
pause
exit /b 1
