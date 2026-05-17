@echo off
:: -------------------------------------------------------------------
:: MariusFX UI preview — standalone build (Win32 + DX11 + ImGui).
::
:: Compiles ui.cpp + theme.cpp from the production sources together
:: with a self-contained mock runtime, so you can iterate on the
:: overlay without restarting FiveM.
::
:: Output: bin\preview\preview.exe
:: -------------------------------------------------------------------

setlocal EnableExtensions
pushd "%~dp0\.."
set ROOT=%CD%

:: Locate vcvars64.bat — adjust if you have Community/Pro/Enterprise.
set VCVARS="C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if not exist %VCVARS% (
    set VCVARS="C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
)
if not exist %VCVARS% (
    echo [build_preview] vcvars64.bat not found. Edit scripts\build_preview.bat to set VCVARS.
    popd
    exit /b 1
)

call %VCVARS% >nul
if errorlevel 1 (
    echo [build_preview] Failed to initialise MSVC environment.
    popd
    exit /b 1
)

set OUTDIR=%ROOT%\bin\preview
set OBJDIR=%OUTDIR%\obj
if not exist "%OUTDIR%" mkdir "%OUTDIR%"
if not exist "%OBJDIR%" mkdir "%OBJDIR%"

set IMGUI=%ROOT%\deps\imgui
set BACKENDS=%IMGUI%\backends
set UI=%ROOT%\mariusfx\ui

set SOURCES=^
 "%UI%\preview\preview_main.cpp"^
 "%UI%\preview\preview_runtime.cpp"^
 "%UI%\ui.cpp"^
 "%UI%\theme.cpp"^
 "%IMGUI%\imgui.cpp"^
 "%IMGUI%\imgui_draw.cpp"^
 "%IMGUI%\imgui_widgets.cpp"^
 "%IMGUI%\imgui_tables.cpp"^
 "%BACKENDS%\imgui_impl_win32.cpp"^
 "%BACKENDS%\imgui_impl_dx11.cpp"

set INCLUDES=/I "%IMGUI%" /I "%BACKENDS%" /I "%UI%"

set DEFINES=/DMARIUSFX_PREVIEW=1 /DUNICODE /D_UNICODE /DNOMINMAX /D_CRT_SECURE_NO_WARNINGS

:: /MD for runtime DLL ; /EHsc for ImGui exception support ; /Zc:__cplusplus
:: so that ui.cpp's `if constexpr` etc. detect C++17.
set CXXFLAGS=/nologo /std:c++17 /EHsc /MD /O2 /W3 /Zc:__cplusplus /Fo"%OBJDIR%\\"

set LINKFLAGS=/nologo /SUBSYSTEM:WINDOWS /OUT:"%OUTDIR%\preview.exe"
set LIBS=user32.lib gdi32.lib d3d11.lib dxgi.lib d3dcompiler.lib imm32.lib shell32.lib

echo [build_preview] Compiling…
cl %CXXFLAGS% %DEFINES% %INCLUDES% %SOURCES% /link %LINKFLAGS% %LIBS%
set ERR=%ERRORLEVEL%

popd
if %ERR% NEQ 0 (
    echo [build_preview] FAILED ^(code %ERR%^)
    exit /b %ERR%
)
echo [build_preview] OK -^> %ROOT%\bin\preview\preview.exe
exit /b 0
