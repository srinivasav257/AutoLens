import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    readonly property var appWindow: ApplicationWindow.window
    readonly property color panelBg: appWindow ? appWindow.panelBg : "#10151c"
    readonly property color border: appWindow ? appWindow.border : "#263242"
    readonly property color textMain: appWindow ? appWindow.textMain : "#e8eef8"
    readonly property color textMuted: appWindow ? appWindow.textMuted : "#91a4c3"

    height: 28
    color: panelBg
    border.color: border
    border.width: 0

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 10
        anchors.rightMargin: 10
        spacing: 12

        Rectangle {
            width: 8
            height: 8
            radius: 4
            color: AppController.connected ? "#59d892" : "#ff6d86"
        }

        Label {
            text: AppController.connected ? "Connected" : "Disconnected"
            color: textMain
            font.pixelSize: 11
        }

        Rectangle {
            width: 1
            height: 14
            color: border
        }

        Label {
            text: AppController.dbcLoaded ? AppController.dbcInfo : "No DBC loaded"
            color: textMuted
            font.pixelSize: 11
            elide: Text.ElideRight
            Layout.fillWidth: true
        }

        Label {
            text: AppController.measuring ? (AppController.frameRate + " fps") : "-"
            color: "#87f5b7"
            font.pixelSize: 11
            font.family: "Consolas"
        }
    }
}
