/**
 * DiagnosticsPage.qml — UDS Diagnostics (Phase 4)
 *
 * Phase 4 will add:
 *  • ISO 15765-2 CAN-TP transport (multi-frame assembly)
 *  • ISO 14229 UDS services:
 *    - Service 0x22: ReadDataByIdentifier (read DIDs)
 *    - Service 0x2E: WriteDataByIdentifier (write DIDs)
 *    - Service 0x19: ReadDTCInformation (read and clear DTCs)
 *    - Service 0x10: DiagnosticSessionControl
 *    - Service 0x27: SecurityAccess
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
            text: "Diagnostics"
            font.pixelSize: 28
            font.bold: true
            color: "#e94560"
        }
        Label {
            anchors.horizontalCenter: parent.horizontalCenter
            text: "Coming in Phase 4\nUDS over CAN-TP (ISO 15765-2)\nRead/Write DIDs · Read/Clear DTCs"
            color: "#8899aa"
            font.pixelSize: 14
            horizontalAlignment: Text.AlignHCenter
        }
    }
}
