/**
 * SimulationPage.qml — Simulation / Replay (Phase 3)
 *
 * Phase 3 will add:
 *  • Log file selector (.asc / .blf)
 *  • Replay controls: play, pause, stop, speed factor
 *  • IG block schedule view (gantt-style timing display)
 */
import QtQuick
import QtQuick.Controls

Page {
    background: Rectangle { color: "#1a1a2e" }

    Column {
        anchors.centerIn: parent
        spacing: 16

        Label {
            anchors.horizontalCenter: parent.horizontalCenter
            text: "Simulation"
            font.pixelSize: 28
            font.bold: true
            color: "#e94560"
        }
        Label {
            anchors.horizontalCenter: parent.horizontalCenter
            text: "Coming in Phase 3\n.asc / .blf log replay\nIG block scheduling"
            color: "#8899aa"
            font.pixelSize: 14
            horizontalAlignment: Text.AlignHCenter
        }
    }
}
