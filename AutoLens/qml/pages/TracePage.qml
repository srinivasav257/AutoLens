import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Page {
    id: tracePage

    readonly property var appWindow: ApplicationWindow.window
    readonly property color pageBg: appWindow ? appWindow.pageBg : "#0d1118"
    readonly property color panelBg: appWindow ? appWindow.panelBg : "#10151c"
    readonly property color controlBg: appWindow ? appWindow.controlBg : "#1a212c"
    readonly property color controlHover: appWindow ? appWindow.controlHover : "#222c39"
    readonly property color border: appWindow ? appWindow.border : "#263242"
    readonly property color accent: appWindow ? appWindow.accent : "#35b8ff"
    readonly property color textMain: appWindow ? appWindow.textMain : "#e8eef8"
    readonly property color textMuted: appWindow ? appWindow.textMuted : "#91a4c3"

    readonly property var colWidths: [96, 48, 88, 48, 228, 136, 0]

    background: Rectangle {
        color: tracePage.pageBg
        radius: 10
        border.color: tracePage.border
        border.width: 0
    }

    header: Rectangle {
        height: 42
        radius: 8
        color: tracePage.panelBg
        border.color: tracePage.border
        border.width: 0

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 10
            anchors.rightMargin: 10
            spacing: 12

            CheckBox {
                id: autoScrollChk
                checked: true
                text: "Auto-scroll"
                implicitHeight: 24
                Layout.alignment: Qt.AlignVCenter
                spacing: 8
                leftPadding: 0
                rightPadding: 0
                topPadding: 0
                bottomPadding: 0

                indicator: Rectangle {
                    x: autoScrollChk.leftPadding
                    y: autoScrollChk.topPadding
                       + (autoScrollChk.availableHeight - height) / 2
                    implicitWidth: 16
                    implicitHeight: 16
                    radius: 3
                    color: autoScrollChk.checked ? tracePage.accent : "transparent"
                    border.color: autoScrollChk.checked ? tracePage.accent : "#344658"
                    border.width: 1

                    Rectangle {
                        anchors.centerIn: parent
                        width: 7
                        height: 7
                        radius: 2
                        color: "#0b1118"
                        visible: autoScrollChk.checked
                    }
                }

                contentItem: Label {
                    leftPadding: autoScrollChk.indicator.width + autoScrollChk.spacing
                    text: autoScrollChk.text
                    color: tracePage.textMain
                    font.pixelSize: 12
                    verticalAlignment: Text.AlignVCenter
                }
            }

            Label {
                text: "Filter ID"
                color: tracePage.textMuted
                font.pixelSize: 12
            }

            TextField {
                id: filterField
                implicitWidth: 110
                placeholderText: "0x0C4"
                color: tracePage.textMain
                font.family: "Consolas"
                font.pixelSize: 12

                background: Rectangle {
                    radius: 6
                    color: tracePage.controlBg
                    border.color: filterField.activeFocus ? tracePage.accent : tracePage.border
                    border.width: filterField.activeFocus ? 1 : 0
                }
            }

            Item { Layout.fillWidth: true }

            Label {
                text: AppController.frameCount + " frames"
                color: "#87f5b7"
                font.pixelSize: 11
                font.family: "Consolas"
            }

            Button {
                id: clearBtn
                text: "Clear"
                implicitWidth: 74
                implicitHeight: 28
                onClicked: AppController.clearTrace()

                background: Rectangle {
                    radius: 7
                    color: clearBtn.down
                           ? "#4f2233"
                           : (clearBtn.hovered ? "#462232" : "#3a1e2c")
                    border.color: "#ff6d86"
                }

                contentItem: Label {
                    text: clearBtn.text
                    color: "#ffd5dd"
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    font.pixelSize: 11
                }
            }
        }
    }

    Rectangle {
        anchors.fill: parent
        anchors.topMargin: 8
        radius: 8
        color: tracePage.panelBg
        border.color: tracePage.border
        border.width: 0

        HorizontalHeaderView {
            id: headerView
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            height: 28
            clip: true
            syncView: tableView
            columnWidthProvider: tableView.columnWidthProvider

            delegate: Rectangle {
                implicitHeight: 28
                color: "#171d27"
                border.color: tracePage.border
                border.width: 0

                Label {
                    anchors.fill: parent
                    anchors.leftMargin: 6
                    anchors.rightMargin: 6
                    text: model.display
                    color: tracePage.textMuted
                    font.pixelSize: 11
                    font.bold: true
                    elide: Text.ElideRight
                    verticalAlignment: Text.AlignVCenter
                }
            }
        }

        TableView {
            id: tableView
            anchors.top: headerView.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            clip: true

            model: AppController.traceModel

            columnWidthProvider: function(col) {
                var widths = tracePage.colWidths
                if (col >= widths.length)
                    return 80
                if (widths[col] === 0) {
                    var used = 0
                    for (var i = 0; i < widths.length - 1; ++i)
                        used += widths[i]
                    return Math.max(tableView.width - used, 110)
                }
                return widths[col]
            }

            rowHeightProvider: function() { return 24 }

            delegate: Rectangle {
                color: model.background
                       ? model.background
                       : (row % 2 === 0 ? "#10161f" : "#0d121a")
                border.color: "transparent"
                border.width: 0

                Label {
                    anchors.fill: parent
                    anchors.leftMargin: 6
                    anchors.rightMargin: 4
                    text: model.display ?? ""
                    color: model.foreground ? model.foreground : tracePage.textMain
                    font.family: "Consolas"
                    font.pixelSize: 11
                    elide: Text.ElideRight
                    verticalAlignment: Text.AlignVCenter
                }

                Rectangle {
                    anchors.fill: parent
                    color: "#ffffff"
                    opacity: cellMouseArea.containsMouse ? 0.05 : 0.0
                }

                MouseArea {
                    id: cellMouseArea
                    anchors.fill: parent
                    hoverEnabled: true
                }
            }

            ScrollBar.vertical: ScrollBar {
                policy: ScrollBar.AsNeeded
                background: Rectangle { color: "#0f141c" }
                contentItem: Rectangle {
                    implicitWidth: 8
                    radius: 4
                    color: "#263242"
                }
            }
        }
    }

    Connections {
        target: AppController.traceModel
        function onRowsInserted(parent, first, last) {
            if (autoScrollChk.checked)
                tableView.positionViewAtRow(tableView.rows - 1, TableView.AlignBottom)
        }
    }
}
