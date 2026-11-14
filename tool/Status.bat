@echo off
rem Tyrion Cuthbert Larger Text Mod - Status launcher for Windows.
rem Usage: Status.bat
setlocal EnableExtensions DisableDelayedExpansion
set "LTM_RC=0"
if /i "%~1"=="--help" goto usage
if /i "%~1"=="-h" goto usage
if not "%~1"=="" goto bad_args
call :find_python || goto no_python
rem Everything that runs after Python is one block, parsed before it starts.
(
  echo Tyrion Cuthbert Larger Text Mod - Status
  echo.
  %LTM_PY% "%~dp0larger_text_mod.py" status
  if errorlevel 1 (echo. & echo Status FAILED - see the message above.) else echo.
  call set "LTM_RC=%%errorlevel%%"
  if not defined LTM_NO_PAUSE pause
  call exit /b %%LTM_RC%%
)

:usage
echo Usage: Status.bat
echo Shows whether the mod is installed, its settings, and features skipped at the last game start.
goto finish

:bad_args
echo ERROR: Wrong arguments.
echo Usage: Status.bat   (see Status.bat --help)
set "LTM_RC=2"
goto finish

:no_python
echo ERROR: Python 3.8 or newer was not found.
echo Hint: Install it with "winget install Python.Python.3.12" or from https://www.python.org/downloads/
echo       (tick "Add python.exe to PATH" in the installer), then run this file again.
set "LTM_RC=1"
goto finish

:find_python
set "LTM_PY="
py -3 -c "import sys; sys.exit(0 if sys.version_info >= (3, 8) else 1)" >nul 2>&1
if not errorlevel 1 (set "LTM_PY=py -3" & exit /b 0)
python -c "import sys; sys.exit(0 if sys.version_info >= (3, 8) else 1)" >nul 2>&1
if not errorlevel 1 (set "LTM_PY=python" & exit /b 0)
python3 -c "import sys; sys.exit(0 if sys.version_info >= (3, 8) else 1)" >nul 2>&1
if not errorlevel 1 (set "LTM_PY=python3" & exit /b 0)
exit /b 1

:finish
if not defined LTM_NO_PAUSE pause
exit /b %LTM_RC%
