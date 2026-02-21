/**
 * @file SplashScreen.qml
 * @brief Startup splash screen content for AutoLens.
 *
 * This Rectangle is placed inside a standalone OS-level Window declared in
 * Main.qml (see the `Window { id: splashWindow }` block).  Using a separate
 * OS window (rather than an in-app overlay) means the main ApplicationWindow
 * can start hidden and only appear after initialization completes — the user
 * never sees a half-initialised UI.
 *
 * Lifecycle
 * ─────────
 *  1. splashWindow is created with Qt.WindowStaysOnTopHint — it sits above
 *     everything while it is alive.
 *  2. AppController.startInitSequence() runs in background threads.
 *  3. When AppController.initComplete becomes true:
 *       a) This Rectangle's state machine starts a 600 ms fade to opacity 0.
 *       b) Main.qml's Connections simultaneously starts a 500 ms fade-in of
 *          the main ApplicationWindow (cross-fade effect).
 *       c) After the fade, PropertyAction sets this Rectangle's visible = false.
 *       d) Main.qml watches onVisibleChanged on this component and closes
 *          splashWindow, removing the OS window entirely.
 *
 * Learning notes
 * ──────────────
 *  • Qt.WindowStaysOnTopHint — tells the OS compositor to render this window
 *    above ALL other windows (including the main app).  Correct for splash.
 *    Released automatically when the window is closed.
 *
 *  • Screen.desktopAvailableWidth — QML Screen singleton; returns usable
 *    screen area excluding taskbar etc.  Works for centering the splash.
 *
 *  • Window.opacity (0.0–1.0) — animating this fades the entire OS window
 *    using DWM compositing on Windows (hardware-accelerated, zero-copy).
 *
 *  • states / transitions — idiomatic QML pattern for animated visibility:
 *    change a state, define a Transition, let the engine interpolate.
 *    SequentialAnimation ensures PropertyAction fires AFTER the fade ends.
 */

import QtQuick
import QtQuick.Controls

Rectangle {
    id: splash

    // Fill the parent Window's content area (sized by the Window in Main.qml)
    anchors.fill: parent

    // ── Appearance ─────────────────────────────────────────────────────────
    color: "#07090d"   // AutoLens dark background

    // ── Dismiss gate — set by Main.qml ─────────────────────────────────────
    //
    // WHY a property instead of reading AppController.initComplete directly:
    //   Main.qml combines initComplete WITH a 2.5-second minimum timer into the
    //   single `readyToReveal` computed property.  By exposing readyToDismiss here
    //   and having Main.qml bind it to readyToReveal, SplashScreen stays
    //   self-contained: it doesn't need to know about the timer, and the timing
    //   policy lives in one place (Main.qml) not scattered across two files.
    //
    // Default false so the splash stays on-screen until the parent explicitly says go.
    property bool readyToDismiss: false

    // ── Fade-out state machine ──────────────────────────────────────────────
    // WHY states + transitions instead of `opacity: x ? 0 : 1`:
    // We need a SEQUENCE — fade opacity to 0, THEN set visible = false.
    // PropertyAction in a SequentialAnimation fires after the animation ends.
    // A plain binding has no concept of "do this after that".
    states: State {
        name: "done"
        when: readyToDismiss      // set by Main.qml when backend+timer both done
        PropertyChanges { target: splash; opacity: 0 }
    }

    transitions: Transition {
        // WHY explicit from: ""  — the default (unnamed) state has an empty-string name.
        // Without from: the engine defaults to "*" (any state). Being explicit ensures
        // this transition only fires when moving from the default state to "done", not
        // in reverse or from other future states.
        from: ""
        to:   "done"
        SequentialAnimation {
            NumberAnimation {
                // WHY explicit target: — in a Transition, omitting target infers it
                // from context. Explicit is safer if this block is ever refactored.
                target:      splash
                property:    "opacity"
                duration:    600
                easing.type: Easing.OutCubic
            }
            // Once fully transparent, mark invisible.
            // Main.qml's onVisibleChanged handler closes the OS window.
            PropertyAction { target: splash; property: "visible"; value: false }
        }
    }

    // ── Central content ─────────────────────────────────────────────────────
    Column {
        anchors.centerIn: parent
        spacing:          24

        // ── Logo ─────────────────────────────────────────────────────────
        Column {
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: 8

            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text:             "AUTOLENS"
                color:            "#35b8ff"
                font.pixelSize:   52
                font.bold:        true
                font.letterSpacing: 8
            }

            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text:             "CAN ANALYZER"
                color:            "#3a5070"
                font.pixelSize:   13
                font.letterSpacing: 5
            }
        }

        // ── Divider ───────────────────────────────────────────────────────
        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            width: 220; height: 1
            color: "#141e2c"
        }

        // ── Animated loading dots (wave pattern) ─────────────────────────
        // Five dots, each with a SequentialAnimation whose leading
        // PauseAnimation grows by 120 ms per dot, creating a left-to-right wave.
        Row {
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: 10

            Repeater {
                model: 5

                Rectangle {
                    property int dotIndex: index
                    width: 7; height: 7; radius: 3.5
                    color: "#35b8ff"
                    opacity: 0.2

                    SequentialAnimation on opacity {
                        loops: Animation.Infinite
                        PauseAnimation  { duration: dotIndex * 120 }
                        NumberAnimation { to: 1.0; duration: 250; easing.type: Easing.OutCubic }
                        PauseAnimation  { duration: 180 }
                        NumberAnimation { to: 0.2; duration: 380; easing.type: Easing.InCubic }
                        PauseAnimation  { duration: (4 - dotIndex) * 120 + 100 }
                    }
                }
            }
        }

        // ── Live status message ──────────────────────────────────────────
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text:             AppController.initStatus
            color:            "#4a6880"
            font.pixelSize:   12
            font.family:      "Consolas"
        }

        // ── Version ───────────────────────────────────────────────────────
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text:             "v" + Qt.application.version
            color:            "#1e2c3a"
            font.pixelSize:   10
            font.letterSpacing: 1.5
        }
    }

    // ── Bottom status strip ──────────────────────────────────────────────────
    Rectangle {
        anchors.bottom: parent.bottom
        anchors.left:   parent.left
        anchors.right:  parent.right
        height:  32
        color:   "#050709"

        Text {
            anchors.centerIn: parent
            text:   AppController.initComplete
                    ? AppController.driverName + " ready"
                    : "Initializing..."
            color:  "#1e3050"
            font.pixelSize:    10
            font.family:       "Consolas"
            font.letterSpacing: 1.2
        }
    }
}
