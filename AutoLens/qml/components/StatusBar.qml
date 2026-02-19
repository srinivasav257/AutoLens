/**
 * StatusBar.qml — Bottom status strip (optional, not used in Phase 1)
 *
 * Can be attached as `footer: StatusBar {}` in Main.qml when needed.
 * Displays connection state, DBC info, and frame statistics.
 */
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    height: 24
    color: "#0a0f1e"
    border.color: "#223"
    border.width: 1

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 8
        anchors.rightMargin: 8
        spacing: 16

        // Connection indicator dot
        Rectangle {
            width: 8; height: 8; radius: 4
            color: AppController.connected ? "#3dae7f" : "#e94560"
        }

        Label {
            text: AppController.connected ? "Connected" : "Disconnected"
            color: AppController.connected ? "#3dde9f" : "#e94560"
            font.pixelSize: 11
        }

        Label {
            text: "|"
            color: "#334"
        }

        Label {
            text: AppController.dbcLoaded ? AppController.dbcInfo : "No DBC loaded"
            color: AppController.dbcLoaded ? "#8899aa" : "#445"
            font.pixelSize: 11
            elide: Text.ElideRight
            Layout.fillWidth: true
        }

        Label {
            text: AppController.measuring
                  ? AppController.frameRate + " fps"
                  : "—"
            color: "#5fd48a"
            font.pixelSize: 11
            font.family: "Consolas"
        }
    }
}
