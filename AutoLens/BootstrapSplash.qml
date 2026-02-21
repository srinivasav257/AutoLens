import QtQuick
import QtQuick.Window
import "qml/components"

Window {
    id: bootstrapSplash
    width: 520
    height: 340
    visible: false
    flags: Qt.Window | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint
    color: "#07090d"

    SplashScreen {
        anchors.fill: parent
        readyToDismiss: false
    }
}
