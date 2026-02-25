@echo off
setlocal EnableDelayedExpansion
rem ============================================================================
rem  AutoLens — Development Build Script
rem  Supports clean and incremental builds in Debug or Release mode.
rem
rem  USAGE:
rem    build.bat                — Incremental Debug build (default)
rem    build.bat debug          — Incremental Debug build
rem    build.bat release        — Incremental Release build
rem    build.bat clean          — Clean + rebuild Debug
rem    build.bat clean release  — Clean + rebuild Release
rem    build.bat configure      — Only run CMake configure (no compile)
rem    build.bat rebuild        — Force rebuild (no clean, rebuild all objects)
rem
rem  ENVIRONMENT OVERRIDES:
rem    SET AL_QT_DIR=C:\Qt\6.8.0\msvc2022_64   — Qt kit path
rem    SET AL_CMAKE=C:\path\to\cmake.exe        — CMake binary
rem    SET AL_GENERATOR=Ninja                   — CMake generator
rem    SET AL_JOBS=8                            — Parallel build jobs
rem ============================================================================

set SCRIPT_DIR=%~dp0
set SCRIPT_DIR=%SCRIPT_DIR:~0,-1%
cd /d "%SCRIPT_DIR%"

rem ---------------------------------------------------------------------------
rem  Parse arguments
rem ---------------------------------------------------------------------------
set BUILD_TYPE=Debug
set DO_CLEAN=0
set CONFIGURE_ONLY=0
set FORCE_REBUILD=0

:ParseArgs
if "%~1"=="" goto :DoneArgs
if /i "%~1"=="debug"     ( set BUILD_TYPE=Debug&   shift& goto :ParseArgs )
if /i "%~1"=="release"   ( set BUILD_TYPE=Release& shift& goto :ParseArgs )
if /i "%~1"=="clean"     ( set DO_CLEAN=1&         shift& goto :ParseArgs )
if /i "%~1"=="configure" ( set CONFIGURE_ONLY=1&   shift& goto :ParseArgs )
if /i "%~1"=="rebuild"   ( set FORCE_REBUILD=1&    shift& goto :ParseArgs )
echo WARNING: Unknown argument "%~1", ignoring.
shift
goto :ParseArgs
:DoneArgs

rem  Build directory per configuration
set BUILD_DIR=%SCRIPT_DIR%\build_%BUILD_TYPE%

echo.
echo ============================================================
echo   AutoLens Development Build
echo   Config : %BUILD_TYPE%
echo   Dir    : %BUILD_DIR%
echo   Action : %ACTION%
echo ============================================================
echo.

rem ---------------------------------------------------------------------------
rem  AUTO-DETECT: CMake
rem ---------------------------------------------------------------------------
echo [1/3] Detecting CMake...

if defined AL_CMAKE (
    set CMAKE=!AL_CMAKE!
    echo       Using AL_CMAKE: !CMAKE!
    goto :CMake_OK
)

where cmake.exe >nul 2>&1
if not errorlevel 1 (
    for /f "tokens=*" %%i in ('where cmake.exe') do (
        set CMAKE=%%i
        echo       Found on PATH: !CMAKE!
        goto :CMake_OK
    )
)

for /d %%d in ("C:\Qt\Tools\CMake_64" "C:\Qt\Tools\cmake_64") do (
    if exist "%%~d\bin\cmake.exe" (
        set CMAKE=%%~d\bin\cmake.exe
        echo       Found Qt-bundled: !CMAKE!
        goto :CMake_OK
    )
)

for %%p in (
    "%ProgramFiles%\CMake\bin\cmake.exe"
    "%ProgramFiles(x86)%\CMake\bin\cmake.exe"
) do (
    if exist %%p (
        set CMAKE=%%~p
        echo       Found at: !CMAKE!
        goto :CMake_OK
    )
)

echo ERROR: CMake not found. Install CMake or set AL_CMAKE.
goto :Error

:CMake_OK
if not exist "!CMAKE!" (
    echo ERROR: CMake path invalid: !CMAKE!
    goto :Error
)
"!CMAKE!" --version 2>nul | findstr /i "version"

rem ---------------------------------------------------------------------------
rem  AUTO-DETECT: Qt Installation
rem ---------------------------------------------------------------------------
echo.
echo [2/3] Detecting Qt...

if defined AL_QT_DIR (
    set QT_DIR=!AL_QT_DIR!
    echo       Using AL_QT_DIR: !QT_DIR!
    goto :Qt_OK
)

if defined CMAKE_PREFIX_PATH (
    if exist "%CMAKE_PREFIX_PATH%\bin\qmake.exe" (
        set QT_DIR=%CMAKE_PREFIX_PATH%
        echo       Using CMAKE_PREFIX_PATH: !QT_DIR!
        goto :Qt_OK
    )
)

set QT_BEST=
for /d %%v in (C:\Qt\6.*) do (
    for %%k in (msvc2022_64 msvc2019_64) do (
        if exist "%%v\%%k\bin\qmake.exe" (
            set QT_BEST=%%v\%%k
        )
    )
)

if defined QT_BEST (
    set QT_DIR=!QT_BEST!
    echo       Auto-detected: !QT_DIR!
    goto :Qt_OK
)

echo ERROR: Qt not found. Set AL_QT_DIR.
echo        e.g.  set AL_QT_DIR=C:\Qt\6.8.0\msvc2022_64
goto :Error

:Qt_OK

rem ---------------------------------------------------------------------------
rem  AUTO-DETECT: Visual Studio (for vcvarsall.bat if not already in env)
rem ---------------------------------------------------------------------------
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"

rem  Check if cl.exe is already available (VS Developer Command Prompt)
where cl.exe >nul 2>&1
if not errorlevel 1 (
    echo       Compiler already in PATH.
    goto :Compiler_OK
)

echo       Setting up MSVC environment...
set VS_PATH=
if exist "!VSWHERE!" (
    for /f "tokens=*" %%i in ('"!VSWHERE!" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath 2^>nul') do (
        set "VS_PATH=%%i"
    )
)

if not defined VS_PATH (
    rem  Fallback to common VS locations
    for %%y in (2022 2019) do (
        for %%e in (Community Professional Enterprise BuildTools) do (
            if exist "!ProgramFiles!\Microsoft Visual Studio\%%y\%%e\VC\Auxiliary\Build\vcvarsall.bat" (
                set "VS_PATH=!ProgramFiles!\Microsoft Visual Studio\%%y\%%e"
                goto :VS_Found
            )
            if exist "!ProgramFiles(x86)!\Microsoft Visual Studio\%%y\%%e\VC\Auxiliary\Build\vcvarsall.bat" (
                set "VS_PATH=!ProgramFiles(x86)!\Microsoft Visual Studio\%%y\%%e"
                goto :VS_Found
            )
        )
    )
)

:VS_Found
if not defined VS_PATH (
    echo ERROR: Visual Studio not found. Run from a VS Developer Command Prompt
    echo        or install Visual Studio with C++ Desktop workload.
    goto :Error
)

set VCVARSALL=!VS_PATH!\VC\Auxiliary\Build\vcvarsall.bat
if not exist "!VCVARSALL!" (
    echo ERROR: vcvarsall.bat not found at: !VCVARSALL!
    goto :Error
)

echo       Loading VS environment from: !VS_PATH!
call "!VCVARSALL!" x64 >nul 2>&1
if errorlevel 1 (
    echo ERROR: Failed to initialize MSVC environment.
    goto :Error
)
echo       MSVC environment loaded.

:Compiler_OK

rem ---------------------------------------------------------------------------
rem  Determine CMake Generator
rem ---------------------------------------------------------------------------
if defined AL_GENERATOR (
    set GENERATOR=!AL_GENERATOR!
) else (
    rem  Check if Ninja is available (faster builds)
    where ninja.exe >nul 2>&1
    if not errorlevel 1 (
        set GENERATOR=Ninja
    ) else (
        set GENERATOR=Ninja
        rem  Qt often bundles Ninja — check Qt tools
        if exist "!QT_DIR!\..\..\Tools\Ninja\ninja.exe" (
            set "PATH=!QT_DIR!\..\..\Tools\Ninja;!PATH!"
        ) else if exist "C:\Qt\Tools\Ninja\ninja.exe" (
            set "PATH=C:\Qt\Tools\Ninja;!PATH!"
        ) else (
            rem  Fall back to NMake if Ninja not found
            where ninja.exe >nul 2>&1
            if errorlevel 1 (
                set GENERATOR=NMake Makefiles
            )
        )
    )
)
echo       Generator: !GENERATOR!

rem  Parallel jobs
if not defined AL_JOBS (
    set AL_JOBS=%NUMBER_OF_PROCESSORS%
    if not defined AL_JOBS set AL_JOBS=4
)
echo       Parallel jobs: !AL_JOBS!

rem ---------------------------------------------------------------------------
rem  CLEAN: Remove build directory if requested
rem ---------------------------------------------------------------------------
if %DO_CLEAN%==1 (
    echo.
    echo [CLEAN] Removing %BUILD_DIR% ...
    if exist "%BUILD_DIR%" (
        rmdir /s /q "%BUILD_DIR%"
        echo       Cleaned.
    ) else (
        echo       Already clean.
    )
)

rem ---------------------------------------------------------------------------
rem  CONFIGURE: Run CMake if needed
rem ---------------------------------------------------------------------------
echo.
echo [3/3] Building...

set NEEDS_CONFIGURE=0
if not exist "%BUILD_DIR%\CMakeCache.txt" set NEEDS_CONFIGURE=1
if %DO_CLEAN%==1 set NEEDS_CONFIGURE=1

if %NEEDS_CONFIGURE%==1 (
    echo.
    echo --- CMake Configure ---
    if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"

    "!CMAKE!" -S "%SCRIPT_DIR%" -B "%BUILD_DIR%" ^
        -G "!GENERATOR!" ^
        -DCMAKE_BUILD_TYPE=%BUILD_TYPE% ^
        -DCMAKE_PREFIX_PATH="!QT_DIR!"

    if errorlevel 1 (
        echo.
        echo ERROR: CMake configure failed.
        goto :Error
    )
    echo       Configure OK.
) else (
    echo       Using existing CMake cache.
)

if %CONFIGURE_ONLY%==1 (
    echo.
    echo Done. CMake configured at: %BUILD_DIR%
    goto :Success
)

rem ---------------------------------------------------------------------------
rem  BUILD: Compile the project
rem ---------------------------------------------------------------------------
echo.
echo --- Build %BUILD_TYPE% ---

set BUILD_OPTS=--parallel !AL_JOBS!
if %FORCE_REBUILD%==1 set BUILD_OPTS=!BUILD_OPTS! --clean-first

set BUILD_START=%TIME%

"!CMAKE!" --build "%BUILD_DIR%" --config %BUILD_TYPE% !BUILD_OPTS!

if errorlevel 1 (
    echo.
    echo ============================================================
    echo   BUILD FAILED
    echo ============================================================
    goto :Error
)

set BUILD_END=%TIME%

rem ---------------------------------------------------------------------------
rem  Success summary
rem ---------------------------------------------------------------------------
:Success
echo.
echo ============================================================
echo   BUILD SUCCEEDED
echo   Config  : %BUILD_TYPE%
echo   Dir     : %BUILD_DIR%
if defined BUILD_START (
    echo   Started : %BUILD_START%
    echo   Ended   : %BUILD_END%
)
echo ============================================================
echo.

rem  Show the output executable path
for %%f in ("%BUILD_DIR%\AutoLens.exe" "%BUILD_DIR%\%BUILD_TYPE%\AutoLens.exe") do (
    if exist "%%~f" (
        echo   Executable: %%~f
        echo.
    )
)

endlocal
exit /b 0

:Error
echo.
echo Build failed. See errors above.
endlocal
exit /b 1
