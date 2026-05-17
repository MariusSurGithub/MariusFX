@echo off
:: ----------------------------------------------------------------------------
:: MariusFX UI hot-reload DLL build.
::
:: Compiles ui.cpp + theme.cpp + exports.cpp + a private copy of ImGui into
:: bin\hot\MariusFXUI.dll. The host (ReShade64.dll) LoadLibrary's this DLL,
:: detects mtime changes, and reloads it on the fly — no FiveM restart.
::
:: Output: bin\hot\MariusFXUI.dll
:: ----------------------------------------------------------------------------

setlocal EnableExtensions
pushd "%~dp0\.."
set ROOT=%CD%

set VCVARS="C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if not exist %VCVARS% (
    set VCVARS="C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
)
if not exist %VCVARS% (
    echo [build_ui_hot] vcvars64.bat not found. Edit scripts\build_ui_hot.bat to set VCVARS.
    popd
    exit /b 1
)

call %VCVARS% >nul
if errorlevel 1 (
    echo [build_ui_hot] Failed to initialise MSVC environment.
    popd
    exit /b 1
)

set OUTDIR=%ROOT%\bin\hot
set OBJDIR=%OUTDIR%\obj
if not exist "%OUTDIR%" mkdir "%OUTDIR%"
if not exist "%OBJDIR%" mkdir "%OBJDIR%"

set IMGUI=%ROOT%\deps\imgui
set UI=%ROOT%\mariusfx\ui

set SOURCES=^
 "%UI%\exports.cpp"^
 "%UI%\ui.cpp"^
 "%UI%\theme.cpp"^
 "%IMGUI%\imgui.cpp"^
 "%IMGUI%\imgui_draw.cpp"^
 "%IMGUI%\imgui_widgets.cpp"^
 "%IMGUI%\imgui_tables.cpp"^
 "%ROOT%\deps\imgui_config.cpp"

:: deps/ comes FIRST so IMGUI_USER_CONFIG="imgui_config.hpp" resolves to
:: deps/imgui_config.hpp (same as deps/ImGui.props uses for the host).
set INCLUDES=/I "%ROOT%\deps" /I "%IMGUI%" /I "%UI%" /I "%ROOT%\include" /I "%ROOT%\source"

:: MARIUSFX_HOT_DLL signals to ui.cpp that we are inside the hot-reload DLL,
:: so it can elide any host-only behaviour (currently nothing — ui.cpp is
:: the same code in every build flavour, but reserve the macro for future).
::
:: RESHADE_GUI / RESHADE_ADDON must match ReShade.vcxproj so source/runtime.hpp
:: exposes the same set of methods (mariusfx_get_technique_timing, etc. live
:: inside the RESHADE_GUI block).
::
:: IMGUI_DISABLE_DEMO_WINDOWS / IMGUI_DISABLE_DEBUG_TOOLS / IMGUI_DEBUG_PRINTF
:: MUST match deps/ImGui.vcxproj exactly. They alter the layout of
:: ImGuiContext, ImGuiIO, ImGuiWindow, etc. — a mismatch makes the DLL read
:: members at the wrong offsets the moment it touches a shared context.
:: ImGui defines MUST match deps/ImGui.props + deps/ImGui.vcxproj exactly.
:: Otherwise ImGuiContext / ImGuiIO have different sizes between host and DLL
:: and any GImGui-> access from the DLL reads at the wrong offsets and crashes.
::   - IMGUI_DISABLE_OBSOLETE_FUNCTIONS removes 32 bytes from ImGuiIO
::     (FontGlobalScale + 3 clipboard fn pointers).
::   - IMGUI_USER_CONFIG points at deps/imgui_config.hpp which #defines GImGui
::     to a thread_local (GImGuiThreadLocal); the matching definition lives
::     in deps/imgui_config.cpp which we now compile into the DLL.
::   - ImTextureID=ImU64 keeps ImDrawCmd identical to the host.
set DEFINES=/DMARIUSFX_HOT_DLL=1 /DMFXUI_BUILDING_DLL=1 /DRESHADE_GUI=1 /DRESHADE_ADDON=2 /DImTextureID=ImU64 "/DIMGUI_USER_CONFIG=\"imgui_config.hpp\"" /DIMGUI_DEFINE_MATH_OPERATORS /DIMGUI_DISABLE_OBSOLETE_FUNCTIONS /DIMGUI_DISABLE_FILE_FUNCTIONS /DIMGUI_DISABLE_WIN32_DEFAULT_IME_FUNCTIONS /DIMGUI_DISABLE_DEMO_WINDOWS /DIMGUI_DISABLE_DEBUG_TOOLS "/DIMGUI_DEBUG_PRINTF=((void)0)" /DNDEBUG /DUNICODE /D_UNICODE /DNOMINMAX /D_CRT_SECURE_NO_WARNINGS

:: /utf-8 = source AND execution charset are UTF-8. Without this MSVC
:: defaults the execution charset to the system code page (CP-1252 on
:: most FR/EN Windows installs), which warns on every Unicode glyph
:: literal in our comments and shortcut strings (↑, ↓, •, ─, …).
set CXXFLAGS=/nologo /std:c++17 /EHsc /MD /O2 /W3 /utf-8 /Zc:__cplusplus /Fo"%OBJDIR%\\"
set LINKFLAGS=/nologo /DLL /OUT:"%OUTDIR%\MariusFXUI.dll" /IMPLIB:"%OUTDIR%\MariusFXUI.lib" /PDB:"%OUTDIR%\MariusFXUI.pdb" /MAP:"%OUTDIR%\MariusFXUI.map" /MAPINFO:EXPORTS
set LIBS=user32.lib gdi32.lib shell32.lib

echo [build_ui_hot] Compiling MariusFXUI.dll...
cl %CXXFLAGS% %DEFINES% %INCLUDES% %SOURCES% /link %LINKFLAGS% %LIBS%
set ERR=%ERRORLEVEL%

popd
if %ERR% NEQ 0 (
    echo [build_ui_hot] FAILED ^(code %ERR%^)
    exit /b %ERR%
)
echo [build_ui_hot] OK -^> %ROOT%\bin\hot\MariusFXUI.dll
exit /b 0
