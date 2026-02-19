/**
 * GeneratorPage.qml — Interactive Generator (Phase 2)
 *
 * Phase 2 will add:
 *  • List of IG blocks (each with: frame ID, DLC, data bytes, mode, interval)
 *  • Hex byte editor for the 8 data bytes
 *  • Send-once button and cyclic start/stop per block
 */
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Page {
    background: Rectangle { color: "#1a1a2e" }

    Column {
        anchors.centerIn: parent
        spacing: 16

        Label {
            anchors.horizontalCenter: parent.horizontalCenter
            text: "Generator"
            font.pixelSize: 28
            font.bold: true
            color: "#e94560"
        }
        Label {
            anchors.horizontalCenter: parent.horizontalCenter
            text: "Coming in Phase 2\nSend individual CAN frames\nand run cyclic IG blocks"
            color: "#8899aa"
            font.pixelSize: 14
            horizontalAlignment: Text.AlignHCenter
        }
    }
}
