/**
 * CANConfigDialog.qml — CAN Port Configuration Dialog
 *
 * ═══════════════════════════════════════════════════════════════════════════
 *  PURPOSE
 * ═══════════════════════════════════════════════════════════════════════════
 *
 *  Launched by the "CAN Config" button in the main toolbar.
 *  Lets the user configure up to 4 CAN channel slots before connecting.
 *
 *  Per-channel settings:
 *    • Enable toggle (checkbox)
 *    • Alias name    (text input — shown in trace status bar)
 *    • HW mapping    (which physical port from AppController.channelList)
 *    • Mode          (CAN HS or CAN FD radio buttons)
 *    • Nominal bitrate (125k / 250k / 500k / 800k / 1M kbit/s)
 *    • Data bitrate    (1M / 2M / 4M / 8M Mbit/s — enabled only for FD)
 *    • DBC file        (Browse button → per-channel decode database)
 *
 * ═══════════════════════════════════════════════════════════════════════════
 *  ARCHITECTURE — WHY individual section objects (not a Repeater)
 * ═══════════════════════════════════════════════════════════════════════════
 *
 *  The dialog needs exactly 4 channel slots, each with ~10 editable fields.
 *  Using a Repeater over a plain JS array doesn't trigger QML bindings when
 *  array elements change (arrays aren't observable in QML).
 *
 *  Solution: define each channel section as a Component instance (ch1, ch2,
 *  ch3, ch4) with its own properties. Properties ARE observable — any control
 *  bound to ch1.chEnabled auto-updates when that property changes.
 *
 *  On open:  populateFromController() copies AppController config → section props.
 *  On Apply: buildConfigList() reads section props → QVariantList for C++.
 *
 * ═══════════════════════════════════════════════════════════════════════════
 *  USAGE (in Main.qml)
 * ═══════════════════════════════════════════════════════════════════════════
 *
 *    CANConfigDialog {
 *        id: canConfigDlg
 *        appWindow: root          // for isDayTheme binding
 *    }
 *    // Then: canConfigDlg.open()
 */

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

Popup {
    id: dlg

    // ─── Public API ───────────────────────────────────────────────────────────
    /** Set to the ApplicationWindow so we can read isDayTheme for theming. */
    required property var appWindow

    // Convenience colour aliases derived from the parent window's theme
    readonly property bool  dark:    appWindow ? !appWindow.isDayTheme : false
    readonly property color bgMain:  dark ? "#0d1420" : "#f0f4fb"
    readonly property color bgPanel: dark ? "#141c28" : "#e6edf8"
    readonly property color bgCard:  dark ? "#1a2333" : "#ffffff"
    readonly property color bgInput: dark ? "#0f1824" : "#ffffff"
    readonly property color border:  dark ? "#263550" : "#c0cfe0"
    readonly property color accent:  "#35b8ff"
    readonly property color txtMain: dark ? "#dce8f8" : "#1c2d40"
    readonly property color txtMute: dark ? "#7a9ab8" : "#5a7090"
    readonly property color success: dark ? "#59d892" : "#1f8e57"
    readonly property color danger:  dark ? "#ff7070" : "#be2e40"

    // ─── Popup layout ─────────────────────────────────────────────────────────
    parent:      Overlay.overlay
    anchors.centerIn: parent

    width:  660
    height: Math.min(contentCol.implicitHeight + 32, 780)

    modal:       true
    closePolicy: Popup.CloseOnEscape

    padding: 0
    topPadding: 0

    // ─── Background ───────────────────────────────────────────────────────────
    background: Rectangle {
        color:        dlg.bgMain
        radius:       12
        border.color: dlg.border
        border.width: 1

        // Drop shadow effect (simple offset rect underneath)
        Rectangle {
            anchors.fill:   parent
            anchors.topMargin:    2
            anchors.leftMargin:   2
            z: -1
            radius: parent.radius
            color: dlg.dark ? "#000000" : "#b0c4d8"
            opacity: 0.35
        }
    }

    // ─── Open / Close hooks ───────────────────────────────────────────────────
    onOpened: populateFromController()

    // ─── Per-channel section IDs ──────────────────────────────────────────────
    // Each section object carries the working-copy properties for one channel.
    // We read/write these directly in all the UI controls below.

    ChannelSectionData { id: ch0; chIndex: 0; chAlias: "CH1" }
    ChannelSectionData { id: ch1; chIndex: 1; chAlias: "CH2" }
    ChannelSectionData { id: ch2; chIndex: 2; chAlias: "CH3" }
    ChannelSectionData { id: ch3; chIndex: 3; chAlias: "CH4" }

    // Helper: array of channel data objects for iteration in JS
    readonly property var channels: [ch0, ch1, ch2, ch3]

    // ─── Helpers ──────────────────────────────────────────────────────────────

    /** Copy current AppController configs into the local section properties. */
    function populateFromController() {
        var cfgs = AppController.getChannelConfigs()
        for (var i = 0; i < 4; i++) {
            var c = cfgs[i]
            var ch = channels[i]
            ch.chEnabled     = c.enabled        || false
            ch.chAlias       = c.alias          || ("CH" + (i+1))
            ch.chHwIndex     = c.hwChannelIndex !== undefined ? c.hwChannelIndex : -1
            ch.chFdEnabled   = c.fdEnabled      || false
            ch.chBitrate     = c.bitrate        || 500000
            ch.chDataBitrate = c.dataBitrate    || 2000000
            ch.chDbcPath     = c.dbcFilePath    || ""
            ch.chDbcInfo     = c.dbcInfo        || ""
        }
    }

    /**
     * openDbcPicker() — called from ChannelSection's Browse button.
     *
     * WHY a function instead of dlgRef.dbcFilePicker.open() directly:
     * QML ids (like dbcFilePicker) are scoped to the component that defines them.
     * Inside an inline 'component', dlgRef.dbcFilePicker is undefined because ids
     * are not automatically exposed as properties on the parent object.
     * A function IS accessible via dlgRef because functions are regular QML properties.
     */
    function openDbcPicker(channelIndex) {
        pendingDbcChannel = channelIndex
        dbcFilePicker.open()
    }

    /** Collect section properties into a QVariantList for AppController. */
    function buildConfigList() {
        var list = []
        for (var i = 0; i < 4; i++) {
            var ch = channels[i]
            list.push({
                "enabled":        ch.chEnabled,
                "alias":          ch.chAlias,
                "hwChannelIndex": ch.chHwIndex,
                "fdEnabled":      ch.chFdEnabled,
                "bitrate":        ch.chBitrate,
                "dataBitrate":    ch.chDataBitrate,
                "dbcFilePath":    ch.chDbcPath,
                "dbcInfo":        ch.chDbcInfo
            })
        }
        return list
    }

    // ─── DBC file dialogs (one per channel, opened on demand) ─────────────────
    //
    // WHY one per channel: FileDialog is non-modal on some platforms,
    // so we need to know which channel to update after the user picks a file.
    // We use a single dialog and a "pending channel" property instead of 4
    // dialogs to save resources.

    property int pendingDbcChannel: -1  // which channel's DBC is being browsed

    FileDialog {
        id: dbcFilePicker
        title:       "Select DBC File for CH" + (dlg.pendingDbcChannel + 1)
        fileMode:    FileDialog.OpenFile
        nameFilters: ["DBC Files (*.dbc)", "All Files (*)"]

        onAccepted: {
            var ch = dlg.channels[dlg.pendingDbcChannel]
            // preloadChannelDbc() parses the DBC immediately and returns an info string
            var info = AppController.preloadChannelDbc(dlg.pendingDbcChannel,
                                                       selectedFile.toString())
            if (info !== "") {
                ch.chDbcPath = selectedFile.toString().replace(/^file:\/\/\//, "")
                ch.chDbcInfo = info
            }
        }
    }

    // ─── Content ──────────────────────────────────────────────────────────────

    contentItem: Column {
        id: contentCol
        width:   dlg.width
        spacing: 0

        // ── Title bar ─────────────────────────────────────────────────────────
        Rectangle {
            width:  parent.width
            height: 48
            color:  dlg.bgPanel
            radius: 12

            // Only round the top corners
            Rectangle {
                anchors.bottom: parent.bottom
                width:  parent.width
                height: parent.height / 2
                color:  parent.color
            }

            // Bottom border
            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width; height: 1
                color: dlg.border
            }

            RowLayout {
                anchors.fill:        parent
                anchors.leftMargin:  18
                anchors.rightMargin: 12
                spacing: 8

                // Icon: gear symbol (drawn with Canvas)
                Canvas {
                    id: dlgGearIcon   // WHY id: Connections handlers in Qt 6 do NOT
                                      // inherit parent — must reference by id, not parent.
                    width: 18; height: 18
                    antialiasing: true
                    onPaint: {
                        var ctx = getContext("2d")
                        ctx.clearRect(0, 0, width, height)
                        ctx.strokeStyle = dlg.accent
                        ctx.lineWidth   = 1.5
                        ctx.lineCap     = "round"
                        var cx = width/2, cy = height/2, r = 5, sr = 2.5
                        // Gear teeth
                        for (var i = 0; i < 6; ++i) {
                            var a = i * Math.PI / 3
                            ctx.beginPath()
                            ctx.moveTo(cx + Math.cos(a) * r,   cy + Math.sin(a) * r)
                            ctx.lineTo(cx + Math.cos(a) * (r+2.5), cy + Math.sin(a) * (r+2.5))
                            ctx.stroke()
                        }
                        ctx.beginPath()
                        ctx.arc(cx, cy, r, 0, Math.PI * 2)
                        ctx.stroke()
                        ctx.beginPath()
                        ctx.arc(cx, cy, sr, 0, Math.PI * 2)
                        ctx.stroke()
                    }
                    Component.onCompleted: requestPaint()
                    Connections {
                        target: dlg
                        function onDarkChanged() { dlgGearIcon.requestPaint() }
                    }
                }

                Label {
                    text:             "CAN Port Configuration"
                    color:            dlg.txtMain
                    font.pixelSize:   14
                    font.bold:        true
                    font.letterSpacing: 0.4
                }

                Item { Layout.fillWidth: true }

                // Refresh HW button
                Rectangle {
                    implicitWidth:  110
                    implicitHeight: 26
                    radius:         6
                    color:          refreshMouse.pressed
                                    ? Qt.darker(dlg.bgCard, 1.1)
                                    : (refreshMouse.containsMouse ? dlg.bgCard : "transparent")
                    border.color:   dlg.border
                    border.width:   1

                    RowLayout {
                        anchors.centerIn: parent
                        spacing: 4

                        Label {
                            text:           "⟳"
                            color:          dlg.accent
                            font.pixelSize: 13
                        }
                        Label {
                            text:           "Refresh HW"
                            color:          dlg.txtMain
                            font.pixelSize: 11
                        }
                    }

                    MouseArea {
                        id:           refreshMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape:  Qt.PointingHandCursor
                        onClicked:    AppController.refreshChannels()
                    }
                }

                // Close (×) button
                Rectangle {
                    implicitWidth:  28
                    implicitHeight: 28
                    radius:         6
                    color:          closeMouse.pressed
                                    ? "#c13e60"
                                    : (closeMouse.containsMouse ? "#a83553" : "transparent")

                    Label {
                        anchors.centerIn: parent
                        text:           "✕"
                        color:          closeMouse.containsMouse ? "white" : dlg.txtMute
                        font.pixelSize: 12
                    }

                    MouseArea {
                        id:           closeMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape:  Qt.PointingHandCursor
                        onClicked:    dlg.close()
                    }
                }
            }
        }

        // ── Channel sections (scrollable) ─────────────────────────────────────
        ScrollView {
            width:              parent.width
            height:             Math.min(contentHeight, 640)
            clip:               true
            ScrollBar.vertical.policy: ScrollBar.AsNeeded

            // WHY contentHeight limit: on small screens with all 4 channels
            // open, the dialog would exceed the display. ScrollView caps it.

            Column {
                width:   dlg.width
                padding: 14
                spacing: 10

                // WHY 4 explicit instances instead of Repeater:
                // Qt 6 inline components with 'required property' declarations opt out of
                // Repeater context-property injection — 'index' becomes undefined at runtime,
                // which makes dlg.channels[index] = undefined and cascades into every
                // chData.xxx binding firing TypeError. Explicit ch0..ch3 bindings are reliable.

                ChannelSection {
                    width:        dlg.width - 28
                    chData:       ch0
                    isDark:       dlg.dark
                    dlgRef:       dlg
                    bgCardColor:  dlg.bgCard
                    bgInputColor: dlg.bgInput
                    borderColor:  dlg.border
                    accentColor:  dlg.accent
                    txtMainColor: dlg.txtMain
                    txtMuteColor: dlg.txtMute
                    successColor: dlg.success
                    dangerColor:  dlg.danger
                }
                ChannelSection {
                    width:        dlg.width - 28
                    chData:       ch1
                    isDark:       dlg.dark
                    dlgRef:       dlg
                    bgCardColor:  dlg.bgCard
                    bgInputColor: dlg.bgInput
                    borderColor:  dlg.border
                    accentColor:  dlg.accent
                    txtMainColor: dlg.txtMain
                    txtMuteColor: dlg.txtMute
                    successColor: dlg.success
                    dangerColor:  dlg.danger
                }
                ChannelSection {
                    width:        dlg.width - 28
                    chData:       ch2
                    isDark:       dlg.dark
                    dlgRef:       dlg
                    bgCardColor:  dlg.bgCard
                    bgInputColor: dlg.bgInput
                    borderColor:  dlg.border
                    accentColor:  dlg.accent
                    txtMainColor: dlg.txtMain
                    txtMuteColor: dlg.txtMute
                    successColor: dlg.success
                    dangerColor:  dlg.danger
                }
                ChannelSection {
                    width:        dlg.width - 28
                    chData:       ch3
                    isDark:       dlg.dark
                    dlgRef:       dlg
                    bgCardColor:  dlg.bgCard
                    bgInputColor: dlg.bgInput
                    borderColor:  dlg.border
                    accentColor:  dlg.accent
                    txtMainColor: dlg.txtMain
                    txtMuteColor: dlg.txtMute
                    successColor: dlg.success
                    dangerColor:  dlg.danger
                }

                // Bottom spacer
                Item { width: 1; height: 4 }
            }
        }

        // ── Footer: Cancel / Apply ─────────────────────────────────────────────
        Rectangle {
            width:  parent.width
            height: 52
            color:  dlg.bgPanel

            // Top border
            Rectangle {
                anchors.top: parent.top
                width: parent.width; height: 1
                color: dlg.border
            }

            // Only round the bottom corners
            Rectangle {
                anchors.top: parent.top
                width:  parent.width
                height: parent.height / 2
                color:  parent.color
            }

            RowLayout {
                anchors.fill:         parent
                anchors.leftMargin:   18
                anchors.rightMargin:  18
                anchors.topMargin:    10
                anchors.bottomMargin: 10
                spacing: 10

                // Info label — shows how many channels are enabled
                Label {
                    text: {
                        var n = 0
                        for (var i = 0; i < 4; i++)
                            if (dlg.channels[i].chEnabled) n++
                        return n === 0 ? "No channels enabled"
                             : n === 1 ? "1 channel configured"
                             : n + " channels configured"
                    }
                    color:          dlg.txtMute
                    font.pixelSize: 11
                }

                Item { Layout.fillWidth: true }

                // Cancel
                Rectangle {
                    implicitWidth:  88
                    implicitHeight: 32
                    radius:         8
                    color:          cancelMouse.pressed
                                    ? Qt.darker(dlg.bgCard, 1.15)
                                    : (cancelMouse.containsMouse ? dlg.bgCard : "transparent")
                    border.color: dlg.border
                    border.width: 1

                    Label {
                        anchors.centerIn: parent
                        text:           "Cancel"
                        color:          dlg.txtMain
                        font.pixelSize: 12
                    }

                    MouseArea {
                        id:           cancelMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape:  Qt.PointingHandCursor
                        onClicked:    dlg.close()
                    }
                }

                // Apply & Close
                Rectangle {
                    implicitWidth:  130
                    implicitHeight: 32
                    radius:         8
                    color: {
                        if (applyMouse.pressed)  return Qt.darker("#1a6fa0", 1.2)
                        if (applyMouse.containsMouse) return "#1a6fa0"
                        return "#145580"
                    }
                    border.color: dlg.accent
                    border.width: 1

                    Label {
                        anchors.centerIn: parent
                        text:           "Apply & Close"
                        color:          "#e8f6ff"
                        font.pixelSize: 12
                        font.bold:      true
                    }

                    MouseArea {
                        id:           applyMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape:  Qt.PointingHandCursor
                        onClicked: {
                            AppController.applyChannelConfigs(dlg.buildConfigList())
                            dlg.close()
                        }
                    }
                }
            }
        }
    }

    // ─── Inner components ─────────────────────────────────────────────────────

    /**
     * ChannelSectionData — a lightweight "data model" object for one channel slot.
     *
     * WHY a QtObject: QML properties on a QtObject are true observable QML
     * properties.  When chEnabled changes, every control bound to it updates
     * automatically.  A plain JS object would require manual notify calls.
     */
    component ChannelSectionData: QtObject {
        property int    chIndex:      0
        property bool   chEnabled:    false
        property string chAlias:      "CH1"
        property int    chHwIndex:    -1      // index into AppController.channelList (-1 = none)
        property bool   chFdEnabled:  false
        property int    chBitrate:    500000
        property int    chDataBitrate:2000000
        property string chDbcPath:   ""
        property string chDbcInfo:   ""
    }

    /**
     * ChannelSection — the UI card for one channel slot.
     *
     * Displays all controls for a single channel config.
     * Grays out all controls when chData.chEnabled is false.
     */
    component ChannelSection: Rectangle {
        id: section

        // Data binding
        required property var chData
        required property var dlgRef    // reference back to dialog (for file picker)

        // Theme colours forwarded from dialog
        required property bool  isDark
        required property color bgCardColor
        required property color bgInputColor
        required property color borderColor
        required property color accentColor
        required property color txtMainColor
        required property color txtMuteColor
        required property color successColor
        required property color dangerColor

        // Channel colours (CH1=blue, CH2=orange, CH3=green, CH4=purple)
        readonly property color chColor: {
            var colors = ["#4da8ff", "#ff8c4d", "#4dff9a", "#c87aff"]
            return colors[chData.chIndex] || "#4da8ff"
        }

        // Enabled shorthand for opacity control
        readonly property bool isEnabled: chData.chEnabled

        height:  isEnabled ? enabledContent.implicitHeight + 48 : 42
        radius:  8
        color:   bgCardColor
        border.color: isEnabled ? chColor : borderColor
        border.width: isEnabled ? 1 : 1
        opacity: 1.0

        Behavior on height {
            NumberAnimation { duration: 180; easing.type: Easing.OutCubic }
        }
        Behavior on border.color {
            ColorAnimation { duration: 150 }
        }

        clip: true  // hides expanded content while animating collapse

        // ── Header row (always visible) ───────────────────────────────────────
        RowLayout {
            id: headerRow
            anchors.top:         parent.top
            anchors.left:        parent.left
            anchors.right:       parent.right
            anchors.topMargin:   12
            anchors.leftMargin:  14
            anchors.rightMargin: 14
            height:              22
            spacing:             10

            // Channel colour indicator strip
            Rectangle {
                width:  4; height: 18
                radius: 2
                color:  section.chColor
                opacity: section.isEnabled ? 1.0 : 0.4
            }

            // Enable toggle (custom checkbox)
            Rectangle {
                id:     enableBox
                width:  16; height: 16
                radius: 4
                color:  chData.chEnabled
                         ? Qt.rgba(section.chColor.r, section.chColor.g,
                                   section.chColor.b, isDark ? 0.3 : 0.15)
                         : (isDark ? "#0d1520" : "#f0f4fb")
                border.color: chData.chEnabled ? section.chColor : section.borderColor
                border.width: 1

                // Check mark
                Rectangle {
                    anchors.centerIn: parent
                    width: 6; height: 6; radius: 1
                    color:   section.chColor
                    visible: chData.chEnabled
                }

                MouseArea {
                    anchors.fill: parent
                    cursorShape:  Qt.PointingHandCursor
                    onClicked:    chData.chEnabled = !chData.chEnabled
                }
            }

            Label {
                text:           "Channel " + (chData.chIndex + 1)
                color:          section.isEnabled ? section.chColor : section.txtMuteColor
                font.pixelSize: 12
                font.bold:      section.isEnabled

                Behavior on color { ColorAnimation { duration: 150 } }
            }

            // Alias shown in collapsed state
            Label {
                visible:        !section.isEnabled
                text:           chData.chAlias
                color:          section.txtMuteColor
                font.pixelSize: 11
            }

            Item { Layout.fillWidth: true }

            // Status badge when enabled
            Rectangle {
                visible:        section.isEnabled
                implicitWidth:  statusBadgeLabel.implicitWidth + 14
                implicitHeight: 18
                radius:         9
                color: chData.chDbcInfo !== ""
                       ? Qt.rgba(section.successColor.r, section.successColor.g,
                                 section.successColor.b, 0.15)
                       : Qt.rgba(section.accentColor.r, section.accentColor.g,
                                 section.accentColor.b, 0.12)
                border.color: chData.chDbcInfo !== "" ? section.successColor : section.accentColor
                border.width: 1

                Label {
                    id:             statusBadgeLabel
                    anchors.centerIn: parent
                    text:           chData.chDbcInfo !== "" ? "DBC ✓" : "No DBC"
                    color:          chData.chDbcInfo !== "" ? section.successColor : section.accentColor
                    font.pixelSize: 9
                    font.bold:      true
                }
            }
        }

        // ── Expanded content (only when enabled) ──────────────────────────────
        Column {
            id: enabledContent
            anchors.top:         headerRow.bottom
            anchors.left:        parent.left
            anchors.right:       parent.right
            anchors.topMargin:   10
            anchors.leftMargin:  14
            anchors.rightMargin: 14
            anchors.bottomMargin:14
            spacing:             10
            visible:             section.isEnabled
            opacity:             section.isEnabled ? 1.0 : 0.0

            Behavior on opacity { NumberAnimation { duration: 160 } }

            // ── Row 1: Alias + HW Channel mapping ─────────────────────────────
            RowLayout {
                width: parent.width
                spacing: 10

                Column {
                    spacing: 4
                    Layout.preferredWidth: 180

                    Label {
                        text:           "Alias Name"
                        color:          section.txtMuteColor
                        font.pixelSize: 10
                        font.letterSpacing: 0.5
                    }

                    Rectangle {
                        width:  180; height: 28
                        radius: 6
                        color:  section.bgInputColor
                        border.color: aliasField.activeFocus ? section.accentColor : section.borderColor
                        border.width: 1

                        TextInput {
                            id:           aliasField
                            anchors.fill: parent
                            anchors.leftMargin:  8
                            anchors.rightMargin: 8
                            anchors.verticalCenter: parent.verticalCenter
                            verticalAlignment: TextInput.AlignVCenter
                            text:         chData.chAlias
                            color:        section.txtMainColor
                            font.pixelSize: 12
                            font.family:  "Consolas"
                            selectByMouse: true
                            clip:         true
                            onTextChanged: chData.chAlias = text
                        }
                    }
                }

                Column {
                    spacing: 4
                    Layout.fillWidth: true

                    Label {
                        text:           "Hardware Port"
                        color:          section.txtMuteColor
                        font.pixelSize: 10
                        font.letterSpacing: 0.5
                    }

                    // HW channel selection dropdown
                    Rectangle {
                        width:  parent.width; height: 28
                        radius: 6
                        color:  section.bgInputColor
                        border.color: hwCombo.activeFocus ? section.accentColor : section.borderColor
                        border.width: 1

                        ComboBox {
                            id:      hwCombo
                            anchors.fill: parent

                            // "Auto (Demo)" at index 0, then real HW channels
                            model: {
                                var list = ["Auto / Demo"]
                                var hw = AppController.channelList
                                for (var i = 0; i < hw.length; i++)
                                    list.push(hw[i])
                                return list
                            }

                            // Map hwChannelIndex (-1 = auto) to combo index (0 = auto)
                            currentIndex: chData.chHwIndex < 0 ? 0 : chData.chHwIndex + 1

                            onCurrentIndexChanged: {
                                chData.chHwIndex = (currentIndex === 0) ? -1 : currentIndex - 1
                            }

                            background: Rectangle {
                                color:        "transparent"
                                border.width: 0
                            }

                            contentItem: Label {
                                leftPadding:  8
                                text:         hwCombo.displayText
                                color:        section.txtMainColor
                                font.pixelSize: 11
                                elide:        Text.ElideRight
                                verticalAlignment: Text.AlignVCenter
                            }

                            popup: Popup {
                                y:            hwCombo.height + 2
                                width:        hwCombo.width
                                padding:      4
                                background: Rectangle {
                                    color:        section.isDark ? "#1a2333" : "#f0f5fc"
                                    border.color: section.borderColor
                                    border.width: 1
                                    radius:       6
                                }
                                contentItem: ListView {
                                    implicitHeight: contentHeight
                                    model: hwCombo.popup.visible ? hwCombo.delegateModel : null
                                    clip:  true
                                    delegate: ItemDelegate {
                                        width: ListView.view.width
                                        highlighted: hwCombo.highlightedIndex === index
                                        contentItem: Label {
                                            text:         modelData
                                            color:        highlighted ? section.accentColor : section.txtMainColor
                                            font.pixelSize: 11
                                            elide:        Text.ElideRight
                                            verticalAlignment: Text.AlignVCenter
                                        }
                                        background: Rectangle {
                                            radius: 4
                                            color:  parent.highlighted
                                                    ? Qt.rgba(section.accentColor.r, section.accentColor.g,
                                                              section.accentColor.b, 0.15)
                                                    : "transparent"
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // ── Row 2: Mode (CAN HS / CAN FD) ────────────────────────────────
            Column {
                width: parent.width
                spacing: 4

                Label {
                    text:           "Bus Mode"
                    color:          section.txtMuteColor
                    font.pixelSize: 10
                    font.letterSpacing: 0.5
                }

                RowLayout {
                    spacing: 8

                    // CAN HS radio button
                    Rectangle {
                        id:     hsBtn
                        width:  100; height: 28
                        radius: 6
                        color: !chData.chFdEnabled
                               ? Qt.rgba(section.accentColor.r, section.accentColor.g,
                                         section.accentColor.b, section.isDark ? 0.25 : 0.15)
                               : (hsMouse.containsMouse ? section.bgInputColor : "transparent")
                        border.color: !chData.chFdEnabled ? section.accentColor : section.borderColor
                        border.width: 1

                        RowLayout {
                            anchors.centerIn: parent
                            spacing: 5

                            // Radio indicator dot
                            Rectangle {
                                width: 10; height: 10; radius: 5
                                color: "transparent"
                                border.color: !chData.chFdEnabled ? section.accentColor : section.borderColor
                                border.width: 1.5

                                Rectangle {
                                    anchors.centerIn: parent
                                    width: 5; height: 5; radius: 2.5
                                    color:   section.accentColor
                                    visible: !chData.chFdEnabled
                                }
                            }

                            Label {
                                text:           "CAN HS"
                                color:          !chData.chFdEnabled ? section.accentColor : section.txtMuteColor
                                font.pixelSize: 11
                                font.bold:      !chData.chFdEnabled
                            }
                        }

                        MouseArea {
                            id:           hsMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape:  Qt.PointingHandCursor
                            onClicked:    chData.chFdEnabled = false
                        }
                    }

                    // CAN FD radio button
                    Rectangle {
                        id:     fdBtn
                        width:  100; height: 28
                        radius: 6
                        color: chData.chFdEnabled
                               ? Qt.rgba(section.accentColor.r, section.accentColor.g,
                                         section.accentColor.b, section.isDark ? 0.25 : 0.15)
                               : (fdMouse.containsMouse ? section.bgInputColor : "transparent")
                        border.color: chData.chFdEnabled ? section.accentColor : section.borderColor
                        border.width: 1

                        RowLayout {
                            anchors.centerIn: parent
                            spacing: 5

                            Rectangle {
                                width: 10; height: 10; radius: 5
                                color: "transparent"
                                border.color: chData.chFdEnabled ? section.accentColor : section.borderColor
                                border.width: 1.5

                                Rectangle {
                                    anchors.centerIn: parent
                                    width: 5; height: 5; radius: 2.5
                                    color:   section.accentColor
                                    visible: chData.chFdEnabled
                                }
                            }

                            Label {
                                text:           "CAN FD"
                                color:          chData.chFdEnabled ? section.accentColor : section.txtMuteColor
                                font.pixelSize: 11
                                font.bold:      chData.chFdEnabled
                            }
                        }

                        MouseArea {
                            id:           fdMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape:  Qt.PointingHandCursor
                            onClicked:    chData.chFdEnabled = true
                        }
                    }

                    // FD badge hint
                    Label {
                        visible:        chData.chFdEnabled
                        text:           "ISO 11898-7"
                        color:          section.txtMuteColor
                        font.pixelSize: 10
                        font.italic:    true
                    }
                }
            }

            // ── Row 3: Nominal Bitrate + Data Bitrate (FD only) ───────────────
            RowLayout {
                width:   parent.width
                spacing: 10

                // Nominal bitrate
                Column {
                    spacing: 4
                    Layout.preferredWidth: 200

                    Label {
                        text:           "Nominal Bitrate"
                        color:          section.txtMuteColor
                        font.pixelSize: 10
                        font.letterSpacing: 0.5
                    }

                    Rectangle {
                        width:  200; height: 28
                        radius: 6
                        color:  section.bgInputColor
                        border.color: brCombo.activeFocus ? section.accentColor : section.borderColor
                        border.width: 1

                        ComboBox {
                            id: brCombo
                            anchors.fill: parent

                            // Standard CAN nominal bitrates
                            property var bitrateValues:  [125000, 250000, 500000, 800000, 1000000]
                            property var bitrateLabels:  ["125 kbit/s", "250 kbit/s", "500 kbit/s (default)", "800 kbit/s", "1 Mbit/s"]

                            model: bitrateLabels

                            currentIndex: {
                                var idx = bitrateValues.indexOf(chData.chBitrate)
                                return idx < 0 ? 2 : idx   // default to 500k (index 2)
                            }

                            onCurrentIndexChanged: {
                                if (currentIndex >= 0 && currentIndex < bitrateValues.length)
                                    chData.chBitrate = bitrateValues[currentIndex]
                            }

                            background: Rectangle { color: "transparent"; border.width: 0 }

                            contentItem: Label {
                                leftPadding: 8
                                text:        brCombo.displayText
                                color:       section.txtMainColor
                                font.pixelSize: 11
                                verticalAlignment: Text.AlignVCenter
                                elide: Text.ElideRight
                            }

                            popup: Popup {
                                y: brCombo.height + 2
                                width: brCombo.width
                                padding: 4
                                background: Rectangle {
                                    color: section.isDark ? "#1a2333" : "#f0f5fc"
                                    border.color: section.borderColor; border.width: 1; radius: 6
                                }
                                contentItem: ListView {
                                    implicitHeight: contentHeight
                                    model: brCombo.popup.visible ? brCombo.delegateModel : null
                                    clip: true
                                    delegate: ItemDelegate {
                                        width: ListView.view.width
                                        highlighted: brCombo.highlightedIndex === index
                                        contentItem: Label {
                                            text: modelData; color: highlighted ? section.accentColor : section.txtMainColor
                                            font.pixelSize: 11; verticalAlignment: Text.AlignVCenter
                                        }
                                        background: Rectangle {
                                            radius: 4
                                            color: parent.highlighted
                                                   ? Qt.rgba(section.accentColor.r, section.accentColor.g, section.accentColor.b, 0.15)
                                                   : "transparent"
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                // Data bitrate (FD only)
                Column {
                    spacing: 4
                    Layout.fillWidth: true
                    opacity: chData.chFdEnabled ? 1.0 : 0.35

                    Behavior on opacity { NumberAnimation { duration: 150 } }

                    Label {
                        text:           "FD Data Bitrate"
                        color:          section.txtMuteColor
                        font.pixelSize: 10
                        font.letterSpacing: 0.5
                    }

                    Rectangle {
                        width:  parent.width; height: 28
                        radius: 6
                        color:  section.bgInputColor
                        border.color: dbrCombo.activeFocus && chData.chFdEnabled
                                      ? section.accentColor : section.borderColor
                        border.width: 1

                        ComboBox {
                            id: dbrCombo
                            anchors.fill: parent
                            enabled: chData.chFdEnabled

                            property var dbrValues: [1000000, 2000000, 4000000, 8000000]
                            property var dbrLabels: ["1 Mbit/s", "2 Mbit/s (default)", "4 Mbit/s", "8 Mbit/s"]

                            model: dbrLabels

                            currentIndex: {
                                var idx = dbrValues.indexOf(chData.chDataBitrate)
                                return idx < 0 ? 1 : idx   // default to 2M (index 1)
                            }

                            onCurrentIndexChanged: {
                                if (chData.chFdEnabled && currentIndex >= 0 && currentIndex < dbrValues.length)
                                    chData.chDataBitrate = dbrValues[currentIndex]
                            }

                            background: Rectangle { color: "transparent"; border.width: 0 }

                            contentItem: Label {
                                leftPadding: 8
                                text:        dbrCombo.displayText
                                color:       chData.chFdEnabled ? section.txtMainColor : section.txtMuteColor
                                font.pixelSize: 11
                                verticalAlignment: Text.AlignVCenter
                                elide: Text.ElideRight
                            }

                            popup: Popup {
                                y: dbrCombo.height + 2
                                width: dbrCombo.width
                                padding: 4
                                background: Rectangle {
                                    color: section.isDark ? "#1a2333" : "#f0f5fc"
                                    border.color: section.borderColor; border.width: 1; radius: 6
                                }
                                contentItem: ListView {
                                    implicitHeight: contentHeight
                                    model: dbrCombo.popup.visible ? dbrCombo.delegateModel : null
                                    clip: true
                                    delegate: ItemDelegate {
                                        width: ListView.view.width
                                        highlighted: dbrCombo.highlightedIndex === index
                                        contentItem: Label {
                                            text: modelData; color: highlighted ? section.accentColor : section.txtMainColor
                                            font.pixelSize: 11; verticalAlignment: Text.AlignVCenter
                                        }
                                        background: Rectangle {
                                            radius: 4
                                            color: parent.highlighted
                                                   ? Qt.rgba(section.accentColor.r, section.accentColor.g, section.accentColor.b, 0.15)
                                                   : "transparent"
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // ── Row 4: DBC File ───────────────────────────────────────────────
            Column {
                width: parent.width
                spacing: 4

                Label {
                    text:           "DBC Decode File"
                    color:          section.txtMuteColor
                    font.pixelSize: 10
                    font.letterSpacing: 0.5
                }

                RowLayout {
                    width: parent.width
                    spacing: 6

                    // DBC info / path display
                    Rectangle {
                        Layout.fillWidth: true
                        height:           28
                        radius:           6
                        color:            section.bgInputColor
                        border.color:     chData.chDbcInfo !== "" ? section.successColor : section.borderColor
                        border.width:     1

                        Label {
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.left:           parent.left
                            anchors.right:          parent.right
                            anchors.leftMargin:     8
                            anchors.rightMargin:    8
                            text:           chData.chDbcInfo !== ""
                                            ? chData.chDbcInfo
                                            : (chData.chDbcPath !== "" ? chData.chDbcPath : "No DBC loaded")
                            color:          chData.chDbcInfo !== ""
                                            ? section.successColor
                                            : section.txtMuteColor
                            font.pixelSize: 10
                            font.family:    chData.chDbcInfo !== "" ? "" : "Consolas"
                            elide:          Text.ElideMiddle
                        }
                    }

                    // Browse button
                    Rectangle {
                        implicitWidth:  76
                        implicitHeight: 28
                        radius:         6
                        color: browseMouse.pressed
                               ? Qt.darker(section.bgInputColor, 1.15)
                               : (browseMouse.containsMouse
                                  ? Qt.lighter(section.bgInputColor, 1.05)
                                  : section.bgInputColor)
                        border.color: browseMouse.containsMouse ? section.accentColor : section.borderColor
                        border.width: 1

                        Label {
                            anchors.centerIn: parent
                            text:           "Browse..."
                            color:          section.txtMainColor
                            font.pixelSize: 11
                        }

                        MouseArea {
                            id:           browseMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape:  Qt.PointingHandCursor
                            onClicked: dlgRef.openDbcPicker(chData.chIndex)
                        }
                    }

                    // Clear DBC button (only when a DBC is loaded)
                    Rectangle {
                        visible:        chData.chDbcPath !== ""
                        implicitWidth:  24
                        implicitHeight: 28
                        radius:         6
                        color:          clearDbcMouse.containsMouse
                                        ? Qt.rgba(section.dangerColor.r, section.dangerColor.g,
                                                  section.dangerColor.b, 0.15)
                                        : "transparent"
                        border.color:   clearDbcMouse.containsMouse ? section.dangerColor : section.borderColor
                        border.width:   1

                        Label {
                            anchors.centerIn: parent
                            text:             "✕"
                            color:            section.dangerColor
                            font.pixelSize:   11
                        }

                        MouseArea {
                            id:           clearDbcMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape:  Qt.PointingHandCursor
                            onClicked: {
                                chData.chDbcPath = ""
                                chData.chDbcInfo = ""
                            }
                        }
                    }
                }
            }

            // Bottom spacer inside expanded card
            Item { width: 1; height: 2 }
        }
    }
}
