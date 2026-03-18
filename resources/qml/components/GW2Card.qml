import QtQuick 2.15

Rectangle {
    id: root
    
    property string title: ""
    property alias content: contentArea.children
    
    color: "#252525"
    border.color: "#333333"
    border.width: 1
    radius: 12
    
    Column {
        anchors.fill: parent
        anchors.margins: 15
        spacing: 10
        
        Text {
            visible: root.title !== ""
            text: root.title
            font.pixelSize: 16
            font.bold: true
            color: "#c09c57"
        }
        
        Item {
            id: contentArea
            width: parent.width
            height: parent.height - (root.title !== "" ? 30 : 0)
        }
    }
}
