import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import QtQuick.Dialogs
import QtQuick.Window
import AutoLens

ApplicationWindow {
    id: root
    title: "AutoLens - Mini CANoe"
    width: 1280
    height: 760
    minimumWidth: 980
    minimumHeight: 620
    visible: true
    flags: Qt.Window | Qt.FramelessWindowHint
    Material.theme: Material.Dark
    Material.accent: Material.Cyan
    Material.primary: Material.Grey

    readonly property color windowBg: "#07090d"
    readonly property color windowBgAlt: "#0a0d12"
    readonly property color panelBg: "#10151c"
    readonly property color panelRaised: "#141a23"
    readonly property color pageBg: "#0d1118"
    readonly property color controlBg: "#1a212c"
    readonly property color controlHover: "#222c39"
    readonly property color border: "#263242"
    readonly property color accent: "#35b8ff"
    readonly property color accentSoft: "#89dbff"
    readonly property color success: "#59d892"
    readonly property color danger: "#ff6d86"
    readonly property color textMain: "#e8eef8"
    readonly property color textMuted: "#91a4c3"

    color: windowBg

    background: Rectangle {
        gradient: Gradient {
            GradientStop { position: 0.0; color: root.windowBgAlt }
            GradientStop { position: 1.0; color: root.windowBg }
        }
    }

    function toggleMaximized() {
        if (root.visibility === Window.Maximized)
            root.showNormal()
        else
            root.showMaximized()
    }

    header: Column {
        width: root.width
        spacing: 0

        Rectangle {
            width: parent.width
            height: 40
            color: root.panelBg
            border.color: root.border
            border.width: 0

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 8
                spacing: 8

                Label {
                    text: "AUTOLENS"
                    color: root.accent
                    font.pixelSize: 14
                    font.bold: true
                    font.letterSpacing: 1.2
                }

                Label {
                    text: "CAN ANALYZER"
                    color: root.textMuted
                    font.pixelSize: 10
                    font.letterSpacing: 1.4
                }

                Item {
                    Layout.fillWidth: true
                    Layout.fillHeight: true

                    DragHandler {
                        target: null
                        onActiveChanged: if (active) root.startSystemMove()
                    }

                    TapHandler {
                        acceptedButtons: Qt.LeftButton
                        gesturePolicy: TapHandler.ReleaseWithinBounds
                        onDoubleTapped: root.toggleMaximized()
                    }
                }

                Rectangle {
                    id: minButton
                    implicitWidth: 34
                    implicitHeight: 24
                    radius: height / 2
                    color: minMouse.pressed
                           ? "#273448"
                           : (minMouse.containsMouse ? "#1f2b3d" : "transparent")
                    border.color: minMouse.containsMouse ? "#2f4258" : "transparent"
                    border.width: minMouse.containsMouse ? 1 : 0

                    Rectangle {
                        anchors.centerIn: parent
                        width: 10
                        height: 2
                        radius: 1
                        color: "#b7c8da"
                    }

                    MouseArea {
                        id: minMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.showMinimized()
                    }
                }

                Rectangle {
                    id: maxButton
                    implicitWidth: 34
                    implicitHeight: 24
                    radius: height / 2
                    color: maxMouse.pressed
                           ? "#273448"
                           : (maxMouse.containsMouse ? "#1f2b3d" : "transparent")
                    border.color: maxMouse.containsMouse ? "#2f4258" : "transparent"
                    border.width: maxMouse.containsMouse ? 1 : 0

                    Rectangle {
                        visible: root.visibility !== Window.Maximized
                        anchors.centerIn: parent
                        width: 10
                        height: 8
                        radius: 1
                        color: "transparent"
                        border.color: "#b7c8da"
                        border.width: 1
                    }

                    Rectangle {
                        visible: root.visibility === Window.Maximized
                        anchors.horizontalCenter: parent.horizontalCenter
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.horizontalCenterOffset: 1
                        anchors.verticalCenterOffset: -1
                        width: 8
                        height: 6
                        color: "transparent"
                        border.color: "#b7c8da"
                        border.width: 1
                    }

                    Rectangle {
                        visible: root.visibility === Window.Maximized
                        anchors.horizontalCenter: parent.horizontalCenter
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.horizontalCenterOffset: -1
                        anchors.verticalCenterOffset: 1
                        width: 8
                        height: 6
                        color: "transparent"
                        border.color: "#b7c8da"
                        border.width: 1
                    }

                    MouseArea {
                        id: maxMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.toggleMaximized()
                    }
                }

                Rectangle {
                    id: closeButton
                    implicitWidth: 34
                    implicitHeight: 24
                    radius: height / 2
                    color: closeMouse.pressed
                           ? "#c13e60"
                           : (closeMouse.containsMouse ? "#a83553" : "transparent")
                    border.color: closeMouse.containsMouse ? "#cf5f7d" : "transparent"
                    border.width: closeMouse.containsMouse ? 1 : 0

                    Rectangle {
                        anchors.centerIn: parent
                        width: 11
                        height: 2
                        radius: 1
                        rotation: 45
                        color: "#f4f7ff"
                    }
                    Rectangle {
                        anchors.centerIn: parent
                        width: 11
                        height: 2
                        radius: 1
                        rotation: -45
                        color: "#f4f7ff"
                    }

                    MouseArea {
                        id: closeMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.close()
                    }
                }
            }
        }

        ToolBar {
            width: parent.width
            height: 52

            background: Rectangle {
                color: root.panelRaised
                border.color: root.border
                border.width: 0
            }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 12
                spacing: 10

                Label {
                    text: "Session"
                    color: root.textMuted
                    font.pixelSize: 11
                    font.letterSpacing: 0.8
                }

                ComboBox {
                    id: channelCombo
                    model: AppController.channelList
                    implicitWidth: 240
                    enabled: !AppController.connected
                    currentIndex: AppController.selectedChannel
                    onCurrentIndexChanged: AppController.selectedChannel = currentIndex

                    background: Rectangle {
                        radius: 8
                        color: channelCombo.enabled ? root.controlBg : "#181c24"
                        border.color: root.border
                        border.width: 0
                    }

                    contentItem: Label {
                        leftPadding: 10
                        rightPadding: 24
                        text: channelCombo.displayText
                        color: root.textMain
                        elide: Text.ElideRight
                        verticalAlignment: Text.AlignVCenter
                        font.pixelSize: 12
                    }

                    popup: Popup {
                        y: channelCombo.height + 4
                        width: channelCombo.width
                        implicitHeight: contentItem.implicitHeight + 8
                        padding: 4

                        background: Rectangle {
                            radius: 8
                            color: root.panelBg
                            border.color: root.border
                            border.width: 1
                        }

                        contentItem: ListView {
                            clip: true
                            implicitHeight: contentHeight
                            model: channelCombo.popup.visible ? channelCombo.delegateModel : null

                            ScrollIndicator.vertical: ScrollIndicator { }

                            delegate: ItemDelegate {
                                width: ListView.view.width
                                text: modelData
                                highlighted: channelCombo.highlightedIndex === index

                                contentItem: Label {
                                    text: parent.text
                                    color: parent.highlighted ? root.accentSoft : root.textMain
                                    verticalAlignment: Text.AlignVCenter
                                    elide: Text.ElideRight
                                }

                                background: Rectangle {
                                    radius: 6
                                    color: parent.highlighted ? "#1f334b" : "transparent"
                                }
                            }
                        }
                    }
                }

                Button {
                    id: connectBtn
                    text: AppController.connected ? "Disconnect" : "Connect"
                    implicitWidth: 122

                    background: Rectangle {
                        radius: 8
                        color: AppController.connected ? "#40242d" : "#1f342b"
                        border.color: AppController.connected ? root.danger : root.success
                        border.width: 1
                    }

                    contentItem: Label {
                        text: connectBtn.text
                        color: AppController.connected ? "#ffd7df" : "#d7ffe9"
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        font.pixelSize: 12
                        font.bold: true
                    }

                    onClicked: AppController.connectChannel()
                }

                Button {
                    id: dbcBtn
                    text: AppController.dbcLoaded ? "DBC Loaded" : "Load DBC"
                    implicitWidth: 116
                    ToolTip.text: AppController.dbcInfo
                    ToolTip.visible: hovered && AppController.dbcLoaded
                    ToolTip.delay: 400

                    background: Rectangle {
                        radius: 8
                        color: AppController.dbcLoaded ? "#1d2f31" : root.controlBg
                        border.color: AppController.dbcLoaded ? "#59d8c0" : "#2a3340"
                        border.width: 1
                    }

                    contentItem: Label {
                        text: dbcBtn.text
                        color: AppController.dbcLoaded ? "#94ffea" : root.textMain
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        font.pixelSize: 12
                    }

                    onClicked: dbcFileDialog.open()
                }

                Item { Layout.fillWidth: true }

                Label {
                    visible: AppController.measuring
                    text: AppController.frameRate + " fps"
                    color: AppController.frameRate > 1000 ? root.accent : root.success
                    font.pixelSize: 11
                    font.family: "Consolas"
                }

                Label {
                    visible: AppController.measuring
                    text: AppController.frameCount + " frames"
                    color: root.textMuted
                    font.pixelSize: 11
                    font.family: "Consolas"
                }

                Rectangle {
                    width: 1
                    height: 24
                    color: root.border
                    opacity: 0.8
                }

                Label {
                    text: AppController.statusText
                    color: root.textMuted
                    font.pixelSize: 11
                    elide: Text.ElideRight
                    Layout.maximumWidth: 340
                }
            }
        }
    }

    RowLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 12

        Rectangle {
            Layout.fillHeight: true
            width: 102
            radius: 12
            color: root.panelBg
            border.color: root.border
            border.width: 0

            Column {
                anchors.fill: parent
                anchors.margins: 10
                spacing: 8

                Repeater {
                    model: [
                        { shortName: "TR", label: "Trace" },
                        { shortName: "GN", label: "Generator" },
                        { shortName: "SM", label: "Simulation" },
                        { shortName: "DG", label: "Diagnostics" }
                    ]

                    delegate: Rectangle {
                        id: navItem
                        width: parent.width
                        height: 72
                        radius: 10
                        property bool active: stack.currentIndex === index
                        property bool hovered: navMouse.containsMouse
                        color: active ? "#1b2735" : (hovered ? "#131c27" : "transparent")
                        border.color: active ? root.accent : (hovered ? "#2a3c52" : "transparent")
                        border.width: active ? 1 : (hovered ? 1 : 0)
                        scale: hovered && !active ? 1.02 : 1.0

                        Behavior on color {
                            ColorAnimation { duration: 120 }
                        }
                        Behavior on scale {
                            NumberAnimation { duration: 120 }
                        }

                        Rectangle {
                            anchors.left: parent.left
                            anchors.verticalCenter: parent.verticalCenter
                            width: 2
                            height: parent.height * 0.55
                            radius: 1
                            color: root.accent
                            opacity: navItem.active ? 0.95 : (navItem.hovered ? 0.45 : 0.0)

                            Behavior on opacity {
                                NumberAnimation { duration: 120 }
                            }
                        }

                        Column {
                            anchors.centerIn: parent
                            spacing: 4

                            Label {
                                anchors.horizontalCenter: parent.horizontalCenter
                                text: modelData.shortName
                                color: navItem.active
                                       ? root.accentSoft
                                       : (navItem.hovered ? "#b9cce2" : root.textMuted)
                                font.pixelSize: 13
                                font.bold: true
                                font.letterSpacing: 1.1
                            }

                            Label {
                                anchors.horizontalCenter: parent.horizontalCenter
                                text: modelData.label
                                color: navItem.active
                                       ? root.textMain
                                       : (navItem.hovered ? "#9fb6cf" : root.textMuted)
                                font.pixelSize: 10
                            }
                        }

                        MouseArea {
                            id: navMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: stack.currentIndex = index
                        }
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: 12
            color: root.panelRaised
            border.color: root.border
            border.width: 0

            StackLayout {
                id: stack
                anchors.fill: parent
                anchors.margins: 10
                currentIndex: 0

                TracePage { }
                GeneratorPage { }
                SimulationPage { }
                DiagnosticsPage { }
            }
        }
    }

    FileDialog {
        id: dbcFileDialog
        title: "Open DBC File"
        nameFilters: ["DBC files (*.dbc)", "All files (*)"]
        onAccepted: AppController.loadDbc(selectedFile.toString())
    }

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
        anchors.bottomMargin: 22
        width: Math.max(errorLabel.implicitWidth + 28, 220)
        height: 36
        radius: 8
        color: "#5b2533"
        border.color: root.danger
        border.width: 1
        visible: false
        z: 100

        Label {
            id: errorLabel
            anchors.centerIn: parent
            color: "#ffe4ea"
            font.pixelSize: 12
            elide: Text.ElideRight
            width: parent.width - 20
            horizontalAlignment: Text.AlignHCenter
        }

        Timer {
            id: toastTimer
            interval: 3800
            onTriggered: errorToast.visible = false
        }

        function showMessage(msg) {
            errorLabel.text = msg
            visible = true
            toastTimer.restart()
        }
    }
}
