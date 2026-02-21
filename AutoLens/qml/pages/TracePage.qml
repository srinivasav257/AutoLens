/**
 * TracePage.qml — Professional CAN/CAN-FD Trace Window
 *
 * ═══════════════════════════════════════════════════════════════════════════
 *  VISUAL DESIGN  (matches Vector CANalyzer / CANoe trace window)
 * ═══════════════════════════════════════════════════════════════════════════
 *
 *  ┌───────────────────────────────────────────────────────────────────────┐
 *  │ [▶ Start] [■ Stop] [⏸ Pause]  │  [Clear] [Save]  │  Auto-scroll [✓]  │
 *  │ Filter: [____________________]  DBC: [Load DBC...]                    │
 *  ├───────────────────────────────────────────────────────────────────────┤
 *  │  CH1 ●  CH2 ○  │  Frames: 12 345  │  Rate: 450 fps  │  CAN FD: 12%   │
 *  ├──────────────┬──────────────────┬───────┬───┬──────────┬────┬────┬────┤
 *  │ Time (ms)    │ Name             │ ID    │Chn│Event Type│Dir │DLC │Data│
 *  ├──────────────┼──────────────────┼───────┼───┼──────────┼────┼────┼────┤
 *  │▶  1234.5678  │ EngineData       │ 0C4h  │ 1 │ CAN FD   │ Rx │  8 │AA..│
 *  │   ├─ EngineSpeed               │1450 rpm│   │          │    │    │0x5A│
 *  │   └─ ThrottlePos               │42.5 %  │   │          │    │    │0x2B│
 *  │   2234.1234  │ BrakeStatus      │ 0B2h  │ 2 │ CAN      │ Rx │  4 │01..│
 *  │   3100.0012  │ ---              │ 7DFh  │ 1 │ CAN      │ Rx │  8 │02..│
 *  └──────────────┴──────────────────┴───────┴───┴──────────┴────┴────┴────┘
 *
 * ═══════════════════════════════════════════════════════════════════════════
 *  KEY ARCHITECTURAL DECISIONS
 * ═══════════════════════════════════════════════════════════════════════════
 *
 *  • Uses Qt 6.3+ TreeView (extends TableView) for native expand/collapse.
 *    QML code calls treeView.expand(row) / treeView.collapse(row) — no manual JS tracking.
 *
 *  • Model is TraceModel (QAbstractItemModel) with 2-level tree:
 *      Level 0 = frame rows (tens of thousands)
 *      Level 1 = decoded signal rows (0-N per frame, shown when expanded)
 *
 *  • Each cell delegate is a lightweight Item — no QObject per cell.
 *    All display strings were pre-formatted in C++ (AppController::buildEntry).
 *
 *  • The `model.isFrame` custom role lets the delegate branch between
 *    frame and signal rendering without any JS state.
 *
 *  • FileDialog for Save and DBC Load — avoids hard-coded paths.
 */

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

Page {
    id: tracePage
    readonly property var appWindow: ApplicationWindow.window
    readonly property bool isDayTheme: appWindow ? appWindow.isDayTheme : false

    // ─────────────────────────────────────────────────────────────────────────
    //  COLOUR PALETTE — dark professional theme matching CANalyzer
    //
    //  All colours are declared as readonly properties so every delegate can
    //  reference them by name rather than embedding colour literals everywhere.
    //  Changing one property here updates the entire page consistently.
    // ─────────────────────────────────────────────────────────────────────────

    // Backgrounds
    readonly property color clrPage:       isDayTheme ? "#f4f8fd" : "#080d14"   // outermost page bg
    readonly property color clrPanel:      isDayTheme ? "#e8eef6" : "#0b1219"   // panel / toolbar bg
    readonly property color clrHeader:     isDayTheme ? "#dce6f2" : "#070c12"   // column header bg
    readonly property color clrRowEven:    isDayTheme ? "#ffffff" : "#0f1825"   // even frame rows
    readonly property color clrRowOdd:     isDayTheme ? "#f2f6fb" : "#121e2e"   // odd frame rows
    readonly property color clrRowSignal:  isDayTheme ? "#eef3fa" : "#0c1422"   // signal child rows
    readonly property color clrRowError:   isDayTheme ? "#ffecef" : "#200f10"   // error frame rows
    readonly property color clrRowHover:   isDayTheme ? "#e3edf8" : "#1a2535"   // hover highlight
    readonly property color clrRowSelect:  isDayTheme ? "#d6e6f8" : "#1a3558"   // selected row
    readonly property color clrBorder:     isDayTheme ? "#c0d0e3" : "#1a2840"   // separator lines
    readonly property color clrScrollBg:   isDayTheme ? "#e4edf7" : "#080d14"   // scrollbar track
    readonly property color clrScrollBar:  isDayTheme ? "#b3c5db" : "#1e3050"   // scrollbar thumb

    // Text colours
    readonly property color clrTextMain:   isDayTheme ? "#1f2f41" : "#c8daf0"   // default cell text
    readonly property color clrTextMuted:  isDayTheme ? "#5d7188" : "#5a7a9a"   // muted / metadata
    readonly property color clrTextHeader: isDayTheme ? "#455b75" : "#7a9ab8"   // column header labels
    readonly property color clrDecoded:    isDayTheme ? "#1f79c6" : "#56b4f5"   // DBC-decoded name (blue)
    readonly property color clrSignalText: isDayTheme ? "#2e6ea8" : "#7dcfff"   // signal child rows
    readonly property color clrFD:         isDayTheme ? "#986600" : "#ffd070"   // CAN FD event type (amber)
    readonly property color clrError:      isDayTheme ? "#be2e40" : "#ff6666"   // error frames (red)
    readonly property color clrTx:         isDayTheme ? "#607388" : "#aabbc8"   // TX echoes (muted)
    readonly property color clrCH1:        isDayTheme ? "#2f8fe0" : "#4da8ff"   // Channel 1 (blue)
    readonly property color clrCH2:        isDayTheme ? "#cb7a3a" : "#ff8c4d"   // Channel 2 (orange)

    // Toolbar button accent colours
    readonly property color clrBtnStart:   isDayTheme ? "#dff3e4" : "#1e5c2a"
    readonly property color clrBtnStop:    isDayTheme ? "#f5dfe4" : "#5c1e1e"
    readonly property color clrBtnPause:   isDayTheme ? "#f3eacb" : "#4a3a10"
    readonly property color clrBtnClear:   isDayTheme ? "#f7e3e3" : "#4a1f1f"
    readonly property color clrBtnSave:    isDayTheme ? "#dfe9f7" : "#1a304a"
    readonly property color clrBtnDBC:     isDayTheme ? "#e3e9f8" : "#1f2e4a"

    // ─────────────────────────────────────────────────────────────────────────
    //  COLUMN WIDTHS
    //
    //  -1 means "fill remaining width" (assigned to the Data column).
    //  Column 7 (Data) stretches to fill — accommodates CAN FD 64-byte dumps.
    // ─────────────────────────────────────────────────────────────────────────
    readonly property var colWidths: [
        132,  // 0  Time       "  1234.567890"
        170,  // 1  Name       "EngineData"
         78,  // 2  ID         "18DB33F1h"
         38,  // 3  Chn        "1" / "2"
         82,  // 4  Event Type "CAN FD BRS"
         38,  // 5  Dir        "Rx" / "Tx"
         38,  // 6  DLC        "64"
         -1   // 7  Data       fill — "AA BB CC DD ..."
    ]

    readonly property int rowH:       22    // frame row height (px)
    readonly property int sigRowH:    20    // signal child row height (px)
    readonly property int headerH:    26    // column header height
    readonly property int toolbarH:   46    // main toolbar height
    readonly property int statusBarH: 32    // channel status bar height

    // ─────────────────────────────────────────────────────────────────────────
    //  FILTER STATE
    // ─────────────────────────────────────────────────────────────────────────
    property string filterText: ""          // bound to filter TextField
    property bool dropHighlightActive: false

    function isSupportedTraceLogUrl(urlValue) {
        if (!urlValue)
            return false

        const path = urlValue.toString().toLowerCase()
        return path.endsWith(".asc") || path.endsWith(".blf")
    }

    function hasSupportedTraceLog(urls) {
        if (!urls || urls.length === 0)
            return false

        for (let i = 0; i < urls.length; ++i) {
            if (isSupportedTraceLogUrl(urls[i]))
                return true
        }
        return false
    }

    function importDroppedTraceUrls(urls) {
        if (!urls || urls.length === 0)
            return

        let append = false
        for (let i = 0; i < urls.length; ++i) {
            if (!isSupportedTraceLogUrl(urls[i]))
                continue

            if (AppController.importTraceLog(urls[i].toString(), append))
                append = true
        }
    }

    // ─────────────────────────────────────────────────────────────────────────
    //  Page background
    // ─────────────────────────────────────────────────────────────────────────
    background: Rectangle { color: tracePage.clrPage }

    // =========================================================================
    //  FILE DIALOGS
    // =========================================================================

    FileDialog {
        id: saveDialog
        title: "Save Trace As"
        fileMode: FileDialog.SaveFile

        // ── Format options ────────────────────────────────────────────────────
        //  ASC  — Vector ASCII Log  (human-readable text, opens in CANalyzer)
        //  BLF  — Vector Binary Log (compact binary, opens in CANalyzer/CANoe)
        //  CSV  — Comma-separated   (opens in Excel / any text editor)
        //
        //  The C++ AppController::saveTrace() reads the file extension and
        //  picks the right exporter automatically — no extra QML logic needed.
        nameFilters: [
            "Vector ASC Files (*.asc)",
            "Vector BLF Files (*.blf)",
            "CSV Files (*.csv)",
            "All Files (*)"
        ]
        defaultSuffix: "asc"   // ASC is the most common CAN exchange format

        onAccepted: AppController.saveTrace(selectedFile.toString())
    }

    // NOTE: DBC loading has moved to the CAN Config dialog (per-channel).
    // The dbcDialog FileDialog is no longer needed here.

    // =========================================================================
    //  HEADER SECTION  (toolbar + channel status bar)
    // =========================================================================

    header: Rectangle {
        id: headerSection
        width: parent.width
        height: tracePage.toolbarH + tracePage.statusBarH
        color: tracePage.clrPanel

        // Bottom border of header block
        Rectangle {
            anchors.bottom: parent.bottom
            width: parent.width
            height: 1
            color: tracePage.clrBorder
        }

        Column {
            anchors.fill: parent
            spacing: 0

            // ─────────────────────────────────────────────────────────────────
            //  MAIN TOOLBAR
            // ─────────────────────────────────────────────────────────────────
            Rectangle {
                width: parent.width
                height: tracePage.toolbarH
                color: tracePage.isDayTheme ? "#edf3fb" : "#090f18"

                // Bottom border
                Rectangle {
                    anchors.bottom: parent.bottom
                    width: parent.width
                    height: 1
                    color: tracePage.clrBorder
                }

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin:  10
                    anchors.rightMargin: 10
                    spacing: 3

                    // ── Measurement group ──────────────────────────────────────
                    //
                    // Start: begins capturing frames (auto-connects if needed).
                    // Stop:  stops the trace display (stays connected to bus).
                    TraceToolButton {
                        label: AppController.measuring ? "Stop" : "Start"
                        accentColor: AppController.measuring
                                     ? tracePage.clrBtnStop
                                     : tracePage.clrBtnStart
                        borderColor: AppController.measuring
                                     ? (tracePage.isDayTheme ? "#d94f6b" : "#ff5555")
                                     : (tracePage.isDayTheme ? "#2f9751" : "#4aff7f")
                        implicitWidth: 72
                        onClicked: AppController.startMeasurement()
                        enabled: !AppController.paused || AppController.measuring
                    }

                    TraceToolButton {
                        label: AppController.paused ? "Resume" : "Pause"
                        accentColor: tracePage.clrBtnPause
                        borderColor: tracePage.isDayTheme ? "#b5882d" : "#ffd070"
                        implicitWidth: 72
                        onClicked: AppController.pauseMeasurement()
                        enabled: AppController.measuring
                        opacity: enabled ? 1.0 : 0.4
                    }

                    // Separator
                    Rectangle {
                        width: 1; height: 28
                        color: tracePage.clrBorder
                        Layout.leftMargin: 4; Layout.rightMargin: 4
                    }

                    TraceToolButton {
                        label: "Clear"
                        accentColor: tracePage.clrBtnClear
                        borderColor: "#ff7070"
                        implicitWidth: 60
                        onClicked: AppController.clearTrace()
                    }

                    TraceToolButton {
                        label: "Save..."
                        accentColor: tracePage.clrBtnSave
                        borderColor: "#5599cc"
                        implicitWidth: 60
                        onClicked: saveDialog.open()
                    }

                    Label {
                        text: "Drop .asc/.blf to analyze"
                        color: tracePage.clrTextMuted
                        font.pixelSize: 10
                        Layout.leftMargin: 6
                    }

                    // Separator
                    Rectangle {
                        width: 1; height: 28
                        color: tracePage.clrBorder
                        Layout.leftMargin: 4; Layout.rightMargin: 4
                    }

                    // Auto-scroll toggle
                    CheckBox {
                        id: autoScrollChk
                        checked: true
                        spacing: 5

                        indicator: Rectangle {
                            x: autoScrollChk.leftPadding
                            y: autoScrollChk.topPadding
                               + (autoScrollChk.availableHeight - height) / 2
                            implicitWidth: 15; implicitHeight: 15
                            radius: 3
                            color: autoScrollChk.checked
                                   ? (tracePage.isDayTheme ? "#d7e8f9" : "#1e4a7a")
                                   : "transparent"
                            border.color: autoScrollChk.checked
                                          ? tracePage.clrCH1
                                          : (tracePage.isDayTheme ? "#8ba8c8" : "#2a4060")
                            border.width: 1

                            Rectangle {
                                anchors.centerIn: parent
                                width: 6; height: 6; radius: 1
                                color: tracePage.clrCH1
                                visible: autoScrollChk.checked
                            }
                        }

                        contentItem: Label {
                            leftPadding: autoScrollChk.indicator.width
                                         + autoScrollChk.spacing + 2
                            text: "Auto-scroll"
                            color: tracePage.clrTextMain
                            font.pixelSize: 11
                            verticalAlignment: Text.AlignVCenter
                        }
                    }

                    Rectangle {
                        width: 1; height: 24
                        color: tracePage.clrBorder
                        Layout.leftMargin: 2
                        Layout.rightMargin: 4
                    }

                    Label {
                        text: "Display:"
                        color: tracePage.clrTextMuted
                        font.pixelSize: 11
                    }

                    Rectangle {
                        id: displayModeToggle
                        implicitWidth: 126
                        implicitHeight: 24
                        radius: 4
                        color: tracePage.isDayTheme ? "#eef3fa" : "#0e1623"
                        border.color: tracePage.clrBorder
                        border.width: 1

                        Row {
                            anchors.fill: parent
                            anchors.margins: 1
                            spacing: 1

                            Rectangle {
                                width: (displayModeToggle.width - 3) / 2
                                height: displayModeToggle.height - 2
                                radius: 3
                                color: !AppController.inPlaceDisplayMode
                                       ? (tracePage.isDayTheme ? "#d9e9fb" : "#1a385d")
                                       : "transparent"
                                border.width: !AppController.inPlaceDisplayMode ? 1 : 0
                                border.color: tracePage.clrCH1

                                Label {
                                    anchors.centerIn: parent
                                    text: "Append"
                                    color: !AppController.inPlaceDisplayMode
                                           ? tracePage.clrCH1
                                           : tracePage.clrTextMuted
                                    font.pixelSize: 10
                                    font.bold: !AppController.inPlaceDisplayMode
                                }

                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: AppController.setInPlaceDisplayMode(false)
                                }
                            }

                            Rectangle {
                                width: (displayModeToggle.width - 3) / 2
                                height: displayModeToggle.height - 2
                                radius: 3
                                color: AppController.inPlaceDisplayMode
                                       ? (tracePage.isDayTheme ? "#d9e9fb" : "#1a385d")
                                       : "transparent"
                                border.width: AppController.inPlaceDisplayMode ? 1 : 0
                                border.color: tracePage.clrCH1

                                Label {
                                    anchors.centerIn: parent
                                    text: "In-Place"
                                    color: AppController.inPlaceDisplayMode
                                           ? tracePage.clrCH1
                                           : tracePage.clrTextMuted
                                    font.pixelSize: 10
                                    font.bold: AppController.inPlaceDisplayMode
                                }

                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: AppController.setInPlaceDisplayMode(true)
                                }
                            }
                        }
                    }

                    // Stretch
                    Item { Layout.fillWidth: true }

                    // Separator
                    Rectangle {
                        width: 1; height: 28
                        color: tracePage.clrBorder
                        Layout.leftMargin: 4; Layout.rightMargin: 4
                    }

                    // Filter
                    Label {
                        text: "Filter:"
                        color: tracePage.clrTextMuted
                        font.pixelSize: 11
                    }

                    TextField {
                        id: filterField
                        implicitWidth: 130
                        implicitHeight: 26
                        placeholderText: "ID / Name / Data..."
                        color: tracePage.clrTextMain
                        font.family: "Consolas"
                        font.pixelSize: 11
                        onTextChanged: tracePage.filterText = text

                        background: Rectangle {
                            radius: 4
                            color: tracePage.isDayTheme ? "#ffffff" : "#0d1828"
                            border.color: filterField.activeFocus
                                          ? tracePage.clrCH1 : tracePage.clrBorder
                            border.width: 1
                        }
                    }

                }
                // NOTE: Load DBC button removed — DBC is now configured
                // per-channel in the CAN Config dialog (toolbar button).
            }

            // ─────────────────────────────────────────────────────────────────
            //  CHANNEL STATUS BAR
            //  Shows live statistics and per-channel indicators
            // ─────────────────────────────────────────────────────────────────
            Rectangle {
                width: parent.width
                height: tracePage.statusBarH
                color: tracePage.isDayTheme ? "#e3ebf5" : "#060b12"

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 12
                    anchors.rightMargin: 12
                    spacing: 10

                    // Channel 1 indicator
                    ChannelIndicator {
                        channelNum: 1
                        active: AppController.connected
                        chColor: tracePage.clrCH1
                    }

                    // Channel 2 indicator
                    ChannelIndicator {
                        channelNum: 2
                        active: false   // extend when dual-channel HW is used
                        chColor: tracePage.clrCH2
                    }

                    // Separator
                    Rectangle {
                        width: 1; height: 18; color: tracePage.clrBorder
                    }

                    // Frame count
                    Label {
                        text: "Frames:"
                        color: tracePage.clrTextMuted
                        font.pixelSize: 11
                    }
                    Label {
                        text: AppController.frameCount.toLocaleString()
                        color: tracePage.isDayTheme ? "#1b9361" : "#4aff9a"
                        font.pixelSize: 11
                        font.family: "Consolas"
                    }

                    // Separator
                    Rectangle {
                        width: 1; height: 18; color: tracePage.clrBorder
                    }

                    // Frame rate
                    Label {
                        text: "Rate:"
                        color: tracePage.clrTextMuted
                        font.pixelSize: 11
                    }
                    Label {
                        text: AppController.frameRate + " fps"
                        color: tracePage.isDayTheme ? "#178ab8" : "#4adfff"
                        font.pixelSize: 11
                        font.family: "Consolas"
                    }

                    // Separator
                    Rectangle {
                        width: 1; height: 18; color: tracePage.clrBorder
                    }

                    // Driver / DBC info
                    Label {
                        text: AppController.dbcLoaded
                              ? AppController.dbcInfo
                              : AppController.driverName
                        color: tracePage.clrTextMuted
                        font.pixelSize: 10
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }

                    // Pause badge
                    Rectangle {
                        visible: AppController.paused
                        height: 18
                        width: pauseBadge.implicitWidth + 16
                        radius: 3
                        color: tracePage.isDayTheme ? "#f4e8c8" : "#3a2a00"
                        border.color: tracePage.isDayTheme ? "#c7962c" : "#ffd070"
                        border.width: 1

                        Label {
                            id: pauseBadge
                            anchors.centerIn: parent
                            text: "PAUSED"
                            color: tracePage.isDayTheme ? "#8b6200" : "#ffd070"
                            font.pixelSize: 10
                            font.bold: true
                            font.letterSpacing: 1
                        }
                    }
                }
            }
        }
    }

    // =========================================================================
    //  MAIN CONTENT: Column Header + TreeView
    // =========================================================================

    Rectangle {
        anchors.fill: parent
        color: tracePage.clrPage

        // Column headers (HorizontalHeaderView synced to TreeView)
        HorizontalHeaderView {
            id: headerView
            anchors.top:   parent.top
            anchors.left:  parent.left
            anchors.right: parent.right
            height: tracePage.headerH
            clip:   true
            syncView: traceView
            columnWidthProvider: traceView.columnWidthProvider

            delegate: Rectangle {
                implicitHeight: tracePage.headerH
                color: tracePage.clrHeader

                // Right border separator
                Rectangle {
                    anchors.right: parent.right
                    width: 1; height: parent.height
                    color: tracePage.clrBorder
                }

                // Bottom border
                Rectangle {
                    anchors.bottom: parent.bottom
                    width: parent.width; height: 1
                    color: tracePage.clrBorder
                }

                Label {
                    anchors.fill: parent
                    anchors.leftMargin:  (column === 0) ? 24 : 6
                    anchors.rightMargin: 4
                    text: model.display ?? ""
                    color: tracePage.clrTextHeader
                    font.pixelSize: 11
                    font.bold: true
                    elide: Text.ElideRight
                    verticalAlignment: Text.AlignVCenter
                    horizontalAlignment: {
                        // Mirror data column alignment
                        if (column === 0) return Text.AlignRight
                        if (column === 3 || column === 5 || column === 6)
                            return Text.AlignHCenter
                        return Text.AlignLeft
                    }
                }
            }
        }

        // ─────────────────────────────────────────────────────────────────────
        //  TREE VIEW
        //
        //  Qt 6.3+ TreeView extends TableView and natively supports
        //  expand/collapse via the QAbstractItemModel parent/child API.
        //
        //  The delegate receives injected required properties:
        //    depth       — 0=frame row, 1=signal row
        //    hasChildren — whether this row has expandable children
        //    expanded    — current expanded state of this row
        //    isTreeNode  — true for the tree-node column (col 0 by default)
        //    treeView    — reference back to the TreeView for expand()/collapse()
        // ─────────────────────────────────────────────────────────────────────
        TreeView {
            id: traceView
            anchors.top:    headerView.bottom
            anchors.left:   parent.left
            anchors.right:  parent.right
            anchors.bottom: parent.bottom
            clip: true

            model: AppController.traceModel

            // ── Column widths ─────────────────────────────────────────────────
            columnWidthProvider: function(col) {
                var w = tracePage.colWidths
                if (col >= w.length) return 80

                if (w[col] < 0) {
                    // Fill remaining width for the Data column
                    var used = 0
                    for (var i = 0; i < w.length; ++i)
                        if (w[i] > 0) used += w[i]
                    return Math.max(traceView.width - used, 120)
                }
                return w[col]
            }

            // ── Row heights ───────────────────────────────────────────────────
            rowHeightProvider: function(row) {
                // All rows the same height; Qt queries this for each visible row.
                // We use a single height for both frame and signal rows to keep
                // the layout calculation fast.
                return tracePage.rowH
            }

            // ── Cell delegate ─────────────────────────────────────────────────
            delegate: Item {
                id: cellDelegate
                implicitHeight: tracePage.rowH

                // ── Required properties injected by TreeView ─────────────────
                // These MUST be declared with "required property" so the TreeView
                // binds them automatically when creating each delegate instance.
                required property TreeView treeView
                required property bool isTreeNode  // true for column 0
                required property bool expanded
                required property bool hasChildren
                required property int  depth       // 0=frame, 1=signal
                required property int  row
                required property int  column

                // ── Model data (via implicit model context) ──────────────────
                // model.display  = Qt::DisplayRole text for this (row, col)
                // model.isFrame  = custom IsFrameRole (false for signal rows)
                // model.channel  = custom ChannelRole (1 or 2)
                // model.isError  = custom IsErrorRole
                // model.isFD     = custom IsFDRole
                // model.isDecoded= custom IsDecodedRole

                // ── Derived state ────────────────────────────────────────────
                readonly property bool isSignalRow: depth > 0
                readonly property int  channelNum:  model.channel  ?? 1
                readonly property bool isError:     model.isError  ?? false
                readonly property bool isFD:        model.isFD     ?? false
                readonly property bool isDecoded:   model.isDecoded ?? false
                readonly property string cellText:  model.display  ?? ""

                // ── Row background ────────────────────────────────────────────
                Rectangle {
                    anchors.fill: parent
                    color: {
                        if (cellDelegate.isError)     return tracePage.clrRowError
                        if (cellDelegate.isSignalRow) return tracePage.clrRowSignal
                        return cellDelegate.row % 2 === 0
                               ? tracePage.clrRowEven
                               : tracePage.clrRowOdd
                    }
                }

                // ── Channel-2 coloured left bar (col 0 only, frame rows) ──────
                // A thin orange bar on the left edge visually groups CH2 frames
                // so the operator can instantly see which channel a row came from.
                Rectangle {
                    visible:  column === 0 && !isSignalRow && channelNum === 2
                    anchors.left: parent.left
                    width: 3; height: parent.height
                    color: tracePage.clrCH2
                    opacity: 0.85
                }

                // ── Expand / Collapse button (col 0, frame rows with children) ──
                //
                //  The expand button is a small triangle inside col 0.
                //  Clicking it calls treeView.expand()/collapse(row) which
                //  instructs Qt to query rowCount(frameIndex) and insert/remove
                //  the child signal rows.  No manual JS row tracking required.
                Rectangle {
                    id: expandBtn
                    visible: column === 0 && !isSignalRow && hasChildren
                    anchors.left:           parent.left
                    anchors.leftMargin:     4
                    anchors.verticalCenter: parent.verticalCenter
                    width: 16; height: 14
                    radius: 3
                    color: expanded
                           ? (tracePage.isDayTheme ? "#d7e8f9" : "#1e3d6a")
                           : (tracePage.isDayTheme ? "#eaf2fb" : "#0f2035")
                    border.color: tracePage.isDayTheme ? "#8fb2d4" : "#2a5580"
                    border.width: 1

                    // Triangle indicator: ▶ = collapsed, ▼ = expanded
                    Text {
                        anchors.centerIn: parent
                        text: expanded ? "\u25BC" : "\u25B6"   // ▼ or ▶
                        color: tracePage.clrCH1
                        font.pixelSize: 7
                    }

                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            // Qt 6 TreeView API: expand() / collapse() / isExpanded()
                            // There is no toggleExpansion() — toggle manually:
                            const tv  = cellDelegate.treeView
                            const row = cellDelegate.row
                            tv.isExpanded(row) ? tv.collapse(row) : tv.expand(row)
                        }
                    }
                }

                // ── Cell text ─────────────────────────────────────────────────
                Text {
                    id: cellText
                    anchors.left: parent.left
                    anchors.leftMargin: {
                        // Col 0: leave room for the expand button (24px)
                        if (column === 0) return 24
                        // Col 1: indent signal names under their parent frame
                        if (column === 1 && isSignalRow) return 22
                        return 6
                    }
                    anchors.right:           parent.right
                    anchors.rightMargin:     4
                    anchors.verticalCenter:  parent.verticalCenter
                    height:                  parent.height

                    text:        cellDelegate.cellText
                    elide:       Text.ElideRight
                    verticalAlignment: Text.AlignVCenter
                    wrapMode:    Text.NoWrap

                    // ── Horizontal alignment per column ───────────────────────
                    horizontalAlignment: {
                        switch (column) {
                        case 0:  return Text.AlignRight   // time  right-aligned
                        case 3:
                        case 5:
                        case 6:  return Text.AlignHCenter // chn/dir/dlc centred
                        default: return Text.AlignLeft
                        }
                    }

                    // ── Font ─────────────────────────────────────────────────
                    font.pixelSize: 11
                    font.family: {
                        // Monospaced font for columns that need precise alignment
                        // Col 0: timestamps (numeric, fixed-width comparison)
                        // Col 7: hex data dump
                        if (column === 0 || column === 7)
                            return "Consolas"
                        return "Segoe UI"   // or let it fall back to system font
                    }

                    // ── Text colour ───────────────────────────────────────────
                    color: {
                        // Signal child rows: always light blue
                        if (isSignalRow) return tracePage.clrSignalText

                        // Error frames: red
                        if (isError) return tracePage.clrError

                        // TX echoes: muted
                        if (model.display === "Tx" && column === 5)
                            return tracePage.clrFD

                        switch (column) {
                        case 1:  // Name column
                            return isDecoded
                                   ? tracePage.clrDecoded     // blue = known frame
                                   : tracePage.clrTextMuted   // grey = unknown
                        case 3:  // Channel column
                            return channelNum === 2
                                   ? tracePage.clrCH2         // orange = CH2
                                   : tracePage.clrCH1         // blue   = CH1
                        case 4:  // Event Type
                            return isFD
                                   ? tracePage.clrFD          // amber = CAN FD
                                   : tracePage.clrTextMain
                        case 0:  // Timestamp
                            return tracePage.clrTextHeader
                        default:
                            return tracePage.clrTextMain
                        }
                    }
                }

                // ── Bottom row separator ──────────────────────────────────────
                Rectangle {
                    anchors.bottom: parent.bottom
                    anchors.left:   parent.left
                    anchors.right:  parent.right
                    height: 1
                    color: tracePage.isDayTheme ? "#d9e4f2" : "#0a1420"
                }

                // ── Right column separator ────────────────────────────────────
                Rectangle {
                    anchors.right:  parent.right
                    anchors.top:    parent.top
                    anchors.bottom: parent.bottom
                    width: 1
                    color: tracePage.isDayTheme ? "#d4dfec" : "#0f1e30"
                    visible: column < 7     // no right border on last column
                }

                // ── Hover highlight ───────────────────────────────────────────
                Rectangle {
                    anchors.fill: parent
                    color: "white"
                    opacity: hoverArea.containsMouse ? 0.035 : 0.0

                    Behavior on opacity {
                        NumberAnimation { duration: 80 }
                    }

                    MouseArea {
                        id: hoverArea
                        anchors.fill: parent
                        hoverEnabled: true
                        acceptedButtons: Qt.LeftButton

                        // Click on col 0 (anywhere on row) also toggles expand
                        onClicked: function(mouse) {
                            if (column === 0 && hasChildren) {
                                const tv  = cellDelegate.treeView
                                const row = cellDelegate.row
                                tv.isExpanded(row) ? tv.collapse(row) : tv.expand(row)
                            }
                        }
                    }
                }
            }   // delegate

            // ── Scrollbars ─────────────────────────────────────────────────────
            ScrollBar.vertical: ScrollBar {
                policy: ScrollBar.AsNeeded
                background: Rectangle { color: tracePage.clrScrollBg }
                contentItem: Rectangle {
                    implicitWidth: 8
                    radius: 4
                    color: tracePage.clrScrollBar
                }
            }

            ScrollBar.horizontal: ScrollBar {
                policy: ScrollBar.AsNeeded
                background: Rectangle { color: tracePage.clrScrollBg }
                contentItem: Rectangle {
                    implicitHeight: 8
                    radius: 4
                    color: tracePage.clrScrollBar
                }
            }

        }   // TreeView

        DropArea {
            anchors.fill: parent

            onEntered: function(drag) {
                if (!drag.hasUrls || !tracePage.hasSupportedTraceLog(drag.urls)) {
                    tracePage.dropHighlightActive = false
                    return
                }

                tracePage.dropHighlightActive = true
                drag.acceptProposedAction()
            }

            onPositionChanged: function(drag) {
                tracePage.dropHighlightActive =
                    drag.hasUrls && tracePage.hasSupportedTraceLog(drag.urls)
            }

            onExited: {
                tracePage.dropHighlightActive = false
            }

            onDropped: function(drop) {
                const canImport =
                    drop.hasUrls && tracePage.hasSupportedTraceLog(drop.urls)

                tracePage.dropHighlightActive = false
                if (!canImport)
                    return

                drop.acceptProposedAction()
                tracePage.importDroppedTraceUrls(drop.urls)
            }
        }

        Rectangle {
            anchors.fill: parent
            visible: tracePage.dropHighlightActive
            z: 20
            radius: 8
            color: tracePage.isDayTheme ? "#dcecff" : "#102f52"
            border.color: tracePage.isDayTheme ? "#2f84d8" : "#56b4f5"
            border.width: 2
            opacity: 0.94

            Label {
                anchors.centerIn: parent
                text: "Drop ASC/BLF trace logs to analyze offline"
                color: tracePage.isDayTheme ? "#174773" : "#d8edff"
                font.pixelSize: 18
                font.bold: true
            }
        }
    }   // Rectangle (main content)

    // =========================================================================
    //  AUTO-SCROLL — follow new rows when they are inserted
    //
    //  WHY: We listen to rowsInserted on the model (at root level = frame rows)
    //  and call positionViewAtRow() to jump to the last visible row.
    //
    //  NOTE: positionViewAtRow() takes a "flat" row index for the visible rows
    //  in the view. Signal rows are also counted here. To follow only frame rows
    //  we use traceView.rows - 1 which reflects the total visible row count.
    // =========================================================================
    Connections {
        target: AppController.traceModel

        function onRowsInserted(parent, first, last) {
            // Only auto-scroll if the user has opted in and the view is showing
            // the bottom (not scrolled up to review old data)
            if (autoScrollChk.checked && !parent.valid) {
                // Schedule for next frame — Qt needs one event loop cycle to
                // process the insert before positionViewAtRow() takes effect.
                Qt.callLater(function() {
                    traceView.positionViewAtRow(
                        traceView.rows - 1, TableView.AlignBottom)
                })
            }
        }

        function onModelReset() {
            // After clear(), return to the top
            traceView.positionViewAtRow(0, TableView.AlignTop)
        }
    }

    // =========================================================================
    //  INLINE COMPONENTS
    //  Defined as components so they can be reused throughout this file
    //  without creating separate QML files.
    // =========================================================================

    // ── Toolbar button ────────────────────────────────────────────────────────
    component TraceToolButton: Rectangle {
        id: ttbRoot
        property string label:       "Button"
        property color  accentColor: tracePage.isDayTheme ? "#e9f1fb" : "#1a2535"
        property color  borderColor: tracePage.isDayTheme ? "#9eb8d6" : "#3a5a8a"
        signal clicked()

        implicitWidth:  64
        implicitHeight: 28
        radius: 4
        color: ttbPressArea.containsPress
               ? Qt.darker(accentColor, 1.3)
               : ttbPressArea.containsMouse
                 ? Qt.lighter(accentColor, 1.2)
                 : accentColor
        border.color: borderColor
        border.width: 1

        Behavior on color { ColorAnimation { duration: 80 } }

        Label {
            anchors.centerIn: parent
            text: ttbRoot.label
            color: tracePage.clrTextMain
            font.pixelSize: 11
        }

        MouseArea {
            id: ttbPressArea
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: ttbRoot.clicked()
        }
    }

    // ── Channel indicator ─────────────────────────────────────────────────────
    component ChannelIndicator: RowLayout {
        property int   channelNum: 1
        property bool  active:     false
        property color chColor:    tracePage.clrCH1
        spacing: 5

        // LED dot
        Rectangle {
            width: 8; height: 8; radius: 4
            color: active ? chColor : (tracePage.isDayTheme ? "#c1d0e1" : "#1a2535")
            border.color: chColor
            border.width: 1

            // Pulse animation when active
            SequentialAnimation on opacity {
                running: active
                loops: Animation.Infinite
                NumberAnimation { to: 0.4; duration: 600; easing.type: Easing.InOutSine }
                NumberAnimation { to: 1.0; duration: 600; easing.type: Easing.InOutSine }
            }
        }

        Label {
            text: "CH" + channelNum
            color: active ? chColor : (tracePage.isDayTheme ? "#6f879f" : "#3a5a7a")
            font.pixelSize: 11
            font.bold: active
        }
    }

}
