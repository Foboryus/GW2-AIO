import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

ApplicationWindow {
    id: root
    visible: true
    width: 900
    height: 600
    title: "GW2 AIO Manager"
    color: "#1a1a1a"
    
    // GW2 Color Palette
    readonly property color bgDark: "#1a1a1a"
    readonly property color bgPanel: "#252525"
    readonly property color accent: "#c09c57"
    readonly property color accentHover: "#d4b06b"
    readonly property color textPrimary: "#e0e0e0"
    readonly property color textSecondary: "#888888"
    
    RowLayout {
        anchors.fill: parent
        spacing: 0
        
        // Navigation sidebar
        Rectangle {
            Layout.fillHeight: true
            Layout.preferredWidth: 200
            color: bgPanel
            
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 20
                spacing: 10
                
                // Title
                Text {
                    text: "GW2 AIO"
                    font.pixelSize: 24
                    font.bold: true
                    color: accent
                    Layout.alignment: Qt.AlignHCenter
                }
                
                Item { Layout.preferredHeight: 30 }
                
                // Nav buttons
                Repeater {
                    model: [
                        { icon: "qrc:/icons/home.svg", text: "Dashboard", page: 0 },
                        { icon: "qrc:/icons/box.svg", text: "Addons", page: 1 },
                        { icon: "qrc:/icons/play.svg", text: "Launcher", page: 2 },
                        { icon: "qrc:/icons/grid.svg", text: "Modules", page: 3 }
                    ]
                    
                    delegate: Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 45
                        radius: 8
                        color: mouseArea.containsMouse ? "#333333" : "transparent"
                        
                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 15
                            spacing: 10
                            
                            Image {
                                source: modelData.icon
                                sourceSize.width: 18
                                sourceSize.height: 18
                                Layout.preferredWidth: 18
                                Layout.preferredHeight: 18
                            }
                            
                            Text {
                                text: modelData.text
                                font.pixelSize: 14
                                color: mouseArea.containsMouse ? textPrimary : textSecondary
                            }
                        }
                        
                        MouseArea {
                            id: mouseArea
                            anchors.fill: parent
                            hoverEnabled: true
                            onClicked: pageStack.currentIndex = modelData.page
                        }
                    }
                }
                
                Item { Layout.fillHeight: true }
            }
        }
        
        // Main content
        StackLayout {
            id: pageStack
            Layout.fillWidth: true
            Layout.fillHeight: true
            
            // Dashboard
            Item {
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 30
                    spacing: 20
                    
                    Text {
                        text: "Dashboard"
                        font.pixelSize: 28
                        font.bold: true
                        color: accent
                    }
                    
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 60
                        radius: 8
                        color: accent
                        
                        RowLayout {
                            anchors.centerIn: parent
                            spacing: 10
                            
                            Image {
                                source: "qrc:/icons/play-circle.svg"
                                sourceSize.width: 24
                                sourceSize.height: 24
                            }
                            
                            Text {
                                text: "Launch GW2"
                                font.pixelSize: 18
                                font.bold: true
                                color: bgDark
                            }
                        }
                        
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: console.log("Launch GW2")
                        }
                    }
                    
                    Item { Layout.fillHeight: true }
                }
            }
            
            // Addons placeholder
            Item {
                Text {
                    anchors.centerIn: parent
                    text: "Addons Page"
                    color: textSecondary
                    font.pixelSize: 20
                }
            }
            
            // Launcher placeholder  
            Item {
                Text {
                    anchors.centerIn: parent
                    text: "Launcher Page"
                    color: textSecondary
                    font.pixelSize: 20
                }
            }
            
            // Modules placeholder
            Item {
                Text {
                    anchors.centerIn: parent
                    text: "Blish Modules Page"
                    color: textSecondary
                    font.pixelSize: 20
                }
            }
        }
    }
}
