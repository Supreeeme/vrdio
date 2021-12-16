import QtQuick 6.0
import QtQuick.Controls 6.0

Slider {
    implicitHeight: handle.height + 20
    implicitWidth: 400

    background: Rectangle {
        x: parent.leftPadding
        y: parent.topPadding + parent.availableHeight / 2 - height / 2
        width: parent.availableWidth
        height: 10
        radius: 2
        color: "#bdbebf"

        Rectangle {
            width: parent.parent.visualPosition * parent.width
            height: parent.height
            color: "#21be2b"
            radius: 2
        }
    }

    handle: Rectangle { // actually a circle!
        x: parent.leftPadding + parent.visualPosition * (parent.availableWidth - width)
        y: parent.topPadding + parent.availableHeight / 2 - height / 2
        implicitWidth: 26
        implicitHeight: 26
        width: 80
        height: 80
        radius: 80
        color: parent.pressed ? "#f0f0f0" : "#f6f6f6"
        border.color: "#bdbebf"

        Text {
            anchors.centerIn: parent
            font.pointSize: 15
            font.family: parent.parent.font.family
            text: parseInt(parent.parent.value) + "%"
        }
    }
}

