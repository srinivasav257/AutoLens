@echo off
setlocal EnableDelayedExpansion
rem ============================================================================
rem  AutoLens — Build + Package Script
rem  Creates both a portable ZIP and a Windows NSIS installer (.exe)
rem
rem  WHAT THIS SCRIPT DOES (in order):
rem    1. Auto-detect or use user-supplied CMake, Qt, compiler, and tools
rem    2. Configure CMake for a Release build (separate folder: build_release\)
rem    3. Compile with the chosen generator / compiler
rem    4. Run windeployqt --release to bundle all Qt DLLs + QML plugins
rem    5. Stage files into dist\AutoLens\ (clean staging area)
rem    6. Copy optional extras (vxlapi64.dll, sample DBC)
rem    7. Create ZIP package with 7-Zip
rem    8. Compile NSIS script to create Setup.exe installer
rem
rem  FLEXIBLE CONFIGURATION — override via environment variables or arguments:
rem    SET AL_QT_DIR=C:\Qt\6.8.0\msvc2019_64   — custom Qt installation
rem    SET AL_CMAKE=C:\path\to\cmake.exe        — custom CMake binary
rem    SET AL_GENERATOR=Ninja                   — cmake generator (default: auto)
rem    SET AL_ARCH=x64                          — architecture (default: x64)
rem    SET AL_BUILD_TYPE=Release                — build type (default: Release)
rem    SET AL_7ZIP=C:\path\to\7z.exe            — custom 7-Zip path
rem    SET AL_NSIS=C:\path\to\makensis.exe      — custom NSIS path
rem    SET AL_VECTOR_DLL=C:\path\to\vxlapi64.dll
rem    SET AL_SKIP_PACKAGE=1                    — skip ZIP + NSIS steps (build only)
rem
rem  USAGE:
rem    build_installer.bat                     — auto-detect everything, v1.0.0
rem    build_installer.bat 1.2.0               — override version string
rem    set AL_GENERATOR=Ninja & build_installer.bat 2.0.0 — use Ninja generator
rem ============================================================================

rem ---------------------------------------------------------------------------
rem  Version: passed as first argument or default to 1.0.0
rem ---------------------------------------------------------------------------
set APP_VERSION=%~1
if "%APP_VERSION%"=="" set APP_VERSION=1.0.0

set APP_NAME=AutoLens

echo.
echo ============================================================
echo   %APP_NAME% Installer Builder  v%APP_VERSION%
echo ============================================================
echo.

rem ---------------------------------------------------------------------------
rem  Resolve the directory where THIS script lives
rem ---------------------------------------------------------------------------
set SCRIPT_DIR=%~dp0
set SCRIPT_DIR=%SCRIPT_DIR:~0,-1%

rem ---------------------------------------------------------------------------
rem  Build Configuration — defaults (overridable via AL_* env vars)
rem ---------------------------------------------------------------------------
if not defined AL_BUILD_TYPE set AL_BUILD_TYPE=Release
if not defined AL_ARCH       set AL_ARCH=x64

rem ---------------------------------------------------------------------------
rem  AUTO-DETECT: CMake
rem  Priority: AL_CMAKE env var > PATH > Qt bundled > common install locations
rem ---------------------------------------------------------------------------
echo [0/8] Detecting tools...
echo.
echo --- CMake ---

if defined AL_CMAKE (
    set CMAKE=!AL_CMAKE!
    echo        Using AL_CMAKE override: !CMAKE!
    goto :CMake_Found
)

rem  Try cmake on PATH first
where cmake.exe >nul 2>&1
if not errorlevel 1 (
    for /f "tokens=*" %%i in ('where cmake.exe') do (
        set CMAKE=%%i
        echo        Found cmake on PATH: !CMAKE!
        goto :CMake_Found
    )
)

rem  Try Qt-bundled CMake (scan C:\Qt\Tools for any CMake_64 folder)
for /d %%d in ("C:\Qt\Tools\CMake_64" "C:\Qt\Tools\cmake_64") do (
    if exist "%%~d\bin\cmake.exe" (
        set CMAKE=%%~d\bin\cmake.exe
        echo        Found Qt-bundled cmake: !CMAKE!
        goto :CMake_Found
    )
)

rem  Try common install locations
for %%p in (
    "%ProgramFiles%\CMake\bin\cmake.exe"
    "%ProgramFiles(x86)%\CMake\bin\cmake.exe"
    "C:\CMake\bin\cmake.exe"
) do (
    if exist %%p (
        set CMAKE=%%~p
        echo        Found cmake at: !CMAKE!
        goto :CMake_Found
    )
)

echo ERROR: CMake not found. Install CMake or set AL_CMAKE environment variable.
echo        e.g.  set AL_CMAKE=C:\Qt\Tools\CMake_64\bin\cmake.exe
goto :Error

:CMake_Found
if not exist "!CMAKE!" (
    echo ERROR: CMake path does not exist: !CMAKE!
    goto :Error
)

rem  Print CMake version for diagnostics
"!CMAKE!" --version 2>nul | findstr /i "version"

rem ---------------------------------------------------------------------------
rem  AUTO-DETECT: Qt Installation
rem  Priority: AL_QT_DIR env var > CMAKE_PREFIX_PATH > scan C:\Qt\
rem
rem  When scanning, we look for the newest Qt version that has an msvc* kit
rem  matching the detected compiler. Falls back to any available kit.
rem ---------------------------------------------------------------------------
echo.
echo --- Qt ---

if defined AL_QT_DIR (
    set QT_DIR=!AL_QT_DIR!
    echo        Using AL_QT_DIR override: !QT_DIR!
    goto :Qt_Found
)

rem  Check CMAKE_PREFIX_PATH (commonly set by Qt users)
if defined CMAKE_PREFIX_PATH (
    if exist "%CMAKE_PREFIX_PATH%\bin\qmake.exe" (
        set QT_DIR=%CMAKE_PREFIX_PATH%
        echo        Using CMAKE_PREFIX_PATH: !QT_DIR!
        goto :Qt_Found
    )
)

rem  Auto-scan C:\Qt\ for the latest version with an msvc kit
set QT_BEST=
set QT_BEST_VER=0.0.0
for /d %%v in (C:\Qt\6.*) do (
    rem  Try msvc2022_64 first, then msvc2019_64, then any msvc* kit
    for %%k in (msvc2022_64 msvc2019_64) do (
        if exist "%%v\%%k\bin\qmake.exe" (
            set QT_BEST=%%v\%%k
            set QT_BEST_VER=%%~nxv
        )
    )
)

if defined QT_BEST (
    set QT_DIR=!QT_BEST!
    echo        Auto-detected Qt !QT_BEST_VER! at: !QT_DIR!
    goto :Qt_Found
)

rem  Fallback: scan for any Qt 6 kit
for /d %%v in (C:\Qt\6.*) do (
    for /d %%k in ("%%v\*") do (
        if exist "%%k\bin\qmake.exe" (
            set "QT_DIR=%%k"
            echo        Auto-detected Qt at: !QT_DIR!
            goto :Qt_Found
        )
    )
)

echo ERROR: Qt installation not found. Set AL_QT_DIR to your Qt kit path.
echo        e.g.  set AL_QT_DIR=C:\Qt\6.8.0\msvc2022_64
goto :Error

:Qt_Found
if not exist "!QT_DIR!\bin" (
    echo ERROR: Qt bin directory not found at !QT_DIR!\bin
    goto :Error
)
set QT_BIN=!QT_DIR!\bin
set WINDEPLOYQT=!QT_BIN!\windeployqt.exe

rem ---------------------------------------------------------------------------
rem  AUTO-DETECT: Visual Studio / Compiler via vswhere
rem  Determines the correct CMake generator string automatically.
rem  Priority: AL_GENERATOR env var > auto-detect via vswhere
rem ---------------------------------------------------------------------------
echo.
echo --- Compiler / Generator ---

set VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe
set VS_PATH=
set VS_YEAR=
set VS_VER_MAJOR=

rem  Locate vswhere and query for VS with C++ tools
rem  -products * : find any product including standalone Build Tools
if exist "!VSWHERE!" (
    rem  Get installation path
    "!VSWHERE!" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath > "%TEMP%\vs_path.txt"
    set /p VS_PATH= < "%TEMP%\vs_path.txt"
    del "%TEMP%\vs_path.txt" 2>nul

    rem  Get VS version number (e.g. 17.x for VS 2022, 16.x for VS 2019)
    "!VSWHERE!" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property catalog_productLineVersion > "%TEMP%\vs_year.txt"
    set /p VS_YEAR= < "%TEMP%\vs_year.txt"
    del "%TEMP%\vs_year.txt" 2>nul

    "!VSWHERE!" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationVersion > "%TEMP%\vs_ver.txt"
    set /p VS_FULL_VER= < "%TEMP%\vs_ver.txt"
    del "%TEMP%\vs_ver.txt" 2>nul
    if defined VS_FULL_VER (
        for /f "tokens=1 delims=." %%m in ("!VS_FULL_VER!") do set VS_VER_MAJOR=%%m
    )
)

rem  Build the generator string if not overridden
if defined AL_GENERATOR (
    set CMAKE_GENERATOR=!AL_GENERATOR!
    echo        Using AL_GENERATOR override: !CMAKE_GENERATOR!
) else (
    if defined VS_YEAR (
        rem  Map VS major version to generator name
        if "!VS_VER_MAJOR!"=="17" (
            set CMAKE_GENERATOR=Visual Studio 17 2022
        ) else if "!VS_VER_MAJOR!"=="16" (
            set CMAKE_GENERATOR=Visual Studio 16 2019
        ) else if "!VS_VER_MAJOR!"=="15" (
            set CMAKE_GENERATOR=Visual Studio 15 2017
        ) else (
            set CMAKE_GENERATOR=Visual Studio !VS_VER_MAJOR! !VS_YEAR!
        )
        echo        Auto-detected: !CMAKE_GENERATOR! at !VS_PATH!
    ) else (
        rem  No VS found — try Ninja as fallback (common with standalone MSVC Build Tools)
        where ninja.exe >nul 2>&1
        if not errorlevel 1 (
            set CMAKE_GENERATOR=Ninja
            echo        Visual Studio not found; using Ninja generator.
        ) else (
            rem  Let CMake pick its default generator
            set CMAKE_GENERATOR=
            echo        WARNING: No VS or Ninja found. CMake will use its default generator.
            echo                 This may fail. Install VS 2019/2022 or set AL_GENERATOR.
        )
    )
)

rem  Determine if using a multi-config generator (VS generators) or single-config (Ninja, Makefiles)
set IS_MULTI_CONFIG=0
echo !CMAKE_GENERATOR! | findstr /i "Visual Studio" >nul 2>&1
if not errorlevel 1 set IS_MULTI_CONFIG=1

rem  For multi-config generators, architecture is passed via -A flag
rem  For single-config (Ninja), architecture comes from the vcvarsall environment
set CMAKE_ARCH_FLAG=
if !IS_MULTI_CONFIG! equ 1 (
    set CMAKE_ARCH_FLAG=-A %AL_ARCH%
)

rem ---------------------------------------------------------------------------
rem  AUTO-DETECT: 7-Zip
rem  Priority: AL_7ZIP env var > PATH > common install locations
rem ---------------------------------------------------------------------------
echo.
echo --- 7-Zip ---

if defined AL_7ZIP (
    set SEVEN_ZIP=!AL_7ZIP!
    echo        Using AL_7ZIP override: !SEVEN_ZIP!
    goto :7Zip_Check
)

where 7z.exe >nul 2>&1
if not errorlevel 1 (
    for /f "tokens=*" %%i in ('where 7z.exe') do (
        set SEVEN_ZIP=%%i
        echo        Found 7z on PATH: !SEVEN_ZIP!
        goto :7Zip_Check
    )
)

for %%p in (
    "%ProgramFiles%\7-Zip\7z.exe"
    "%ProgramFiles(x86)%\7-Zip\7z.exe"
    "C:\7-Zip\7z.exe"
) do (
    if exist %%p (
        set SEVEN_ZIP=%%~p
        echo        Found 7z at: !SEVEN_ZIP!
        goto :7Zip_Check
    )
)

set SEVEN_ZIP=
echo        7-Zip not found — ZIP packaging will be skipped.
echo        Install from https://7-zip.org or set AL_7ZIP.

:7Zip_Check

rem ---------------------------------------------------------------------------
rem  AUTO-DETECT: NSIS
rem  Priority: AL_NSIS env var > PATH > common install locations
rem ---------------------------------------------------------------------------
echo.
echo --- NSIS ---

if defined AL_NSIS (
    set MAKENSIS=!AL_NSIS!
    echo        Using AL_NSIS override: !MAKENSIS!
    goto :NSIS_Check
)

where makensis.exe >nul 2>&1
if not errorlevel 1 (
    for /f "tokens=*" %%i in ('where makensis.exe') do (
        set MAKENSIS=%%i
        echo        Found makensis on PATH: !MAKENSIS!
        goto :NSIS_Check
    )
)

rem  Because (x86) contains parens, test each path with goto style
set "_NSIS_TEST=%ProgramFiles(x86)%\NSIS\makensis.exe"
if exist "!_NSIS_TEST!" (
    set MAKENSIS=!_NSIS_TEST!
    echo        Found makensis at: !MAKENSIS!
    goto :NSIS_Check
)
set "_NSIS_TEST=%ProgramFiles%\NSIS\makensis.exe"
if exist "!_NSIS_TEST!" (
    set MAKENSIS=!_NSIS_TEST!
    echo        Found makensis at: !MAKENSIS!
    goto :NSIS_Check
)

set MAKENSIS=
echo        NSIS not found — installer creation will be skipped.
echo        Install from https://nsis.sourceforge.io or set AL_NSIS.

:NSIS_Check

rem ---------------------------------------------------------------------------
rem  Optional: Vector XL DLL path
rem ---------------------------------------------------------------------------
if not defined AL_VECTOR_DLL set AL_VECTOR_DLL=C:\VectorXL\bin\vxlapi64.dll
set VECTOR_DLL=!AL_VECTOR_DLL!

rem ---------------------------------------------------------------------------
rem  Build and staging directories
rem ---------------------------------------------------------------------------
set BUILD_DIR=%SCRIPT_DIR%\build_release
set DIST_STAGE=%SCRIPT_DIR%\dist\%APP_NAME%
set DIST_OUT=%SCRIPT_DIR%\dist
set NSI_SCRIPT=%SCRIPT_DIR%\installer\AutoLens.nsi

rem ---------------------------------------------------------------------------
rem  Summary of detected configuration
rem ---------------------------------------------------------------------------
echo.
echo ============================================================
echo   Configuration Summary
echo ============================================================
echo   CMake      : !CMAKE!
echo   Qt         : !QT_DIR!
echo   Generator  : !CMAKE_GENERATOR!
echo   Arch       : %AL_ARCH%
echo   Build Type : %AL_BUILD_TYPE%
echo   Build Dir  : %BUILD_DIR%
if defined SEVEN_ZIP echo   7-Zip      : !SEVEN_ZIP!
if defined MAKENSIS echo   NSIS       : !MAKENSIS!
echo ============================================================
echo.

rem ---------------------------------------------------------------------------
rem  STEP 0: Verify windeployqt exists (critical for Qt deployment)
rem ---------------------------------------------------------------------------
echo [0/8] Verifying prerequisites...

if not exist "!WINDEPLOYQT!" (
    rem  Try windeployqt6 as a fallback (some Qt installations name it this way)
    if exist "!QT_BIN!\windeployqt6.exe" (
        set WINDEPLOYQT=!QT_BIN!\windeployqt6.exe
        echo        Using windeployqt6.exe
    ) else (
        echo ERROR: windeployqt not found at !WINDEPLOYQT!
        echo        Check your Qt installation at !QT_DIR!
        goto :Error
    )
)
echo        All critical tools verified. OK.

rem ---------------------------------------------------------------------------
rem  STEP 1: Set up compiler environment for single-config generators
rem  For Ninja/Makefiles, we need to run vcvarsall.bat so cl.exe is on PATH.
rem  For VS generators, MSBuild handles this internally.
rem ---------------------------------------------------------------------------
echo.
echo [1/8] Setting up compiler environment...

if !IS_MULTI_CONFIG! equ 0 (
    if defined VS_PATH (
        rem  Find vcvarsall.bat
        set "VCVARSALL=!VS_PATH!\VC\Auxiliary\Build\vcvarsall.bat"
        if exist "!VCVARSALL!" (
            echo        Initialising MSVC environment via vcvarsall.bat...
            call "!VCVARSALL!" %AL_ARCH% >nul 2>&1
            echo        MSVC environment ready.
        ) else (
            echo        WARNING: vcvarsall.bat not found. Compiler may not be on PATH.
        )
    ) else (
        echo        No VS detected — assuming compiler is already on PATH.
        echo        ^(e.g. MinGW, Clang, or a Developer Command Prompt^)
    )
) else (
    echo        Multi-config generator — compiler env handled by MSBuild.
    if defined VS_PATH echo        Visual Studio: !VS_PATH!
)

rem ---------------------------------------------------------------------------
rem  STEP 2: Configure CMake — Release build in separate folder
rem
rem  Multi-config generators (VS): CMAKE_BUILD_TYPE is ignored; --config is used
rem  Single-config generators (Ninja): CMAKE_BUILD_TYPE selects the config
rem ---------------------------------------------------------------------------
echo.
echo [2/8] Configuring CMake (%AL_BUILD_TYPE%)...
echo        Source : %SCRIPT_DIR%
echo        Build  : %BUILD_DIR%

set CMAKE_CONFIGURE_CMD="!CMAKE!" -S "%SCRIPT_DIR%" -B "%BUILD_DIR%"

rem  Add generator if we detected/specified one
if defined CMAKE_GENERATOR (
    set CMAKE_CONFIGURE_CMD=!CMAKE_CONFIGURE_CMD! -G "!CMAKE_GENERATOR!"
)

rem  Add architecture flag for multi-config generators
if defined CMAKE_ARCH_FLAG (
    set CMAKE_CONFIGURE_CMD=!CMAKE_CONFIGURE_CMD! !CMAKE_ARCH_FLAG!
)

rem  Add Qt prefix path and build type
set CMAKE_CONFIGURE_CMD=!CMAKE_CONFIGURE_CMD! -DCMAKE_PREFIX_PATH="!QT_DIR!"
set CMAKE_CONFIGURE_CMD=!CMAKE_CONFIGURE_CMD! -DCMAKE_BUILD_TYPE=%AL_BUILD_TYPE%
set CMAKE_CONFIGURE_CMD=!CMAKE_CONFIGURE_CMD! -Wno-dev

echo        Command: !CMAKE_CONFIGURE_CMD!
echo.

!CMAKE_CONFIGURE_CMD!

if errorlevel 1 (
    echo ERROR: CMake configuration failed.
    goto :Error
)
echo        CMake configuration succeeded.

rem  If CMAKE_GENERATOR was unset (CMake picked its own default), detect it
rem  from the CMakeCache so we know the correct output layout for later steps.
if !IS_MULTI_CONFIG! equ 0 (
    if exist "%BUILD_DIR%\CMakeCache.txt" (
        for /f "tokens=2 delims==" %%g in ('findstr /b "CMAKE_GENERATOR:INTERNAL" "%BUILD_DIR%\CMakeCache.txt"') do (
            set "DETECTED_GENERATOR=%%g"
        )
        if defined DETECTED_GENERATOR (
            echo !DETECTED_GENERATOR! | findstr /i "Visual Studio" >nul 2>&1
            if not errorlevel 1 (
                set IS_MULTI_CONFIG=1
                echo        Detected multi-config generator: !DETECTED_GENERATOR!
            )
        )
    )
)

rem ---------------------------------------------------------------------------
rem  STEP 3: Build via cmake --build
rem
rem  --config is used for multi-config generators (VS) to pick Release/Debug.
rem  For single-config (Ninja), it's harmless (ignored). --parallel enables
rem  parallel compilation across independent targets.
rem ---------------------------------------------------------------------------
echo.
echo [3/8] Building (cmake --build, %AL_BUILD_TYPE% %AL_ARCH%)...

"!CMAKE!" --build "%BUILD_DIR%" --config %AL_BUILD_TYPE% --parallel

if errorlevel 1 (
    echo ERROR: Build failed. Re-run with verbose output for details:
    echo        cmake --build "%BUILD_DIR%" --config %AL_BUILD_TYPE% --verbose
    goto :Error
)
echo        Build succeeded.

rem ---------------------------------------------------------------------------
rem  STEP 4: Run windeployqt on the Release exe
rem  windeployqt inspects the .exe and copies all needed Qt*.dll files,
rem  QML plugins, platform plugins, imageformat plugins, etc. next to the exe.
rem
rem  --release          : copy release DLLs (not debug *d.dll variants)
rem  --qmldir           : path to source QML files (needed for QML import scan)
rem  --no-translations  : skip Qt translation files (saves ~5 MB)
rem  --no-system-d3d-compiler : skip D3D compiler (we don't use Direct3D explicitly)
rem  --no-opengl-sw     : skip software OpenGL renderer (remove if GPU issues arise)
rem ---------------------------------------------------------------------------
echo.
echo [4/8] Running windeployqt (%AL_BUILD_TYPE%)...

rem  For multi-config generators (VS), the exe is in build\Release\ or build\Debug\
rem  For single-config generators (Ninja), the exe is directly in build\
if !IS_MULTI_CONFIG! equ 1 (
    set RELEASE_EXE=%BUILD_DIR%\%AL_BUILD_TYPE%\%APP_NAME%.exe
) else (
    set RELEASE_EXE=%BUILD_DIR%\%APP_NAME%.exe
)

if not exist "!RELEASE_EXE!" (
    echo ERROR: Executable not found at !RELEASE_EXE!
    echo        Check build output above.
    goto :Error
)

rem  Build windeployqt flags based on build type
set DEPLOY_FLAGS=--qmldir "%SCRIPT_DIR%" --no-translations --no-system-d3d-compiler
if /i "%AL_BUILD_TYPE%"=="Release" set DEPLOY_FLAGS=--release !DEPLOY_FLAGS!

"!WINDEPLOYQT!" !DEPLOY_FLAGS! "!RELEASE_EXE!"

if errorlevel 1 (
    echo ERROR: windeployqt failed.
    goto :Error
)
echo        windeployqt completed.

rem ---------------------------------------------------------------------------
rem  STEP 5: Stage distribution files
rem  We create a clean staging folder (dist\AutoLens\) and copy everything
rem  windeployqt prepared. This is what gets zipped and installed.
rem ---------------------------------------------------------------------------
echo.
echo [5/8] Staging distribution files...

rem  Clean and recreate the staging directory
if exist "%DIST_STAGE%" rmdir /s /q "%DIST_STAGE%"
if exist "%DIST_OUT%\%APP_NAME%-v%APP_VERSION%-win64.zip" del /q "%DIST_OUT%\%APP_NAME%-v%APP_VERSION%-win64.zip"
if exist "%DIST_OUT%\%APP_NAME%-v%APP_VERSION%-Setup.exe" del /q "%DIST_OUT%\%APP_NAME%-v%APP_VERSION%-Setup.exe"

mkdir "%DIST_STAGE%"
mkdir "%DIST_OUT%" 2>nul

rem  xcopy /E = copy directories and subdirectories (including empty)
rem  xcopy /I = assume destination is a directory
rem  xcopy /Y = suppress overwrite prompts
rem  xcopy /H = copy hidden files too
if !IS_MULTI_CONFIG! equ 1 (
    set _EXE_DIR=%BUILD_DIR%\%AL_BUILD_TYPE%
) else (
    set _EXE_DIR=%BUILD_DIR%
)
xcopy /E /I /Y /H "!_EXE_DIR!\*" "%DIST_STAGE%\"

if errorlevel 1 (
    echo ERROR: Failed to copy release files to staging area.
    goto :Error
)

rem  Remove files not needed at runtime (linker import library, debug DB)
del /q "%DIST_STAGE%\%APP_NAME%.exp" 2>nul
del /q "%DIST_STAGE%\%APP_NAME%.lib" 2>nul
del /q "%DIST_STAGE%\%APP_NAME%.pdb" 2>nul

echo        Staged to: %DIST_STAGE%

rem ---------------------------------------------------------------------------
rem  STEP 6: Copy optional extras
rem  vxlapi64.dll — Vector XL driver DLL (needed on remote test bench)
rem  Copy it if it exists; skip silently if not (demo mode still works).
rem ---------------------------------------------------------------------------
echo.
echo [6/8] Copying optional extras...

if exist "%VECTOR_DLL%" (
    copy /Y "%VECTOR_DLL%" "%DIST_STAGE%\" >nul
    echo        Copied vxlapi64.dll from Vector installation.
) else (
    echo        vxlapi64.dll not found — app will run in Demo mode without it.
    echo        ^(This is fine for testing without Vector hardware^)
)

rem  Copy a sample DBC file if one exists in the project
if exist "%SCRIPT_DIR%\sample.dbc" (
    copy /Y "%SCRIPT_DIR%\sample.dbc" "%DIST_STAGE%\" >nul
    echo        Copied sample.dbc
)

rem  Create a minimal README.txt in the package
(
echo %APP_NAME% v%APP_VERSION% — Portable Distribution
echo =====================================================
echo.
echo QUICK START:
echo   Run AutoLens.exe to start the application.
echo.
echo VECTOR HARDWARE:
echo   Copy vxlapi64.dll from your Vector installation next to AutoLens.exe
echo   to enable real CAN hardware. Without it, Demo mode is used.
echo.
echo REQUIREMENTS:
echo   - Windows 10 / 11 (64-bit)
echo   - Visual C++ Redistributable 2022 (x64)
echo     Download: https://aka.ms/vs/17/release/vc_redist.x64.exe
echo.
echo Built with Qt + CMake
) > "%DIST_STAGE%\README.txt"

echo        README.txt created.

rem ---------------------------------------------------------------------------
rem  STEP 7: Create ZIP package with 7-Zip
rem  The ZIP is a portable/no-install distribution — just extract and run.
rem
rem  7z a             : add files (create archive)
rem  -tzip            : ZIP format (cross-platform compatible)
rem  -mx=9            : maximum compression level
rem  -mmt             : use multiple threads (faster)
rem  The archive wraps everything inside an "AutoLens\" folder inside the ZIP
rem  so extracting doesn't scatter files into the current directory.
rem ---------------------------------------------------------------------------
echo.
echo [7/8] Creating ZIP package...

if defined AL_SKIP_PACKAGE (
    echo        AL_SKIP_PACKAGE is set — skipping ZIP packaging.
    goto :Skip_ZIP
)

if not defined SEVEN_ZIP (
    echo        7-Zip not available — skipping ZIP packaging.
    goto :Skip_ZIP
)
if not exist "!SEVEN_ZIP!" (
    echo        7-Zip not found at !SEVEN_ZIP! — skipping ZIP packaging.
    goto :Skip_ZIP
)

set ZIP_FILE=%DIST_OUT%\%APP_NAME%-v%APP_VERSION%-win64.zip

rem  We zip the DIST_STAGE folder — 7-Zip will include the folder name in the archive
"!SEVEN_ZIP!" a -tzip -mx=9 -mmt "%ZIP_FILE%" "%DIST_STAGE%"

if errorlevel 1 (
    echo ERROR: 7-Zip failed to create ZIP archive.
    goto :Error
)
echo        ZIP created: %ZIP_FILE%

:Skip_ZIP

rem ---------------------------------------------------------------------------
rem  STEP 8: Compile NSIS installer script
rem  makensis reads the .nsi script and produces a self-contained Setup.exe.
rem
rem  /D flags inject variables into the NSIS script (like #define in C):
rem    APP_VERSION  — version string shown in the installer title
rem    DIST_DIR     — path to the staged files to package
rem    OUTPUT_DIR   — where to write the Setup.exe
rem ---------------------------------------------------------------------------
echo.
echo [8/8] Compiling NSIS installer...

if defined AL_SKIP_PACKAGE (
    echo        AL_SKIP_PACKAGE is set — skipping installer creation.
    goto :Skip_NSIS
)

if not defined MAKENSIS (
    echo        NSIS not available — skipping installer creation.
    goto :Skip_NSIS
)
if not exist "!MAKENSIS!" (
    echo        NSIS not found at !MAKENSIS! — skipping installer creation.
    goto :Skip_NSIS
)

if not exist "%NSI_SCRIPT%" (
    echo        NSIS script not found at %NSI_SCRIPT% — skipping installer.
    goto :Skip_NSIS
)

rem  Check if LICENSE.txt exists — if not, create a placeholder
set LICENSE_FILE=%SCRIPT_DIR%\LICENSE.txt
if not exist "%LICENSE_FILE%" (
    echo        NOTE: LICENSE.txt not found — creating a placeholder.
    (
    echo AutoLens Software License
    echo.
    echo This software is provided for educational and evaluation purposes.
    echo All rights reserved.
    ) > "%LICENSE_FILE%"
)

"!MAKENSIS!" ^
    /DAPP_VERSION="%APP_VERSION%" ^
    /DDIST_DIR="%DIST_STAGE%" ^
    /DOUTPUT_DIR="%DIST_OUT%" ^
    "%NSI_SCRIPT%"

if errorlevel 1 (
    echo ERROR: NSIS compilation failed. See output above.
    goto :Error
)
echo        Installer created: %DIST_OUT%\%APP_NAME%-v%APP_VERSION%-Setup.exe

:Skip_NSIS

rem ---------------------------------------------------------------------------
rem  Summary
rem ---------------------------------------------------------------------------
echo.
echo ============================================================
echo   BUILD COMPLETE!
echo ============================================================
echo.
echo   Output files in: %DIST_OUT%\
echo.
echo   [ZIP]       %APP_NAME%-v%APP_VERSION%-win64.zip
echo               Portable — extract and run, no installation needed.
echo.
echo   [INSTALLER] %APP_NAME%-v%APP_VERSION%-Setup.exe
echo               Windows installer — creates Start Menu + uninstaller.
echo.
echo   Staged files: %DIST_STAGE%\
echo.
echo ============================================================
goto :End

:Error
echo.
echo ============================================================
echo   BUILD FAILED — see error messages above.
echo ============================================================
echo.
exit /b 1

:End
endlocal
exit /b 0
