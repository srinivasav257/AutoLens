/**
 * @file main.cpp
 * @brief AutoLens application entry point.
 *
 * Responsibilities:
 *  1. Create QGuiApplication (owns the Qt event loop).
 *  2. Register C++ types with the QML type system.
 *  3. Show a lightweight bootstrap splash immediately.
 *  4. Expose AppController as a QML context property.
 *  5. Load Main.qml to start the UI.
 *
 * Architecture overview
 * ─────────────────────
 *
 *  ┌─ QML Engine ─────────────────────────────────────┐
 *  │  Main.qml                                        │
 *  │   ├─ TracePage.qml  ← TableView(traceModel)      │
 *  │   ├─ GeneratorPage.qml (Phase 2)                 │
 *  │   └─ DiagnosticsPage.qml (Phase 4)               │
 *  └──────────────────┬───────────────────────────────┘
 *         Q_PROPERTY  │  Q_INVOKABLE
 *  ┌──────────────────▼───────────────────────────────┐
 *  │  AppController (C++ singleton, UI thread)        │
 *  │   ├─ ICANDriver* (VectorCANDriver or Demo)       │
 *  │   ├─ DBCDatabase  (loaded from .dbc file)        │
 *  │   └─ TraceModel   (QAbstractTableModel)          │
 *  └──────────────────────────────────────────────────┘
 *         ↑ queued signal (thread-safe)
 *  ┌──────┴────────────────────────────────────────── ┐
 *  │  CAN Receive Thread (inside VectorCANDriver)     │
 *  │  Polls hardware, emits messageReceived(CANMsg)   │
 *  └──────────────────────────────────────────────────┘
 */

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QScreen>
#include <QQuickWindow>
#include <QQuickStyle>

// Centralized logging system — replaces the old temp file logger.
// Installed before QGuiApplication so even early Qt messages are captured.
#include "app/Logger.h"

#ifdef Q_OS_WIN
#include <windows.h>

#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif

#ifndef DWMWCP_DEFAULT
enum DWM_WINDOW_CORNER_PREFERENCE {
    DWMWCP_DEFAULT = 0,
    DWMWCP_DONOTROUND = 1,
    DWMWCP_ROUND = 2,
    DWMWCP_ROUNDSMALL = 3
};
#endif
#endif

#include "app/AppController.h"
#include "trace/TraceModel.h"
#include "trace/TraceFilterProxy.h"
#include "hardware/CANInterface.h"  // for CANManager::CANMessage

// ---------------------------------------------------------------------------
//  Meta-type registration for cross-thread signal/slot
//
//  CANMessage is emitted from the CAN receive thread and received on the UI
//  thread.  Qt's queued connection copies the argument into the event queue,
//  which requires the type to be registered with the meta-object system.
// ---------------------------------------------------------------------------
Q_DECLARE_METATYPE(CANManager::CANMessage)

int main(int argc, char* argv[])
{
    // -----------------------------------------------------------------------
    //  Install centralized logger FIRST — before QGuiApplication.
    //  This ensures even early Qt initialization messages are captured.
    //  On crash, the logger writes a crash marker with the last 50 messages.
    //  Log files: %LOCALAPPDATA%/AutoLens/logs/autolens_YYYYMMDD_HHmmss.log
    // -----------------------------------------------------------------------
    Logger::install(QStringLiteral("0.1.0"));

    // Check if previous session crashed and log it
    if (Logger::previousSessionCrashed()) {
        const QString crashInfo = Logger::previousCrashInfo();
        qWarning() << "[AutoLens] Previous session crashed! Crash info:\n" << crashInfo;
    }

    // QGuiApplication: event loop for Qt Quick (QML) apps.
    // For Qt Widgets apps you would use QApplication instead.
    QGuiApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("AutoLens"));
    app.setApplicationVersion(QStringLiteral("0.1.0"));
    app.setOrganizationName(QStringLiteral("AutoLens"));

    // ---------------------------------------------------------------------------
    //  Qt Quick Controls 2 style
    //  Material gives modern control metrics while our QML provides the
    //  application-specific dark palette.
    // ---------------------------------------------------------------------------
    QQuickStyle::setStyle(QStringLiteral("Material"));
    QQuickStyle::setFallbackStyle(QStringLiteral("Fusion"));

    // Register CANMessage so Qt can copy it through the event queue when
    // a queued connection fires across thread boundaries.
    qRegisterMetaType<CANManager::CANMessage>("CANManager::CANMessage");

    // ---------------------------------------------------------------------------
    //  Register TraceModel as an "uncreatable" QML type.
    //  QML cannot instantiate it with `TraceModel {}`, but once AppController
    //  hands out a pointer via a Q_PROPERTY the QML engine knows the type and
    //  TableView can bind to it correctly.
    // ---------------------------------------------------------------------------
    qmlRegisterUncreatableType<TraceModel>(
        "AutoLens", 1, 0, "TraceModel",
        QStringLiteral("TraceModel is owned by AppController — use AppController.traceModel")
    );

    qmlRegisterUncreatableType<TraceFilterProxy>(
        "AutoLens", 1, 0, "TraceFilterProxy",
        QStringLiteral("TraceFilterProxy is owned by AppController — use AppController.traceProxy")
    );

    // ---------------------------------------------------------------------------
    //  Create the application controller.
    //  It will auto-detect whether Vector hardware is available and select
    //  VectorCANDriver or DemoCANDriver accordingly.
    // ---------------------------------------------------------------------------
    AppController controller;

    // ---------------------------------------------------------------------------
    //  Bootstrap splash (separate engine, loaded BEFORE Main.qml)
    //
    //  WHY: Main.qml + page hierarchy are heavy and are created synchronously.
    //  If splash lives inside Main.qml, it cannot appear until that work ends.
    //  Loading this tiny splash first guarantees immediate visual feedback.
    // ---------------------------------------------------------------------------
    QQmlApplicationEngine splashEngine;
    splashEngine.rootContext()->setContextProperty(QStringLiteral("AppController"), &controller);
    QObject::connect(
        &splashEngine, &QQmlApplicationEngine::objectCreationFailed,
        &app,          []() { QCoreApplication::exit(-1); },
        Qt::QueuedConnection
    );
    splashEngine.loadFromModule("AutoLens", "BootstrapSplash");
    if (splashEngine.rootObjects().isEmpty())
        return -1;

    auto* bootstrapSplash = qobject_cast<QQuickWindow*>(splashEngine.rootObjects().first());
    if (bootstrapSplash) {
        if (auto* primary = QGuiApplication::primaryScreen()) {
            const QRect available = primary->availableGeometry();
            const int splashX = available.x() + (available.width()  - bootstrapSplash->width())  / 2;
            const int splashY = available.y() + (available.height() - bootstrapSplash->height()) / 2;
            bootstrapSplash->setPosition(splashX, splashY);
        }
        bootstrapSplash->show();
    }

    // Let the OS deliver WM_SHOWWINDOW/WM_PAINT so splash is visible now.
    QCoreApplication::processEvents(QEventLoop::AllEvents, 120);

    // Start backend initialization after the splash is painted.
    controller.startInitSequence();

    // ---------------------------------------------------------------------------
    //  Main UI engine
    // ---------------------------------------------------------------------------
    QQmlApplicationEngine engine;

    // Expose AppController to all QML files.
    engine.rootContext()->setContextProperty(QStringLiteral("AppController"), &controller);

    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed,
        &app,    []() { QCoreApplication::exit(-1); },
        Qt::QueuedConnection
    );

    engine.loadFromModule("AutoLens", "Main");
    if (engine.rootObjects().isEmpty())
        return -1;

    // Apply smooth rounded corners to the frameless main window.
    if (auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first())) {
        // Keep splash visible until the main window is actually shown.
        if (bootstrapSplash) {
            QObject::connect(window, &QQuickWindow::visibleChanged, bootstrapSplash,
                             [window, bootstrapSplash]() {
                if (window->isVisible() && bootstrapSplash->isVisible())
                    bootstrapSplash->close();
            });
            if (window->isVisible())
                bootstrapSplash->close();
        }

#ifdef Q_OS_WIN
        using DwmSetWindowAttributeFn = HRESULT(WINAPI*)(HWND, DWORD, LPCVOID, DWORD);

        auto applyNativeRoundedCorners = [window]() {
            const HWND hwnd = reinterpret_cast<HWND>(window->winId());
            if (!hwnd)
                return;

            const HMODULE dwmapi = LoadLibraryW(L"dwmapi.dll");
            if (!dwmapi)
                return;

            const auto setWindowAttribute = reinterpret_cast<DwmSetWindowAttributeFn>(
                GetProcAddress(dwmapi, "DwmSetWindowAttribute")
            );

            if (setWindowAttribute) {
                const DWM_WINDOW_CORNER_PREFERENCE cornerPreference = DWMWCP_ROUNDSMALL;
                setWindowAttribute(
                    hwnd,
                    DWMWA_WINDOW_CORNER_PREFERENCE,
                    &cornerPreference,
                    sizeof(cornerPreference)
                );
            }

            FreeLibrary(dwmapi);
        };

        QObject::connect(window, &QQuickWindow::visibleChanged, window, applyNativeRoundedCorners);
        applyNativeRoundedCorners();
#endif
    }

    const int exitCode = app.exec();

    // -----------------------------------------------------------------------
    //  Clean shutdown of the centralized logger.
    //  Writes session footer (uptime, message counts), removes crash marker.
    //  If this line is never reached (crash), the crash marker persists and
    //  the next session will detect + report it.
    // -----------------------------------------------------------------------
    Logger::shutdown();

    return exitCode;
}
