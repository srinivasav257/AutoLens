import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Page {
    id: simulationPage

    readonly property var appWindow: ApplicationWindow.window
    readonly property bool isDayTheme: appWindow ? appWindow.isDayTheme : false
    readonly property color pageBg: appWindow ? appWindow.pageBg : "#0d1118"
    readonly property color panelBg: appWindow ? appWindow.panelBg : "#10151c"
    readonly property color border: appWindow ? appWindow.border : "#263242"
    readonly property color accent: appWindow ? appWindow.accent : "#35b8ff"
    readonly property color textMain: appWindow ? appWindow.textMain : "#e8eef8"
    readonly property color textMuted: appWindow ? appWindow.textMuted : "#91a4c3"

    background: Rectangle {
        color: simulationPage.pageBg
        radius: 10
        border.color: simulationPage.border
        border.width: 0
    }

    Rectangle {
        width: Math.min(parent.width * 0.78, 760)
        height: 320
        radius: 14
        anchors.centerIn: parent
        border.color: simulationPage.border
        border.width: 0

        gradient: Gradient {
            GradientStop { position: 0.0; color: simulationPage.isDayTheme ? "#f8fbff" : "#161f2b" }
            GradientStop { position: 1.0; color: simulationPage.panelBg }
        }

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 26
            spacing: 14

            Label {
                text: "Simulation"
                color: simulationPage.textMain
                font.pixelSize: 34
                font.bold: true
            }

            Rectangle {
                width: 64
                height: 3
                radius: 2
                color: simulationPage.accent
            }

            Label {
                text: "Replay and traffic simulation are planned for Phase 3."
                color: simulationPage.textMuted
                font.pixelSize: 14
            }

            Label {
                text: "Upcoming: ASC/BLF file import, transport controls, speed factors, and timing schedule view."
                color: simulationPage.textMuted
                wrapMode: Text.WordWrap
                font.pixelSize: 13
                Layout.fillWidth: true
            }

            Item { Layout.fillHeight: true }

            Label {
                text: "STATUS: REQUIREMENTS DEFINED"
                color: simulationPage.accent
                font.pixelSize: 11
                font.letterSpacing: 1.0
            }
        }
    }
}
