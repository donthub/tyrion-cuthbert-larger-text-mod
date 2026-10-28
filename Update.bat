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
echo Python 3.10 or newer is required to download the update.
popd
pause
exit /b 1
:run
rem Parse the whole block before updating, because Update.bat may replace itself.
(
    %DIALOGUE_PY% %DIALOGUE_PY_ARGS% update_mod.py %*
    if errorlevel 1 (
        echo.
        echo Update failed. Read the error above.
        popd
        pause
        exit /b 1
    )
    popd
    pause
    exit /b 0
)
