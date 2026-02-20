@echo off
setlocal EnableDelayedExpansion
rem ============================================================================
rem  AutoLens — Build + Package Script
rem  Creates both a portable ZIP and a Windows NSIS installer (.exe)
rem
rem  WHAT THIS SCRIPT DOES (in order):
rem    1. Find MSBuild via vswhere (Visual Studio's locator tool)
rem    2. Configure CMake for a Release build (separate folder: build_release\)
rem    3. Compile with MSBuild (Release config, x64 platform)
rem    4. Run windeployqt --release to bundle all Qt DLLs + QML plugins
rem    5. Stage files into dist\AutoLens\ (clean staging area)
rem    6. Copy optional extras (vxlapi64.dll, sample DBC)
rem    7. Create ZIP package with 7-Zip
rem    8. Compile NSIS script to create Setup.exe installer
rem
rem  PREREQUISITES (all already confirmed installed on this machine):
rem    - Visual Studio 2022 with C++ workload
rem    - Qt 6.10.2 msvc2022_64 at C:\Qt\6.10.2\
rem    - CMake at C:\Qt\Tools\CMake_64\bin\cmake.exe
rem    - 7-Zip at C:\Program Files\7-Zip\7z.exe
rem    - NSIS at C:\Program Files (x86)\NSIS\makensis.exe
rem
rem  USAGE:
rem    build_installer.bat              — uses default version 1.0.0
rem    build_installer.bat 1.2.0        — override version string
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
rem  %~dp0 expands to the drive+path of the batch file, with trailing backslash.
rem  We strip the trailing backslash for cleaner path concatenation.
rem ---------------------------------------------------------------------------
set SCRIPT_DIR=%~dp0
set SCRIPT_DIR=%SCRIPT_DIR:~0,-1%

rem ---------------------------------------------------------------------------
rem  Key Paths — centralised here so they're easy to update
rem ---------------------------------------------------------------------------
set QT_DIR=C:\Qt\6.10.2\msvc2022_64
set QT_BIN=%QT_DIR%\bin
set CMAKE=C:\Qt\Tools\CMake_64\bin\cmake.exe
set WINDEPLOYQT=%QT_BIN%\windeployqt.exe
set SEVEN_ZIP=C:\Program Files\7-Zip\7z.exe
set MAKENSIS=C:\Program Files (x86)\NSIS\makensis.exe

rem  Optional: Vector XL DLL (copied if present; silent skip if not)
set VECTOR_DLL=C:\VectorXL\bin\vxlapi64.dll

rem  Build and staging directories (relative to the project root)
set BUILD_DIR=%SCRIPT_DIR%\build_release
set DIST_STAGE=%SCRIPT_DIR%\dist\%APP_NAME%
set DIST_OUT=%SCRIPT_DIR%\dist
set NSI_SCRIPT=%SCRIPT_DIR%\installer\AutoLens.nsi

rem ---------------------------------------------------------------------------
rem  STEP 0: Verify tools exist before doing any work
rem ---------------------------------------------------------------------------
echo [0/8] Verifying prerequisites...

if not exist "%CMAKE%" (
    echo ERROR: CMake not found at %CMAKE%
    echo        Install Qt with CMake component or update the CMAKE path above.
    goto :Error
)
if not exist "%WINDEPLOYQT%" (
    echo ERROR: windeployqt not found at %WINDEPLOYQT%
    echo        Ensure Qt 6.10.2 msvc2022_64 is installed correctly.
    goto :Error
)
if not exist "%SEVEN_ZIP%" (
    echo ERROR: 7-Zip not found at %SEVEN_ZIP%
    echo        Download from https://7-zip.org and install.
    goto :Error
)
rem  NOTE: %MAKENSIS% path contains "(x86)" — parentheses crash the if ( ) block
rem  form in CMD. Use "if exist ... goto" instead (no block parentheses needed).
if exist "%MAKENSIS%" goto :Tools_OK
echo ERROR: NSIS not found at %MAKENSIS%
echo        Download from https://nsis.sourceforge.io and install.
goto :Error
:Tools_OK
echo        All tools found. OK.

rem ---------------------------------------------------------------------------
rem  STEP 1: Verify Visual Studio is installed via vswhere
rem  cmake --build will invoke MSBuild automatically through the VS generator.
rem  We still verify VS is present here to give a clear error early.
rem ---------------------------------------------------------------------------
echo.
echo [1/8] Verifying Visual Studio 2022 via vswhere...

set VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe
rem  Same (x86) issue — use goto style
if exist "%VSWHERE%" goto :VsWhere_OK
echo ERROR: vswhere.exe not found. Is Visual Studio 2022 installed?
goto :Error
:VsWhere_OK

rem  Confirm at least one VS 2022 instance with the C++ build tools exists.
rem  vswhere -products * : find any edition (Community/Professional/Enterprise)
rem  vswhere returns exit code 0 even if nothing found, so check its output length.
"%VSWHERE%" -latest -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath > "%TEMP%\vs_path.txt"
set /p VS_PATH= < "%TEMP%\vs_path.txt"
del "%TEMP%\vs_path.txt" 2>nul

if not defined VS_PATH (
    echo ERROR: VS 2022 with C++ workload not found.
    echo        Install "Desktop development with C++" in the VS Installer.
    goto :Error
)
echo        Visual Studio: %VS_PATH%

rem ---------------------------------------------------------------------------
rem  STEP 2: Configure CMake — Release build in separate folder
rem  We use a separate build_release\ folder so Debug builds are untouched.
rem
rem  Key flags:
rem   -G "Visual Studio 17 2022" -A x64  : force MSVC x64 generator
rem   -DCMAKE_PREFIX_PATH                 : tell CMake where Qt is installed
rem   -DCMAKE_BUILD_TYPE=Release          : optimised, no debug symbols
rem   -Wno-dev                            : suppress CMake developer warnings
rem ---------------------------------------------------------------------------
echo.
echo [2/8] Configuring CMake (Release)...
echo        Source : %SCRIPT_DIR%
echo        Build  : %BUILD_DIR%

"%CMAKE%" -S "%SCRIPT_DIR%" -B "%BUILD_DIR%" ^
    -G "Visual Studio 17 2022" -A x64 ^
    -DCMAKE_PREFIX_PATH="%QT_DIR%" ^
    -DCMAKE_BUILD_TYPE=Release ^
    -Wno-dev

if errorlevel 1 (
    echo ERROR: CMake configuration failed.
    goto :Error
)
echo        CMake configuration succeeded.

rem ---------------------------------------------------------------------------
rem  STEP 3: Build via cmake --build (Release configuration)
rem
rem  WHY cmake --build instead of calling MSBuild directly?
rem  Qt 6 generates many interdependent helper projects (qmltyperegistration,
rem  automoc_json_extraction, copy_qml, etc.). When MSBuild runs them in
rem  parallel (/m), a race condition occurs: qmltyperegistrar tries to write
rem  to build_release\AutoLens\ before that directory exists, exiting with
rem  code 1 (MSB8066). cmake --build respects CMake's dependency graph and
rem  serialises steps that must run in order, so this race cannot happen.
rem
rem  --config Release : select Release from the multi-config VS generator
rem  --parallel       : still parallelise independent targets (safe with
rem                     CMake's graph) — omit if you want a clean serial build
rem ---------------------------------------------------------------------------
echo.
echo [3/8] Building (cmake --build, Release x64)...

"%CMAKE%" --build "%BUILD_DIR%" --config Release --parallel

if errorlevel 1 (
    echo ERROR: Build failed. Re-run with verbose output for details:
    echo        cmake --build "%BUILD_DIR%" --config Release --verbose
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
echo [4/8] Running windeployqt (Release)...

set RELEASE_EXE=%BUILD_DIR%\Release\%APP_NAME%.exe
if not exist "%RELEASE_EXE%" (
    echo ERROR: Release exe not found at %RELEASE_EXE%
    echo        Check build output above.
    goto :Error
)

"%WINDEPLOYQT%" ^
    --release ^
    --qmldir "%SCRIPT_DIR%" ^
    --no-translations ^
    --no-system-d3d-compiler ^
    "%RELEASE_EXE%"

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
xcopy /E /I /Y /H "%BUILD_DIR%\Release\*" "%DIST_STAGE%\"

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
echo Built with Qt 6.10.2 + MSVC 2022
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

set ZIP_FILE=%DIST_OUT%\%APP_NAME%-v%APP_VERSION%-win64.zip

rem  We zip the DIST_STAGE folder — 7-Zip will include the folder name in the archive
"%SEVEN_ZIP%" a -tzip -mx=9 -mmt "%ZIP_FILE%" "%DIST_STAGE%"

if errorlevel 1 (
    echo ERROR: 7-Zip failed to create ZIP archive.
    goto :Error
)
echo        ZIP created: %ZIP_FILE%

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

if not exist "%NSI_SCRIPT%" (
    echo ERROR: NSIS script not found at %NSI_SCRIPT%
    echo        Ensure installer\AutoLens.nsi exists in the project.
    goto :Error
)

rem  Check if LICENSE.txt exists — if not, patch the NSI to skip the license page
rem  (NSIS will error if !insertmacro MUI_PAGE_LICENSE references a missing file)
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

"%MAKENSIS%" ^
    /DAPP_VERSION="%APP_VERSION%" ^
    /DDIST_DIR="%DIST_STAGE%" ^
    /DOUTPUT_DIR="%DIST_OUT%" ^
    "%NSI_SCRIPT%"

if errorlevel 1 (
    echo ERROR: NSIS compilation failed. See output above.
    goto :Error
)
echo        Installer created: %DIST_OUT%\%APP_NAME%-v%APP_VERSION%-Setup.exe

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
