import QtQuick 2.15

Item {
    id: root
    
    property bool checked: false
    property string label: ""
    
    signal toggled(bool checked)
    
    implicitWidth: 200
    implicitHeight: 30
    
    Row {
        anchors.fill: parent
        spacing: 10
        
        // Toggle track
        Rectangle {
            width: 50
            height: 26
            radius: 13
            color: root.checked ? "#c09c57" : "#333333"
            border.color: root.checked ? "#d4b06b" : "#444444"
            
            Behavior on color { ColorAnimation { duration: 150 } }
            
            // Toggle knob
            Rectangle {
                id: knob
                width: 20
                height: 20
                radius: 10
                y: 3
                x: root.checked ? 27 : 3
                color: "#e0e0e0"
                
                Behavior on x { NumberAnimation { duration: 150 } }
            }
            
            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: {
                    root.checked = !root.checked
                    root.toggled(root.checked)
                }
            }
        }
        
        // Label
        Text {
            visible: root.label !== ""
            text: root.label
            font.pixelSize: 14
            color: "#e0e0e0"
            anchors.verticalCenter: parent.verticalCenter
        }
    }
}
