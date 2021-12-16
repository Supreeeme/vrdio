import QtQuick 6.0
import QtQuick.Controls 6.0

ComboBox{
    FontLoader{
        id: localFont
        source: "qrc:/SourceSansPro-Regular.ttf"
    }

    id: dropdown
    implicitHeight: 60
    font.family: localFont.name
    font.pointSize: 33

    contentItem: Text{
        text: dropdown.displayText
        width: parent.width
        elide: Text.ElideRight
        font: dropdown.font
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter;
    }

    popup.contentItem: ListView{
        model: dropdown.popup.visible ? dropdown.delegateModel : null
        currentIndex: dropdown.currentIndex
        implicitHeight: contentHeight
        interactive: false
        maximumFlickVelocity: 0.1

        MouseArea{
            id: m
            anchors.fill: parent
            onWheel: (wheel)=>{
                if (wheel.angleDelta.y > 0)
                    scrollbar.increase()
                else
                    scrollbar.decrease()
            }
            acceptedButtons: Qt.NoButton
            propagateComposedEvents: true
        }
        ScrollBar.vertical: ScrollBar{
            id: scrollbar
            policy: ScrollBar.AlwaysOn
            visible: parent.height < parent.contentHeight
            width: 20
            stepSize: 0.05
        }
    }

    delegate: ItemDelegate{
        id: del
        width: dropdown.width
        text: modelData
        font.pointSize: 30
        font.bold: (dropdown.currentIndex == index)
        font.family: dropdown.font.family
        highlighted: (area.containsMouse)
        MouseArea{
            id: area
            acceptedButtons: Qt.NoButton
            anchors.fill: parent
            hoverEnabled: true
        }
    }


    background: Rectangle{
        color: dropdown.down ? "#e0e0e0" : "#d0d0d0"
        radius: 15
    }

}
