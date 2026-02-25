cmake_minimum_required(VERSION 3.21)

# ============================================================================
#  AutoLens — Mini CANoe Replacement Tool
#  Qt6 / QML Automotive Learning Project
# ============================================================================
project(AutoLens VERSION 0.1.0 LANGUAGES CXX)

# ---------------------------------------------------------------------------
# C++ standard: C++17 required for structured bindings, std::optional, etc.
# ---------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---------------------------------------------------------------------------
# Qt auto-generation tools
#  AUTOMOC  — runs the Meta-Object Compiler on every .h with Q_OBJECT
#  AUTORCC  — compiles .qrc resource files into C++
# ---------------------------------------------------------------------------
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

# ---------------------------------------------------------------------------
# Qt6 packages
#   Core          — QObject, QThread, QTimer, QMutex, QAbstractTableModel …
#   Gui           — QGuiApplication, QColor …
#   Quick         — QML engine, QQuickView, QAbstractTableModel ↔ TableView
#   QuickControls2— Button, ToolBar, TableView, ScrollBar, TabBar …
# ---------------------------------------------------------------------------
find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Quick QuickControls2 QuickDialogs2)
#  QuickDialogs2 — backs the QtQuick.Dialogs QML module (FileDialog, ColorDialog…)
#  Without this, FileDialog is "not a type" at runtime.

# qt_standard_project_setup: enables Qt's recommended defaults for executables
# (unicode, build-dir layout, QML import path helpers). Requires Qt ≥ 6.5.
qt_standard_project_setup(REQUIRES 6.5)

# QTP0004 (Qt >= 6.8): generate qmldir entries for extra QML subdirectories
# (e.g. qml/pages, qml/components) to avoid configure-time warnings.
if(COMMAND qt_policy)
    qt_policy(SET QTP0004 NEW)
endif()

# ============================================================================
#  Application Sources
# ============================================================================
set(APP_SOURCES
    main.cpp

    # --- Hardware Abstraction Layer ---
    # CANInterface.cpp: stub that lets AUTOMOC generate ICANDriver's moc code.
    #   (ICANDriver has Q_OBJECT in a header — needs a .cpp to anchor moc output)
    # VectorCANDriver wraps Vector's XL Library via runtime DLL loading.
    # DemoCANDriver generates synthetic traffic for development without HW.
    src/hardware/CANInterface.cpp
    src/hardware/VectorCANDriver.cpp
    src/hardware/DemoCANDriver.cpp

    # --- DBC Parser ---
    # Reads Vector DBC database files (*.dbc) to obtain CAN message and
    # signal definitions used for decoding raw bytes into physical values.
    src/dbc/DBCParser.cpp

    # --- Trace Model ---
    # QAbstractTableModel that the QML TableView binds to.
    # Rows = received CAN frames; columns = timestamp/ID/DLC/data/decoded.
    src/trace/TraceModel.cpp

    # --- Trace Exporter ---
    # Saves captured frames to industry-standard Vector formats:
    #   ASC  — human-readable ASCII Log  (Vector CANalyzer compatible)
    #   BLF  — compact Binary Log File   (Vector CANalyzer / CANoe compatible)
    #   CSV  — comma-separated (handled inline in AppController::saveTrace)
    src/trace/TraceExporter.cpp
    src/trace/TraceImporter.cpp

    # --- Centralized Logger ---
    # Crash-resilient logging system: captures all qDebug/qWarning/qCritical,
    # writes rotating log files, ring-buffer crash marker, SEH handler.
    src/app/Logger.cpp

    # --- Application Controller ---
    # Singleton QObject exposed to QML via context property "AppController".
    # Owns the driver, DBC database, and trace model; bridges C++ ↔ QML.
    src/app/AppController.cpp
)

# ============================================================================
#  Executable + QML Module
#  qt_add_executable   — creates the binary target
#  qt_add_qml_module   — compiles QML files into Qt resources and registers
#                        them under the URI "AutoLens" (import AutoLens 1.0)
# ============================================================================
qt_add_executable(AutoLens ${APP_SOURCES})

qt_add_qml_module(AutoLens
    URI AutoLens
    VERSION 1.0
    QML_FILES
        BootstrapSplash.qml
        Main.qml
        qml/pages/TracePage.qml
        qml/pages/GeneratorPage.qml
        qml/pages/SimulationPage.qml
        qml/pages/DiagnosticsPage.qml
        qml/components/StatusBar.qml
        qml/components/CANConfigDialog.qml
        qml/components/SplashScreen.qml        # Startup overlay — fades out when initComplete
)

# ============================================================================
#  Include Directories
#   src/                 — allows #include "hardware/CANInterface.h" etc.
#   third_party/vector_xl— provides vxlapi.h (Vector XL Library header)
# ============================================================================
target_include_directories(AutoLens PRIVATE
    src
    third_party/vector_xl
)

# ============================================================================
#  Linked Libraries
# ============================================================================
target_link_libraries(AutoLens PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::QuickDialogs2   # FileDialog, FolderDialog, etc. from QtQuick.Dialogs
)

# Note: We do NOT link vxlapi.lib here.
# VectorCANDriver loads vxlapi64.dll at runtime via QLibrary, so the
# application compiles and starts even when Vector drivers are not installed.

# ============================================================================
#  Windows: Copy Qt DLLs next to the executable after build
#  windeployqt scans the binary and copies all Qt*.dll + QML plugin files.
#  Without this, the exe won't run on a machine that doesn't have Qt in PATH.
# ============================================================================
if(WIN32)
    set(WINDEPLOYQT "${Qt6_DIR}/../../../bin/windeployqt.exe")
    if(EXISTS "${WINDEPLOYQT}")
        add_custom_command(TARGET AutoLens POST_BUILD
            COMMAND "${WINDEPLOYQT}"
                --qmldir "${CMAKE_CURRENT_SOURCE_DIR}"
                --no-translations
                --no-system-d3d-compiler
                "$<TARGET_FILE:AutoLens>"
            COMMENT "Running windeployqt to bundle Qt DLLs…"
            VERBATIM
        )
    endif()
endif()
