/**
 * Main.qml — AutoLens application window
 *
 * Structure
 * ─────────
 *   ApplicationWindow (root)
 *   ├── header: ToolBar          (connect / DBC / stats)
 *   └── contentItem: RowLayout
 *       ├── SideBar (Column)     (icon buttons for Trace/Gen/Sim/Diag)
 *       └── StackLayout          (one Page per tab)
 *           ├── TracePage
 *           ├── GeneratorPage
 *           ├── SimulationPage
 *           └── DiagnosticsPage
 *
 * Qt Quick Controls 2 learning notes
 * ────────────────────────────────────
 *  • ApplicationWindow: the root element for a QML app (provides title bar,
 *    menu bar support, header, footer attachment points).
 *  • StackLayout: shows only the child at currentIndex; others are invisible
 *    and don't consume paint time.
 *  • Binding expressions: `text: AppController.statusText` — Qt automatically
 *    re-evaluates this whenever statusTextChanged() is emitted. No manual
 *    connect() calls needed.
 *  • Signal handlers: `onClicked: AppController.connectChannel()` calls the
 *    C++ slot directly from QML.
 */

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs   // FileDialog — native OS open/save dialogs (Qt 6.2+)
import AutoLens          // our module URI from qt_add_qml_module

ApplicationWindow {
    id: root
    title: "AutoLens  —  Mini CANoe"
    width: 1280
    height: 760
    minimumWidth: 800
    minimumHeight: 500
    visible: true

    // ── Colour palette ────────────────────────────────────────────────────
    readonly property color bgDark:    "#1a1a2e"
    readonly property color bgMid:     "#16213e"
    readonly property color bgPanel:   "#0f3460"
    readonly property color accent:    "#e94560"
    readonly property color textMain:  "#eaeaea"
    readonly property color textMuted: "#8899aa"

    color: bgDark

    // ======================================================================
    //  Header — Toolbar
    // ======================================================================
    header: ToolBar {
        background: Rectangle { color: root.bgPanel }
        height: 46

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 8
            anchors.rightMargin: 8
            spacing: 8

            // --- App logo / name ---
            Label {
                text: "AutoLens"
                font.pixelSize: 16
                font.bold: true
                color: root.accent
            }

            Rectangle { width: 1; height: 28; color: root.textMuted; opacity: 0.4 }

            // --- Channel selector ---
            ComboBox {
                id: channelCombo
                model: AppController.channelList
                implicitWidth: 220
                enabled: !AppController.connected

                // Reflect external index changes (e.g. after refreshChannels)
                currentIndex: AppController.selectedChannel
                onCurrentIndexChanged: AppController.selectedChannel = currentIndex

                background: Rectangle {
                    color: channelCombo.enabled ? root.bgMid : "#222"
                    border.color: root.textMuted
                    radius: 3
                }
                contentItem: Label {
                    leftPadding: 8
                    text: channelCombo.displayText
                    color: root.textMain
                    elide: Text.ElideRight
                    verticalAlignment: Text.AlignVCenter
                }
            }

            // --- Connect / Disconnect button ---
            Button {
                id: connectBtn
                text: AppController.connected ? "■  Disconnect" : "⚡  Connect"
                implicitWidth: 130

                background: Rectangle {
                    color: AppController.connected ? "#7a1a2a" : "#1a5a2a"
                    radius: 3
                    border.color: AppController.connected ? root.accent : "#3d9e5f"
                }
                contentItem: Label {
                    text: connectBtn.text
                    color: root.textMain
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    font.pixelSize: 12
                }

                onClicked: AppController.connectChannel()
            }

            // --- Load DBC button ---
            Button {
                id: dbcBtn
                text: AppController.dbcLoaded ? "✓ DBC" : "Load DBC"
                implicitWidth: 110
                ToolTip.text: AppController.dbcInfo
                ToolTip.visible: hovered && AppController.dbcLoaded
                ToolTip.delay: 500

                background: Rectangle {
                    color: AppController.dbcLoaded ? "#1a4a3a" : root.bgMid
                    radius: 3
                    border.color: AppController.dbcLoaded ? "#3dae7f" : root.textMuted
                }
                contentItem: Label {
                    text: dbcBtn.text
                    color: AppController.dbcLoaded ? "#3dde9f" : root.textMain
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    font.pixelSize: 12
                }

                onClicked: dbcFileDialog.open()
            }

            // --- Spacer ---
            Item { Layout.fillWidth: true }

            // --- Frame rate badge ---
            Label {
                visible: AppController.measuring
                text: AppController.frameRate + " fps"
                color: AppController.frameRate > 1000 ? root.accent : "#5fd48a"
                font.pixelSize: 12
                font.family: "Consolas"
            }

            // --- Frame count ---
            Label {
                visible: AppController.measuring
                text: AppController.frameCount + " frames"
                color: root.textMuted
                font.pixelSize: 11
                font.family: "Consolas"
            }

            Rectangle { width: 1; height: 28; color: root.textMuted; opacity: 0.4 }

            // --- Status text ---
            Label {
                text: AppController.statusText
                color: root.textMuted
                font.pixelSize: 11
                elide: Text.ElideRight
                Layout.maximumWidth: 300
            }
        }
    }

    // ======================================================================
    //  Body — Sidebar + Content
    // ======================================================================
    RowLayout {
        anchors.fill: parent
        spacing: 0

        // ── Left sidebar ───────────────────────────────────────────────
        Rectangle {
            width: 54
            Layout.fillHeight: true
            color: root.bgPanel

            // Thin accent line on the right edge
            Rectangle {
                width: 2; height: parent.height
                anchors.right: parent.right
                color: root.accent
                opacity: 0.3
            }

            Column {
                anchors.top: parent.top
                anchors.topMargin: 8
                width: parent.width
                spacing: 2

                Repeater {
                    model: [
                        { icon: "⊡", label: "Trace" },
                        { icon: "↑", label: "Gen" },
                        { icon: "▶", label: "Sim" },
                        { icon: "⚕", label: "Diag" }
                    ]

                    delegate: Rectangle {
                        width: parent.width
                        height: 52
                        color: stack.currentIndex === index ? root.bgDark : "transparent"

                        // Left accent bar for active tab
                        Rectangle {
                            width: 3; height: parent.height
                            color: root.accent
                            visible: stack.currentIndex === index
                        }

                        Column {
                            anchors.centerIn: parent
                            spacing: 2

                            Label {
                                anchors.horizontalCenter: parent.horizontalCenter
                                text: modelData.icon
                                font.pixelSize: 18
                                color: stack.currentIndex === index
                                       ? root.accent : root.textMuted
                            }
                            Label {
                                anchors.horizontalCenter: parent.horizontalCenter
                                text: modelData.label
                                font.pixelSize: 9
                                color: stack.currentIndex === index
                                       ? root.textMain : root.textMuted
                            }
                        }

                        MouseArea {
                            anchors.fill: parent
                            onClicked: stack.currentIndex = index
                            cursorShape: Qt.PointingHandCursor
                        }
                    }
                }
            }
        }

        // ── Page content ───────────────────────────────────────────────
        StackLayout {
            id: stack
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: 0

            TracePage {}
            GeneratorPage {}
            SimulationPage {}
            DiagnosticsPage {}
        }
    }

    // ======================================================================
    //  File Dialog — Load DBC
    // ======================================================================
    // We use a simple FileDialog from QtQuick.Dialogs (available in Qt 6)
    // This shows the native OS open-file dialog.
    FileDialog {
        id: dbcFileDialog
        title: "Open DBC File"
        nameFilters: ["DBC files (*.dbc)", "All files (*)"]
        onAccepted: AppController.loadDbc(selectedFile.toString())
    }

    // ======================================================================
    //  Error toast (bottom notification)
    // ======================================================================
    Connections {
        target: AppController
        function onErrorOccurred(message) {
            errorToast.showMessage(message)
        }
    }

    Rectangle {
        id: errorToast
        anchors.bottom: parent.bottom
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottomMargin: 20
        width: errorLabel.implicitWidth + 32
        height: 36
        radius: 4
        color: "#7a1a1a"
        border.color: root.accent
        visible: false
        z: 100

        Label {
            id: errorLabel
            anchors.centerIn: parent
            color: root.textMain
            font.pixelSize: 12
        }

        Timer {
            id: toastTimer
            interval: 4000
            onTriggered: errorToast.visible = false
        }

        function showMessage(msg) {
            errorLabel.text = msg
            visible = true
            toastTimer.restart()
        }
    }
}
