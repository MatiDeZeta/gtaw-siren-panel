@echo off
REM Build gtaw-siren-panel.asi. Needs Visual Studio Build Tools with the "Desktop development with
REM C++" workload. Run from an "x64 Native Tools Command Prompt", or let this find vcvars.
REM
REM There is nothing to restore, download or unpack first. The plugin has no dependencies: the
REM only library it links is winhttp.lib, which is part of the Windows SDK.

if "%VSCMD_ARG_TGT_ARCH%"=="x64" goto build
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
  echo Visual Studio Build Tools not found. Install "Desktop development with C++" from:
  echo   https://visualstudio.microsoft.com/downloads/  ^(Build Tools for Visual Studio^)
  exit /b 1
)
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -all -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSPATH=%%i"
if "%VSPATH%"=="" (
  for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -all -latest -products * -property installationPath`) do set "VSPATH=%%i"
)
if "%VSPATH%"=="" (
  echo Found Visual Studio but not the C++ tools. Add the "Desktop development with C++" workload.
  exit /b 1
)
call "%VSPATH%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1

:build
rc /nologo /fo gtaw-siren-panel.res gtaw-siren-panel.rc || exit /b 1

REM /MT   so the .asi does not need the VC runtime DLLs next to FiveM.
REM /EHsc for std::string and std::vector.
REM /sdl /GS /guard:cf  extra checking on code that parses bytes off a socket.
REM No /clr: a managed DLL is refused outright by asi-five, so this cannot be built as C#.
cl /nologo /std:c++17 /W4 /O2 /MT /EHsc /GS /sdl /guard:cf /DNDEBUG /DWIN32_LEAN_AND_MEAN /LD /I src ^
   dllmain.cpp src\core\*.cpp src\cdp\*.cpp src\hud\*.cpp ^
   gtaw-siren-panel.res /Fe:gtaw-siren-panel.asi ^
   /link /DLL winhttp.lib /guard:cf /DYNAMICBASE /NXCOMPAT /HIGHENTROPYVA /Brepro || exit /b 1

REM /Brepro writes a content hash where the build time goes, which reads as a date decades away.
REM Put the commit's time there instead: still reproducible, and not a future-dated header.
where python >nul 2>nul && python tools\set_pe_time.py gtaw-siren-panel.asi

del /q *.obj *.res *.exp *.lib 2>nul
echo.
echo Built gtaw-siren-panel.asi
echo Copy it to: ^<your FiveM install^>\FiveM.app\plugins\
