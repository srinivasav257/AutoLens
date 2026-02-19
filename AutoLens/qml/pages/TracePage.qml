/**
 * TracePage.qml — Live CAN trace window
 *
 * Layout
 * ──────
 *   ┌─ Filter bar ──────────────────────────────────────────────────────┐
 *   │ [✓ Auto-scroll]  [Filter ID: ______]  [Clear]  [N frames]        │
 *   └───────────────────────────────────────────────────────────────────┘
 *   ┌─ Header row ──────────────────────────────────────────────────────┐
 *   │ Time (ms) │ Ch │  ID   │ DLC │ Data                │ Message │ Signals │
 *   ├───────────┼────┼───────┼─────┼─────────────────────┼─────────┼─────────┤
 *   │  1234.567 │ CH1│ 0x0C4 │  8  │ AA BB CC DD EE FF   │ EngData │ RPM=145 │
 *   │  …
 *
 * Qt Quick TableView learning notes
 * ──────────────────────────────────
 *  • TableView: the Qt Quick item for displaying tabular data.
 *    It virtualises rows — only visible rows are instantiated, so it stays
 *    fast even with 50 000 rows.
 *  • HorizontalHeaderView: a companion item that draws column headers,
 *    synchronised with the TableView via syncView.
 *  • columnWidthProvider: a JS function called by the engine to size each
 *    column. We return fixed pixel widths.
 *  • positionViewAtRow: scrolls the view to a specific row.
 *    We call it in Connections.onRowsInserted to implement auto-scroll.
 *  • model.display: in a TableView delegate, `display` is the QML name for
 *    Qt::DisplayRole (registered in roleNames()).
 */

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Page {
    id: tracePage
    background: Rectangle { color: "#1a1a2e" }

    // Column widths (pixels)
    readonly property var colWidths: [88, 38, 80, 36, 200, 130, 0]
    //                               Time  Ch  ID  DLC  Data  Msg  Signals(fill)

    // ======================================================================
    //  Control Bar
    // ======================================================================
    header: Rectangle {
        height: 38
        color: "#0f1428"
        border.color: "#223"

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 8
            anchors.rightMargin: 8
            spacing: 12

            CheckBox {
                id: autoScrollChk
                text: "Auto-scroll"
                checked: true
                contentItem: Label {
                    leftPadding: autoScrollChk.indicator.width + 4
                    text: autoScrollChk.text
                    color: "#eaeaea"
                    font.pixelSize: 12
                    verticalAlignment: Text.AlignVCenter
                }
            }

            Label { text: "Filter ID:" ; color: "#8899aa"; font.pixelSize: 12 }

            TextField {
                id: filterField
                implicitWidth: 90
                placeholderText: "e.g. 0x0C4"
                color: "#eaeaea"
                font.family: "Consolas"
                font.pixelSize: 12
                background: Rectangle {
                    color: "#16213e"
                    border.color: "#334"
                    radius: 3
                }
            }

            Item { Layout.fillWidth: true }

            Label {
                text: AppController.frameCount + " frames"
                color: "#5fd48a"
                font.pixelSize: 11
                font.family: "Consolas"
            }

            Button {
                text: "Clear"
                implicitWidth: 64
                implicitHeight: 26
                background: Rectangle {
                    color: parent.pressed ? "#3a1a1a" : "#2a1a1a"
                    radius: 3
                    border.color: "#e94560"
                    border.width: 1
                }
                contentItem: Label {
                    text: "Clear"
                    color: "#e94560"
                    horizontalAlignment: Text.AlignHCenter
                    font.pixelSize: 11
                }
                onClicked: AppController.clearTrace()
            }
        }
    }

    // ======================================================================
    //  Table
    // ======================================================================
    Item {
        anchors.fill: parent

        // --- Column headers ---
        HorizontalHeaderView {
            id: headerView
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            syncView: tableView
            height: 26

            // Match column widths to the table
            columnWidthProvider: tableView.columnWidthProvider

            delegate: Rectangle {
                // implicitHeight is required by HorizontalHeaderView — it uses
                // it to measure the header row height. Without it the view
                // prints a warning and the header may not render correctly.
                implicitHeight: 26
                color: "#0f3460"
                border.color: "#223366"

                Label {
                    anchors.fill: parent
                    anchors.leftMargin: 4
                    text: model.display          // headerData() Qt::DisplayRole
                    color: "#bbccdd"
                    font.pixelSize: 11
                    font.bold: true
                    elide: Text.ElideRight
                    verticalAlignment: Text.AlignVCenter
                }
            }
        }

        // --- Data rows ---
        TableView {
            id: tableView
            anchors.top: headerView.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom

            model: AppController.traceModel

            // columnWidthProvider: function(col) → pixel width
            // We give 0 to the last column → it takes all remaining space.
            columnWidthProvider: function(col) {
                var widths = tracePage.colWidths
                if (col >= widths.length) return 80
                if (widths[col] === 0) {
                    // Fill remaining width
                    var used = 0
                    for (var i = 0; i < widths.length - 1; ++i)
                        used += widths[i]
                    return Math.max(tableView.width - used, 100)
                }
                return widths[col]
            }

            rowHeightProvider: function() { return 22 }

            clip: true

            // ── Row delegate ───────────────────────────────────────────
            delegate: Rectangle {
                // model.background comes from Qt::BackgroundRole in TraceModel::data()
                color: model.background ? model.background : "transparent"

                // Subtle hover highlight
                Rectangle {
                    anchors.fill: parent
                    color: "#ffffff"
                    opacity: cellMouseArea.containsMouse ? 0.05 : 0
                }

                Label {
                    anchors.fill: parent
                    anchors.leftMargin: 4
                    anchors.rightMargin: 2

                    // model.display comes from Qt::DisplayRole
                    text: model.display ?? ""

                    // model.foreground comes from Qt::ForegroundRole
                    color: model.foreground ? model.foreground : "#eaeaea"

                    font.family: "Consolas"
                    font.pixelSize: 11
                    elide: Text.ElideRight
                    verticalAlignment: Text.AlignVCenter
                }

                MouseArea {
                    id: cellMouseArea
                    anchors.fill: parent
                    hoverEnabled: true
                }
            }

            ScrollBar.vertical: ScrollBar {
                policy: ScrollBar.AsNeeded
                background: Rectangle { color: "#0a0a1a" }
                contentItem: Rectangle {
                    implicitWidth: 8
                    color: "#334466"
                    radius: 4
                }
            }
        }
    }

    // ======================================================================
    //  Auto-scroll — jump to last row when new frames arrive
    //  We listen for rowsInserted on the model and scroll if autoScrollChk
    //  is checked.
    // ======================================================================
    Connections {
        target: AppController.traceModel

        function onRowsInserted(parent, first, last) {
            if (autoScrollChk.checked)
                tableView.positionViewAtRow(tableView.rows - 1, TableView.AlignBottom)
        }
    }
}
