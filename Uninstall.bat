@echo off
setlocal EnableExtensions DisableDelayedExpansion
pushd "%~dp0" || exit /b 1
set "DIALOGUE_PY=py"
set "DIALOGUE_PY_ARGS=-3"
py -3 -c "import sys; sys.exit(0 if sys.version_info >= (3,10) else 1)" >nul 2>&1
if not errorlevel 1 goto run
set "DIALOGUE_PY=python"
set "DIALOGUE_PY_ARGS="
python -c "import sys; sys.exit(0 if sys.version_info >= (3,10) else 1)" >nul 2>&1
if not errorlevel 1 goto run
echo Python 3.10 or newer is required to restore the backups.
goto failed
:run
%DIALOGUE_PY% %DIALOGUE_PY_ARGS% mod.py uninstall
if errorlevel 1 goto failed
popd
pause
exit /b 0
:failed
echo.
echo Restore failed. Read the error above.
popd
pause
exit /b 1
