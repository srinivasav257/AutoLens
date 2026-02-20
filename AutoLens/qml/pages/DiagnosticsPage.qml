import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Page {
    id: diagnosticsPage

    readonly property var appWindow: ApplicationWindow.window
    readonly property bool isDayTheme: appWindow ? appWindow.isDayTheme : false
    readonly property color pageBg: appWindow ? appWindow.pageBg : "#0d1118"
    readonly property color panelBg: appWindow ? appWindow.panelBg : "#10151c"
    readonly property color border: appWindow ? appWindow.border : "#263242"
    readonly property color accent: appWindow ? appWindow.accent : "#35b8ff"
    readonly property color textMain: appWindow ? appWindow.textMain : "#e8eef8"
    readonly property color textMuted: appWindow ? appWindow.textMuted : "#91a4c3"

    background: Rectangle {
        color: diagnosticsPage.pageBg
        radius: 10
        border.color: diagnosticsPage.border
        border.width: 0
    }

    Rectangle {
        width: Math.min(parent.width * 0.78, 760)
        height: 340
        radius: 14
        anchors.centerIn: parent
        border.color: diagnosticsPage.border
        border.width: 0

        gradient: Gradient {
            GradientStop { position: 0.0; color: diagnosticsPage.isDayTheme ? "#f8fbff" : "#1a202a" }
            GradientStop { position: 1.0; color: diagnosticsPage.panelBg }
        }

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 26
            spacing: 14

            Label {
                text: "Diagnostics"
                color: diagnosticsPage.textMain
                font.pixelSize: 34
                font.bold: true
            }

            Rectangle {
                width: 64
                height: 3
                radius: 2
                color: diagnosticsPage.accent
            }

            Label {
                text: "UDS diagnostics support is planned for Phase 4."
                color: diagnosticsPage.textMuted
                font.pixelSize: 14
            }

            Label {
                text: "Scope: CAN-TP transport, DID read/write, DTC handling, session control, and security access."
                color: diagnosticsPage.textMuted
                wrapMode: Text.WordWrap
                font.pixelSize: 13
                Layout.fillWidth: true
            }

            Item { Layout.fillHeight: true }

            Label {
                text: "STATUS: ARCHITECTURE IN PROGRESS"
                color: diagnosticsPage.accent
                font.pixelSize: 11
                font.letterSpacing: 1.0
            }
        }
    }
}
