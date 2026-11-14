@echo off
rem Tyrion Cuthbert Larger Text Mod - Install launcher for Windows.
rem Usage: Install.bat [scale] [opacity]     (double-click for the defaults)
setlocal EnableExtensions DisableDelayedExpansion
set "LTM_RC=0"
if /i "%~1"=="--help" goto usage
if /i "%~1"=="-h" goto usage
if not "%~3"=="" goto bad_args
call :find_python || goto no_python
set "LTM_ARGS="
if not "%~1"=="" set LTM_ARGS=--scale "%~1"
if not "%~2"=="" set LTM_ARGS=%LTM_ARGS% --opacity "%~2"
rem Everything that runs after Python is one block, parsed before it starts.
(
  echo Tyrion Cuthbert Larger Text Mod - Install
  echo.
  %LTM_PY% "%~dp0larger_text_mod.py" install %LTM_ARGS%
  if errorlevel 1 (echo. & echo Install FAILED - see the message above.) else echo.
  call set "LTM_RC=%%errorlevel%%"
  if not defined LTM_NO_PAUSE pause
  call exit /b %%LTM_RC%%
)

:usage
echo Usage: Install.bat [scale] [opacity]
echo   scale    text size multiplier, 1.1 to 2.0 (default 1.5)
echo   opacity  dialogue panel background opacity, 0.0 to 1.0 (default 0.99)
echo Example: Install.bat 1.3 0.8
echo Running Install again keeps your config.ini and only changes the values you give.
goto finish

:bad_args
echo ERROR: Too many arguments.
echo Usage: Install.bat [scale] [opacity]   (see Install.bat --help)
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
