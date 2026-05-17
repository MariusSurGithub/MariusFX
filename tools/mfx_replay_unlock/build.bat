@echo off
:: ───────────────────────────────────────────────────────────────────────────
:: build.bat  —  build mfx_replay_unlock.asi as a standalone FiveM plugin
::
:: Outputs:
::   bin\mfx_replay_unlock.asi   (this is just a renamed .dll; FiveM's
::                                asi-five loader explicitly looks for
::                                *.asi in plugins\, see
::                                docs/client-manual/_index.md).
::   bin\mfx_replay_unlock.pdb   (full PDB so crashes from FiveM-side
::                                anti-cheat reports symbolicate cleanly)
::   bin\mfx_replay_unlock.map   (linker map for offset-to-symbol)
::
:: This script has zero dependencies on MariusFX's build pipeline. It only
:: needs:
::   - cl.exe / rc.exe / link.exe (MSVC, x64)
::   - %ROOT%\deps\minhook  (vendored — same path as MariusFX uses; the
::     MariusFX repo just happens to be where the deps live, the .asi
::     itself doesn't import any MariusFX symbol).
:: ───────────────────────────────────────────────────────────────────────────

setlocal

set HERE=%~dp0
:: %HERE% ends with backslash. Strip it for cleaner paths.
if "%HERE:~-1%"=="\" set HERE=%HERE:~0,-1%

:: ROOT = parent of tools\mfx_replay_unlock = MariusFX repo root.
:: That's how we reach deps\minhook without vendoring our own copy.
for %%I in ("%HERE%\..\..") do set ROOT=%%~fI

set OUT=%HERE%\bin
set OBJ=%OUT%\obj
if not exist "%OUT%" mkdir "%OUT%"
if not exist "%OBJ%" mkdir "%OBJ%"

:: ── Resolve MSVC ───────────────────────────────────────────────────────────
:: We use GOTO instead of nested IF (..) because the path to vswhere lives
:: under %ProgramFiles(x86)% — the literal ")" in that env-var name throws
:: off CMD's paren matcher when the IF body itself is wrapped in (...).
if not "%VSCMD_ARG_TGT_ARCH%"=="" goto :vs_ready
goto :find_vs

:find_vs
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo [build] vswhere.exe missing - install VS 2019+ with C++ tools.
    exit /b 1
)
for /f "usebackq tokens=*" %%v in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSPATH=%%v"
if not defined VSPATH (
    echo [build] No VS install with C++ x64 tools found.
    exit /b 1
)
call "%VSPATH%\VC\Auxiliary\Build\vcvars64.bat" >nul

:vs_ready

:: ── Sources ────────────────────────────────────────────────────────────────
set MH=%ROOT%\deps\minhook

:: MinHook (C) + our hooks + dllmain. Keep C files in a separate cl call
:: so we don't pollute them with /std:c++17 (cl auto-detects by extension
:: but being explicit is cleaner).
set MH_C=^
 "%MH%\src\hook.c"^
 "%MH%\src\buffer.c"^
 "%MH%\src\trampoline.c"^
 "%MH%\src\hde\hde64.c"

set CPP=^
 "%HERE%\src\hooks.cpp"^
 "%HERE%\src\dllmain.cpp"

set RC_IN=%HERE%\src\mfx_replay_unlock.rc
set RC_OUT=%OBJ%\mfx_replay_unlock.res

set INCLUDES=/I "%MH%\include" /I "%HERE%\src"

:: NOTE: we deliberately do NOT pull in any MariusFX header. This .asi
:: must be 100%% independent of MariusFX — that's the whole point of
:: living in tools/mfx_replay_unlock/ rather than mariusfx/.
:: WIN32_LEAN_AND_MEAN is set per-TU in dllmain.cpp/hooks.cpp; defining it
:: here too would double-define and emit C4005.
set DEFINES=/DNDEBUG /DUNICODE /D_UNICODE /DNOMINMAX /D_CRT_SECURE_NO_WARNINGS

set CXXFLAGS=/nologo /std:c++17 /EHsc /MT /O2 /W3 /utf-8 /Zc:__cplusplus /Zi /Fo"%OBJ%\\" /Fd"%OBJ%\mfx_replay_unlock.pdb"

:: /MT static-links the CRT. Critical for an .asi: FiveM might not have
:: ucrtbase shimmed in the GTAProcess's DLL search path the same way our
:: dev shell does, and we don't want to fight DLL-not-found errors at
:: load time. The trade-off is +200 KiB binary, which is fine.

set LDFLAGS=/nologo /DLL /OUT:"%OUT%\mfx_replay_unlock.asi" /IMPLIB:"%OUT%\mfx_replay_unlock.lib" /PDB:"%OUT%\mfx_replay_unlock.pdb" /MAP:"%OUT%\mfx_replay_unlock.map" /MAPINFO:EXPORTS /DEBUG:FULL /OPT:REF /OPT:ICF

set LIBS=ws2_32.lib user32.lib kernel32.lib advapi32.lib shell32.lib

:: ── Compile resources ──────────────────────────────────────────────────────
echo [build] rc.exe ^<- mfx_replay_unlock.rc
rc.exe /nologo /fo "%RC_OUT%" "%RC_IN%"
if errorlevel 1 (
    echo [build] FAILED resource compile
    exit /b 1
)

:: ── Compile + link ─────────────────────────────────────────────────────────
echo [build] cl.exe ^<- hooks.cpp dllmain.cpp + MinHook
cl.exe %CXXFLAGS% %DEFINES% %INCLUDES% %CPP% %MH_C% "%RC_OUT%" /link %LDFLAGS% %LIBS%
if errorlevel 1 (
    echo [build] FAILED link
    exit /b 1
)

echo [build] OK -^> %OUT%\mfx_replay_unlock.asi
for %%I in ("%OUT%\mfx_replay_unlock.asi") do echo            size = %%~zI bytes

endlocal
