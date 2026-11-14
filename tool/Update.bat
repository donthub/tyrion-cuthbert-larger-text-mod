@echo off
rem Tyrion Cuthbert Larger Text Mod - Update launcher for Windows.
rem Usage: Update.bat [--check]
rem The update may replace this file while it runs. cmd reads batch files line by line,
rem so all code from the Python call to the end is inside one parenthesized block
rem that cmd parses completely before running it, and that block ends with exit /b.
setlocal EnableExtensions DisableDelayedExpansion
set "LTM_RC=0"
if /i "%~1"=="--help" goto usage
if /i "%~1"=="-h" goto usage
if not "%~2"=="" goto bad_args
if not "%~1"=="" if /i not "%~1"=="--check" goto bad_args
call :find_python || goto no_python
set "LTM_ARGS="
if /i "%~1"=="--check" set "LTM_ARGS=--check"
rem Everything that runs after Python is one block, parsed before it starts.
(
  echo Tyrion Cuthbert Larger Text Mod - Update
  echo.
  %LTM_PY% "%~dp0update_mod.py" %LTM_ARGS%
  if errorlevel 1 (echo. & echo Update FAILED - see the message above.) else echo.
  call set "LTM_RC=%%errorlevel%%"
  if not defined LTM_NO_PAUSE pause
  call exit /b %%LTM_RC%%
)

:usage
echo Usage: Update.bat [--check]
echo Updates the mod to the latest stable release from GitHub.
echo   --check  only download and validate the latest release; change nothing
echo Your config.ini and settings are kept. The new version is active at the next game start.
goto finish

:bad_args
echo ERROR: Wrong arguments.
echo Usage: Update.bat [--check]   (see Update.bat --help)
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
