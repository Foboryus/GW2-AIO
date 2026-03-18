import QtQuick 2.15
import QtQuick.Controls 2.15

Button {
    id: root
    
    property bool primary: false
    
    implicitWidth: 120
    implicitHeight: 40
    
    background: Rectangle {
        radius: 8
        color: {
            if (root.primary) {
                return root.pressed ? "#a88847" : (root.hovered ? "#d4b06b" : "#c09c57")
            } else {
                return root.pressed ? "#3a3a3a" : (root.hovered ? "#404040" : "#333333")
            }
        }
        border.color: root.hovered ? "#c09c57" : "#444444"
        border.width: root.primary ? 0 : 1
    }
    
    contentItem: Text {
        text: root.text
        font.pixelSize: 14
        font.bold: root.primary
        color: root.primary ? "#1a1a1a" : "#e0e0e0"
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }
}
