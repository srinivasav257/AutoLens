/**
 * @file main.cpp
 * @brief AutoLens application entry point.
 *
 * Responsibilities:
 *  1. Create QGuiApplication (owns the Qt event loop).
 *  2. Register C++ types with the QML type system.
 *  3. Expose AppController as a QML context property.
 *  4. Load Main.qml to start the UI.
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
#include <QQuickStyle>

#include "app/AppController.h"
#include "trace/TraceModel.h"
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
    // QGuiApplication: event loop for Qt Quick (QML) apps.
    // For Qt Widgets apps you would use QApplication instead.
    QGuiApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("AutoLens"));
    app.setApplicationVersion(QStringLiteral("0.1.0"));
    app.setOrganizationName(QStringLiteral("AutoLens"));

    // ---------------------------------------------------------------------------
    //  Qt Quick Controls 2 style
    //  Options: Basic | Fusion | Material | Universal | Imagine
    //  Fusion gives a clean, flat desktop look on all platforms.
    //  Switch to "Material" for a more modern automotive feel in a later phase.
    // ---------------------------------------------------------------------------
    QQuickStyle::setStyle(QStringLiteral("Fusion"));

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

    // ---------------------------------------------------------------------------
    //  Create the application controller.
    //  It will auto-detect whether Vector hardware is available and select
    //  VectorCANDriver or DemoCANDriver accordingly.
    // ---------------------------------------------------------------------------
    AppController controller;

    // ---------------------------------------------------------------------------
    //  QML engine setup
    // ---------------------------------------------------------------------------
    QQmlApplicationEngine engine;

    // Expose AppController to all QML files.
    // QML can now call: AppController.connectChannel()  or
    //                   text: AppController.statusText
    engine.rootContext()->setContextProperty(QStringLiteral("AppController"), &controller);

    // If QML fails to create the root object (e.g. syntax error), exit cleanly.
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed,
        &app,    []() { QCoreApplication::exit(-1); },
        Qt::QueuedConnection
    );

    // Load the root QML file.
    // "qrc:/AutoLens/Main.qml" is the Qt resource path assigned by
    // qt_add_qml_module with URI "AutoLens".
    engine.loadFromModule("AutoLens", "Main");

    return app.exec();
}
