import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import QtQuick.Dialogs
import QtQuick.Window
import AutoLens
import "qml/components"

ApplicationWindow {
    id: root
    title: "AutoLens - Mini CANoe"
    width: 1280
    height: 760
    minimumWidth: 980
    minimumHeight: 620
    visible: true
    flags: Qt.Window | Qt.FramelessWindowHint
    property bool isDayTheme: true

    // -----------------------------------------------------------------------
    //  Normal (non-maximized) geometry tracking
    //
    //  WHY track separately: when the window is maximized on Windows,
    //  root.x / root.y become negative (e.g. -8,-8) and root.width /
    //  root.height reflect the entire screen.  Saving those values and
    //  restoring them next session would produce an off-screen window.
    //
    //  Solution: mirror x/y/width/height into normalX/Y/W/H but ONLY when
    //  the window is NOT maximized.  On close we pass the normal values to
    //  saveWindowState() along with a separate "was maximized" flag.
    // -----------------------------------------------------------------------
    property int normalX: 100
    property int normalY: 100
    property int normalW: 1280
    property int normalH: 760

    onXChanged:      if (visibility !== Window.Maximized && visibility !== Window.FullScreen) normalX = x
    onYChanged:      if (visibility !== Window.Maximized && visibility !== Window.FullScreen) normalY = y
    onWidthChanged:  if (visibility !== Window.Maximized && visibility !== Window.FullScreen) normalW = width
    onHeightChanged: if (visibility !== Window.Maximized && visibility !== Window.FullScreen) normalH = height

    // -----------------------------------------------------------------------
    //  Restore saved geometry and theme on first paint.
    //
    //  WHY Component.onCompleted (not onVisibleChanged):
    //  At onCompleted the QML tree is fully built and property bindings are
    //  stable.  Setting width/height here correctly resizes the window before
    //  the first frame is painted so there is no visual "jump".
    // -----------------------------------------------------------------------
    Component.onCompleted: {
        // --- Restore theme (before window shows to avoid flash) ---
        root.isDayTheme = AppController.loadTheme()

        // --- Restore window geometry ---
        var state = AppController.loadWindowState()
        if (state.hasGeometry) {
            root.x      = state.x
            root.y      = state.y
            root.width  = state.w
            root.height = state.h
            // Update our shadow copies so they don't get clobbered
            normalX = state.x;  normalY = state.y
            normalW = state.w;  normalH = state.h
        }
        // Apply maximized state last (after geometry, so normal size is known)
        if (state.maximized)
            root.showMaximized()
    }

    // -----------------------------------------------------------------------
    //  Save geometry on close.
    //
    //  WHY onClosing and not onDestruction:
    //  onDestruction fires during QML engine teardown when object lifetimes
    //  are unstable.  onClosing fires while the window is still alive and
    //  AppController is still accessible.
    // -----------------------------------------------------------------------
    onClosing: (close) => {
        AppController.saveWindowState(normalX, normalY, normalW, normalH,
                                      root.visibility === Window.Maximized)
    }

    // -----------------------------------------------------------------------
    //  Auto-save theme whenever it changes (toggle button or startup restore).
    //
    //  WHY use onIsDayThemeChanged instead of putting saveTheme() in the
    //  MouseArea onClicked: this handler catches ALL sources of theme changes
    //  (user click, programmatic set in Component.onCompleted, future code)
    //  from a single central location.
    // -----------------------------------------------------------------------
    onIsDayThemeChanged: AppController.saveTheme(isDayTheme)

    Material.theme: root.isDayTheme ? Material.Light : Material.Dark
    Material.accent: Material.Cyan
    Material.primary: Material.Grey

    readonly property color windowBg: root.isDayTheme ? "#eef3f9" : "#07090d"
    readonly property color windowBgAlt: root.isDayTheme ? "#fbfdff" : "#0a0d12"
    readonly property color panelBg: root.isDayTheme ? "#dde6f2" : "#10151c"
    readonly property color panelRaised: root.isDayTheme ? "#e8eef6" : "#141a23"
    readonly property color pageBg: root.isDayTheme ? "#f4f8fd" : "#0d1118"
    readonly property color controlBg: root.isDayTheme ? "#ffffff" : "#1a212c"
    readonly property color controlHover: root.isDayTheme ? "#edf3fb" : "#222c39"
    readonly property color controlDisabled: root.isDayTheme ? "#d7e0eb" : "#181c24"
    readonly property color border: root.isDayTheme ? "#b8c8db" : "#263242"
    readonly property color accent: "#35b8ff"
    readonly property color accentSoft: root.isDayTheme ? "#1d87cb" : "#89dbff"
    readonly property color success: root.isDayTheme ? "#1f8e57" : "#59d892"
    readonly property color danger: "#ff6d86"
    readonly property color textMain: root.isDayTheme ? "#1d2b3b" : "#e8eef8"
    readonly property color textMuted: root.isDayTheme ? "#5a6f86" : "#91a4c3"
    readonly property color titleGlyph: root.isDayTheme ? "#3b4f66" : "#b7c8da"
    readonly property color titleButtonHover: root.isDayTheme ? "#d1dceb" : "#1f2b3d"
    readonly property color titleButtonPress: root.isDayTheme ? "#bfcee0" : "#273448"
    readonly property color navActiveBg: root.isDayTheme ? "#dce9f9" : "#1b2735"
    readonly property color navHoverBg: root.isDayTheme ? "#e8f0fa" : "#131c27"
    readonly property color navHoverBorder: root.isDayTheme ? "#b7cce4" : "#2a3c52"
    readonly property color navHoverText: root.isDayTheme ? "#4f6882" : "#9fb6cf"
    readonly property color navHoverShortText: root.isDayTheme ? "#48617b" : "#b9cce2"
    readonly property color popupHighlightBg: root.isDayTheme ? "#dce9f9" : "#1f334b"
    readonly property color toastBg: root.isDayTheme ? "#fde8ed" : "#5b2533"
    readonly property color toastText: root.isDayTheme ? "#89263d" : "#ffe4ea"

    color: windowBg

    background: Rectangle {
        gradient: Gradient {
            GradientStop { position: 0.0; color: root.windowBgAlt }
            GradientStop { position: 1.0; color: root.windowBg }
        }
    }

    function toggleMaximized() {
        if (root.visibility === Window.Maximized)
            root.showNormal()
        else
            root.showMaximized()
    }

    header: Column {
        width: root.width
        spacing: 0

        Rectangle {
            width: parent.width
            height: 40
            color: root.panelBg
            border.color: root.border
            border.width: 0

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 8
                spacing: 8

                Label {
                    text: "AUTOLENS"
                    color: root.accent
                    font.pixelSize: 14
                    font.bold: true
                    font.letterSpacing: 1.2
                }

                Label {
                    text: "CAN ANALYZER"
                    color: root.textMuted
                    font.pixelSize: 10
                    font.letterSpacing: 1.4
                }

                Item {
                    Layout.fillWidth: true
                    Layout.fillHeight: true

                    DragHandler {
                        target: null
                        onActiveChanged: if (active) root.startSystemMove()
                    }

                    TapHandler {
                        acceptedButtons: Qt.LeftButton
                        gesturePolicy: TapHandler.ReleaseWithinBounds
                        onDoubleTapped: root.toggleMaximized()
                    }
                }

                // ── Day / Night theme toggle ──────────────────────────────────
                //
                // Design rationale: the sun/moon icon lives INSIDE the sliding
                // knob rather than being centered on the track.  This way the
                // symbol always travels with the knob, making the control feel
                // cohesive instead of two independent elements.
                //
                // Layout (52 × 26 track, 20 × 20 knob, 3 px padding each side):
                //   Day  : knob at x=3  (left)  → sun  icon in knob
                //   Night: knob at x=29 (right) → moon icon in knob
                //
                // Track tint reinforces the current mode visually so the toggle
                // reads correctly at a glance without the tooltip.
                Rectangle {
                    id: titleThemeToggle
                    implicitWidth:  52
                    implicitHeight: 26
                    radius: height / 2

                    // Warm amber in day, cool navy in night — matches theme feel
                    color: root.isDayTheme ? "#fff8e6" : "#0f1a30"
                    border.color: root.isDayTheme ? "#e8aa20" : "#2e4a88"
                    border.width: 1

                    ToolTip.visible: titleThemeMouse.containsMouse
                    ToolTip.delay:   300
                    ToolTip.text:    root.isDayTheme
                                     ? "Switch to night theme"
                                     : "Switch to day theme"

                    // ── Sliding knob ─────────────────────────────────────────
                    Rectangle {
                        id: titleThemeKnob
                        width:  20
                        height: 20
                        radius: 10
                        anchors.verticalCenter: parent.verticalCenter

                        // 3 px padding from either edge; Behavior animates the slide
                        x: root.isDayTheme ? 3 : (titleThemeToggle.width - width - 3)

                        // Knob surface: white so the coloured icon pops on both themes
                        color: "#ffffff"
                        border.color: root.isDayTheme ? "#d4a010" : "#3a62cc"
                        border.width: 1

                        // Drop shadow effect via a slightly larger semi-transparent rect
                        Rectangle {
                            anchors.centerIn: parent
                            width:  parent.width  + 2
                            height: parent.height + 2
                            radius: (parent.width + 2) / 2
                            color:  "transparent"
                            border.color: root.isDayTheme
                                          ? "#00000018"
                                          : "#00000030"
                            border.width: 2
                            z: -1
                        }

                        // ── Icon canvas inside the knob ──────────────────────
                        //
                        // WHY Canvas: QML has no built-in sun/moon primitives.
                        // Canvas gives pixel-accurate 2-D drawing with antialiasing.
                        // The same canvas renders EITHER the sun OR the moon
                        // depending on isDayTheme — no need for two separate items.
                        Canvas {
                            id: knobIcon
                            anchors.fill: parent
                            antialiasing: true

                            Component.onCompleted: requestPaint()

                            onPaint: {
                                var ctx = getContext("2d")
                                ctx.clearRect(0, 0, width, height)
                                var cx = width  / 2   // knob centre x
                                var cy = height / 2   // knob centre y

                                if (root.isDayTheme) {
                                    // ── Sun ──────────────────────────────────
                                    // 8 short rays radiating from a central disk
                                    ctx.strokeStyle = "#c88800"
                                    ctx.lineWidth   = 1.4
                                    ctx.lineCap     = "round"
                                    for (var i = 0; i < 8; ++i) {
                                        var a = i * Math.PI / 4
                                        ctx.beginPath()
                                        ctx.moveTo(cx + Math.cos(a) * 5.5,
                                                   cy + Math.sin(a) * 5.5)
                                        ctx.lineTo(cx + Math.cos(a) * 7.5,
                                                   cy + Math.sin(a) * 7.5)
                                        ctx.stroke()
                                    }
                                    // Central disk (outline only, no fill, so
                                    // the white knob background shows through)
                                    ctx.beginPath()
                                    ctx.arc(cx, cy, 3.5, 0, Math.PI * 2)
                                    ctx.stroke()

                                } else {
                                    // ── Moon crescent ────────────────────────
                                    // Draw a filled blue circle, then punch out
                                    // a slightly offset circle using destination-out
                                    // compositing — this is the standard crescent trick.
                                    ctx.fillStyle = "#4a7aff"
                                    ctx.beginPath()
                                    ctx.arc(cx, cy, 6.0, 0, Math.PI * 2)
                                    ctx.fill()

                                    // Punch-out circle shifted right + slightly up
                                    ctx.globalCompositeOperation = "destination-out"
                                    ctx.beginPath()
                                    ctx.arc(cx + 3.2, cy - 1.2, 5.2, 0, Math.PI * 2)
                                    ctx.fill()
                                    ctx.globalCompositeOperation = "source-over"
                                }
                            }

                            // Repaint whenever the theme switches
                            Connections {
                                target: root
                                function onIsDayThemeChanged() { knobIcon.requestPaint() }
                            }
                        }

                        Behavior on x {
                            NumberAnimation {
                                duration:    180
                                easing.type: Easing.OutCubic
                            }
                        }
                    }

                    MouseArea {
                        id:           titleThemeMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape:  Qt.PointingHandCursor
                        onClicked:    root.isDayTheme = !root.isDayTheme
                    }
                }

                Rectangle {
                    id: minButton
                    implicitWidth: 34
                    implicitHeight: 24
                    radius: height / 2
                    color: minMouse.pressed
                           ? root.titleButtonPress
                           : (minMouse.containsMouse ? root.titleButtonHover : "transparent")
                    border.color: minMouse.containsMouse ? root.border : "transparent"
                    border.width: minMouse.containsMouse ? 1 : 0

                    Rectangle {
                        anchors.centerIn: parent
                        width: 10
                        height: 2
                        radius: 1
                        color: root.titleGlyph
                    }

                    MouseArea {
                        id: minMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.showMinimized()
                    }
                }

                Rectangle {
                    id: maxButton
                    implicitWidth: 34
                    implicitHeight: 24
                    radius: height / 2
                    color: maxMouse.pressed
                           ? root.titleButtonPress
                           : (maxMouse.containsMouse ? root.titleButtonHover : "transparent")
                    border.color: maxMouse.containsMouse ? root.border : "transparent"
                    border.width: maxMouse.containsMouse ? 1 : 0

                    Rectangle {
                        visible: root.visibility !== Window.Maximized
                        anchors.centerIn: parent
                        width: 10
                        height: 8
                        radius: 1
                        color: "transparent"
                        border.color: root.titleGlyph
                        border.width: 1
                    }

                    Rectangle {
                        visible: root.visibility === Window.Maximized
                        anchors.horizontalCenter: parent.horizontalCenter
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.horizontalCenterOffset: 1
                        anchors.verticalCenterOffset: -1
                        width: 8
                        height: 6
                        color: "transparent"
                        border.color: root.titleGlyph
                        border.width: 1
                    }

                    Rectangle {
                        visible: root.visibility === Window.Maximized
                        anchors.horizontalCenter: parent.horizontalCenter
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.horizontalCenterOffset: -1
                        anchors.verticalCenterOffset: 1
                        width: 8
                        height: 6
                        color: "transparent"
                        border.color: root.titleGlyph
                        border.width: 1
                    }

                    MouseArea {
                        id: maxMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.toggleMaximized()
                    }
                }

                Rectangle {
                    id: closeButton
                    implicitWidth: 34
                    implicitHeight: 24
                    radius: height / 2
                    color: closeMouse.pressed
                           ? "#c13e60"
                           : (closeMouse.containsMouse ? "#a83553" : "transparent")
                    border.color: closeMouse.containsMouse ? "#cf5f7d" : "transparent"
                    border.width: closeMouse.containsMouse ? 1 : 0

                    Rectangle {
                        anchors.centerIn: parent
                        width: 11
                        height: 2
                        radius: 1
                        rotation: 45
                        // White on red hover/press; dark glyph colour when idle in light mode
                        color: closeMouse.containsMouse ? "#ffffff" : root.titleGlyph
                    }
                    Rectangle {
                        anchors.centerIn: parent
                        width: 11
                        height: 2
                        radius: 1
                        rotation: -45
                        color: closeMouse.containsMouse ? "#ffffff" : root.titleGlyph
                    }

                    MouseArea {
                        id: closeMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.close()
                    }
                }
            }
        }

        ToolBar {
            width: parent.width
            height: 52

            background: Rectangle {
                color: root.panelRaised
                border.color: root.border
                border.width: 0
            }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 12
                spacing: 8

                // ── CAN Config button ─────────────────────────────────────────
                //
                // Opens the per-channel config dialog (alias, HW port,
                // HS/FD mode, bitrate, DBC file) for up to 4 channels.
                // This replaces the old Session dropdown + Load DBC button.

                Rectangle {
                    id: canConfigBtn
                    implicitWidth:  110
                    implicitHeight: 34
                    radius:         8
                    color: cfgMouse.pressed
                           ? Qt.darker(root.controlBg, 1.12)
                           : (cfgMouse.containsMouse ? root.controlHover : root.controlBg)
                    border.color: cfgMouse.containsMouse ? root.accent : root.border
                    border.width: 1

                    ToolTip.text:    "Configure CAN channels, bitrates and DBC files"
                    ToolTip.visible: cfgMouse.containsMouse
                    ToolTip.delay:   400

                    RowLayout {
                        anchors.centerIn: parent
                        spacing: 6

                        // Gear icon (canvas-drawn)
                        Canvas {
                            width: 14; height: 14
                            antialiasing: true
                            onPaint: {
                                var ctx = getContext("2d")
                                ctx.clearRect(0, 0, width, height)
                                ctx.strokeStyle = root.accent
                                ctx.lineWidth   = 1.4
                                ctx.lineCap     = "round"
                                var cx = 7, cy = 7, r = 4.2
                                for (var i = 0; i < 6; ++i) {
                                    var a = i * Math.PI / 3
                                    ctx.beginPath()
                                    ctx.moveTo(cx + Math.cos(a)*r, cy + Math.sin(a)*r)
                                    ctx.lineTo(cx + Math.cos(a)*(r+2), cy + Math.sin(a)*(r+2))
                                    ctx.stroke()
                                }
                                ctx.beginPath()
                                ctx.arc(cx, cy, r, 0, Math.PI*2)
                                ctx.stroke()
                                ctx.beginPath()
                                ctx.arc(cx, cy, 2, 0, Math.PI*2)
                                ctx.stroke()
                            }
                            Component.onCompleted: requestPaint()
                            Connections {
                                target: root
                                function onIsDayThemeChanged() { parent.requestPaint() }
                            }
                        }

                        Label {
                            text:           "CAN Config"
                            color:          root.textMain
                            font.pixelSize: 12
                            font.bold:      true
                        }
                    }

                    MouseArea {
                        id:           cfgMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape:  Qt.PointingHandCursor
                        onClicked:    canConfigDialog.open()
                    }
                }

                // Thin separator
                Rectangle {
                    width: 1; height: 28
                    color: root.border
                    opacity: 0.7
                }

                // ── Connect / Disconnect button ───────────────────────────────
                //
                // Connects to the CAN hardware port (goes on-bus).
                // Separate from Start — you can be connected without measuring.

                Rectangle {
                    id:             connectBtn
                    implicitWidth:  116
                    implicitHeight: 34
                    radius:         8
                    color: AppController.connected
                           ? (root.isDayTheme ? "#ffdce5" : "#40242d")
                           : (root.isDayTheme ? "#ddf5e7" : "#1f342b")
                    border.color: AppController.connected ? root.danger : root.success
                    border.width: 1

                    ToolTip.text:    AppController.connected
                                     ? "Disconnect from CAN bus"
                                     : "Connect to CAN bus (configure first with CAN Config)"
                    ToolTip.visible: connMouse.containsMouse
                    ToolTip.delay:   400

                    RowLayout {
                        anchors.centerIn: parent
                        spacing: 5

                        // Status dot (green when connected, red when not)
                        Rectangle {
                            width:  7; height: 7; radius: 4
                            color: AppController.connected ? root.success : root.danger

                            SequentialAnimation on opacity {
                                running:  AppController.connected
                                loops:    Animation.Infinite
                                NumberAnimation { to: 0.3; duration: 800 }
                                NumberAnimation { to: 1.0; duration: 800 }
                            }
                        }

                        Label {
                            text: AppController.connected ? "Disconnect" : "Connect"
                            color: AppController.connected
                                   ? (root.isDayTheme ? "#7d2b42" : "#ffd7df")
                                   : (root.isDayTheme ? "#1c6d43" : "#d7ffe9")
                            font.pixelSize: 12
                            font.bold:      true
                        }
                    }

                    MouseArea {
                        id:           connMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape:  Qt.PointingHandCursor
                        onClicked:    AppController.connectChannels()
                    }
                }

                // ── DBC status indicator (read-only, informational) ───────────
                Rectangle {
                    visible:        AppController.dbcLoaded
                    implicitWidth:  dbcInfoLabel.implicitWidth + 20
                    implicitHeight: 26
                    radius:         6
                    color:          root.isDayTheme ? "#ddf4ef" : "#1d2f31"
                    border.color:   root.isDayTheme ? "#2e9a86" : "#59d8c0"
                    border.width:   1

                    ToolTip.text:    AppController.dbcInfo
                    ToolTip.visible: dbcBadgeMouse.containsMouse
                    ToolTip.delay:   300

                    Label {
                        id:             dbcInfoLabel
                        anchors.centerIn: parent
                        text:           "DBC ✓"
                        color:          root.isDayTheme ? "#236e61" : "#94ffea"
                        font.pixelSize: 11
                        font.bold:      true
                    }

                    MouseArea {
                        id:           dbcBadgeMouse
                        anchors.fill: parent
                        hoverEnabled: true
                    }
                }

                Item { Layout.fillWidth: true }

                // ── Live stats (fps + frame count) ────────────────────────────
                Label {
                    visible:        AppController.measuring
                    text:           AppController.frameRate + " fps"
                    color:          AppController.frameRate > 1000 ? root.accent : root.success
                    font.pixelSize: 11
                    font.family:    "Consolas"
                }

                Label {
                    visible:        AppController.measuring
                    text:           AppController.frameCount + " frames"
                    color:          root.textMuted
                    font.pixelSize: 11
                    font.family:    "Consolas"
                }

                Rectangle {
                    width: 1; height: 24
                    color: root.border
                    opacity: 0.8
                }

                Label {
                    text:              AppController.statusText
                    color:             root.textMuted
                    font.pixelSize:    11
                    elide:             Text.ElideRight
                    Layout.maximumWidth: 380
                }
            }
        }
    }

    RowLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 12

        Rectangle {
            Layout.fillHeight: true
            width: 102
            radius: 12
            color: root.panelBg
            border.color: root.border
            border.width: 0

            Column {
                anchors.fill: parent
                anchors.margins: 10
                spacing: 8

                Repeater {
                    model: [
                        { shortName: "TR", label: "Trace" },
                        { shortName: "GN", label: "Generator" },
                        { shortName: "SM", label: "Simulation" },
                        { shortName: "DG", label: "Diagnostics" }
                    ]

                    delegate: Rectangle {
                        id: navItem
                        width: parent.width
                        height: 72
                        radius: 10
                        property bool active: stack.currentIndex === index
                        property bool hovered: navMouse.containsMouse
                        color: active ? root.navActiveBg : (hovered ? root.navHoverBg : "transparent")
                        border.color: active ? root.accent : (hovered ? root.navHoverBorder : "transparent")
                        border.width: active ? 1 : (hovered ? 1 : 0)
                        scale: hovered && !active ? 1.02 : 1.0

                        Behavior on color {
                            ColorAnimation { duration: 120 }
                        }
                        Behavior on scale {
                            NumberAnimation { duration: 120 }
                        }

                        Rectangle {
                            anchors.left: parent.left
                            anchors.verticalCenter: parent.verticalCenter
                            width: 2
                            height: parent.height * 0.55
                            radius: 1
                            color: root.accent
                            opacity: navItem.active ? 0.95 : (navItem.hovered ? 0.45 : 0.0)

                            Behavior on opacity {
                                NumberAnimation { duration: 120 }
                            }
                        }

                        Column {
                            anchors.centerIn: parent
                            spacing: 4

                            Label {
                                anchors.horizontalCenter: parent.horizontalCenter
                                text: modelData.shortName
                                color: navItem.active
                                       ? root.accentSoft
                                       : (navItem.hovered ? root.navHoverShortText : root.textMuted)
                                font.pixelSize: 13
                                font.bold: true
                                font.letterSpacing: 1.1
                            }

                            Label {
                                anchors.horizontalCenter: parent.horizontalCenter
                                text: modelData.label
                                color: navItem.active
                                       ? root.textMain
                                       : (navItem.hovered ? root.navHoverText : root.textMuted)
                                font.pixelSize: 10
                            }
                        }

                        MouseArea {
                            id: navMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: stack.currentIndex = index
                        }
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: 12
            color: root.panelRaised
            border.color: root.border
            border.width: 0

            StackLayout {
                id: stack
                anchors.fill: parent
                anchors.margins: 10
                currentIndex: 0

                TracePage { }
                GeneratorPage { }
                SimulationPage { }
                DiagnosticsPage { }
            }
        }
    }

    // ── CAN Config dialog ─────────────────────────────────────────────────────
    //
    // Modal popup for per-channel CAN configuration.
    // Opened by the CAN Config button in the toolbar.
    // Defined in qml/components/CANConfigDialog.qml.

    CANConfigDialog {
        id:        canConfigDialog
        appWindow: root
    }

    Connections {
        target: AppController
        function onErrorOccurred(message) {
            errorToast.showMessage(message)
        }
    }

    Rectangle {
        id: errorToast
        anchors.bottom: parent.bottom
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottomMargin: 22
        width: Math.max(errorLabel.implicitWidth + 28, 220)
        height: 36
        radius: 8
        color: root.toastBg
        border.color: root.danger
        border.width: 1
        visible: false
        z: 100

        Label {
            id: errorLabel
            anchors.centerIn: parent
            color: root.toastText
            font.pixelSize: 12
            elide: Text.ElideRight
            width: parent.width - 20
            horizontalAlignment: Text.AlignHCenter
        }

        Timer {
            id: toastTimer
            interval: 3800
            onTriggered: errorToast.visible = false
        }

        function showMessage(msg) {
            errorLabel.text = msg
            visible = true
            toastTimer.restart()
        }
    }
}
