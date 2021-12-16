import QtQuick 6.0
import QtQuick.Controls 6.0

Page {
    id: topLevel
    width: screenWidth
    height: screenHeight
    background: Rectangle{
        anchors.fill: parent
        gradient: Gradient{
            GradientStop { position: 0.0; color: "#464A4B" }
            GradientStop { position: 0.7; color: "#292C32" }
        }
    }

    property alias currentCardIndex: cardDropdown.currentIndex
    property alias currentProfileIndex: profileDropdown.currentIndex

    MouseArea{
        anchors.fill: parent
        onPressedChanged: {
            if (pressed) parent.focus = true
        }
    }

    HeaderText{
        id: sinkText
        text: "Default Output"
        y: 100

        anchors.horizontalCenter: parent.horizontalCenter
    }

    VRComboBox{
        id: sinkDropdown
        anchors.top: sinkText.bottom
        anchors.topMargin: 20
        anchors.horizontalCenter: sinkText.horizontalCenter

        width: 1300
        model: pulse.sinks
        property bool ready: false
        onCurrentIndexChanged: {
            // avoid changing sink on startup
            if (!ready){
                ready = true
                return
            }
            pulse.changeSink(currentIndex)
        }
        Component.onCompleted:{
            setIndex()
            pulse.newDefaultSink.connect(setIndex)
        }

        function setIndex(){
            currentIndex = pulse.sinkIndex
        }
    }

    VolumeSlider {
        id: slider
        width: sinkDropdown.width
        anchors.horizontalCenter: sinkDropdown.horizontalCenter
        anchors.top: sinkDropdown.bottom
        anchors.bottomMargin: 20

        from: 0
        to: 100

        Component.onCompleted: {
            pulse.newDefaultSink.connect(volUpdate)
            volUpdate()
        }
        onValueChanged: pulse.changeVol(value)
        function volUpdate(){
            value = pulse.getVolPct()
        }
    }

    Button{
	id: configButton
        anchors.top: slider.bottom
        anchors.topMargin: 20
        height: 50
        font.pointSize: 30
        anchors.horizontalCenter: slider.horizontalCenter
        text: "Save config"
	onClicked: {
	    configFeedback.visible = true
	    if (pulse.saveConfig())
		configFeedback.text = "Config saved successfully!"
	    else
		configFeedback.text = "Config was not saved due to an error!"
	    configFeedbackTimer.start()
	}
    }
    HeaderText{
	id: configFeedback
	property bool success
	anchors.top: configButton.bottom
	anchors.topMargin: 20
	anchors.horizontalCenter: parent.horizontalCenter
	Timer {
	    id: configFeedbackTimer
	    interval: 2000
	    onTriggered: parent.visible = false
	}
    }

    Rectangle{
        id: separator
        width: screenWidth - 40
        height: 5
        y: screenHeight/2 + 100
        anchors.horizontalCenter: parent.horizontalCenter
        color: "gray"
    }

    HeaderText{
        id: configText
        text: "Device Configuration"
        anchors.top: separator.bottom
        anchors.topMargin: 20
        anchors.horizontalCenter: separator.horizontalCenter
    }

    HeaderText{
        id: cardText
        text: "Device"
        anchors.horizontalCenter: cardDropdown.horizontalCenter
        anchors.top: configText.bottom
        anchors.topMargin: 100
    }

    VRComboBox{
        id: cardDropdown
        width: 1000

        anchors.top: cardText.bottom
        anchors.topMargin: 20
        anchors.left: parent.left
        anchors.leftMargin: 30
        model: {
            var l = []
            for (var i = 0; i<pulse.cards.length; i++){
                l.push(pulse.cards[i].description)
            }
            return l
        }
    }

    HeaderText{
        id: profileText
        text: "Active Profile"
        anchors.horizontalCenter: profileDropdown.horizontalCenter
        anchors.top: cardText.top
    }
    VRComboBox{
        id: profileDropdown
        y: cardDropdown.y
        width: 1000

        anchors.right: parent.right
        anchors.rightMargin: cardDropdown.anchors.leftMargin
        model: pulse.cards[currentCardIndex].profiles
        property bool userChange: false
        onCurrentIndexChanged: {
            if (userChange){
                var currentCard = pulse.cards[currentCardIndex]
                var currentProfile = currentCard.profiles[currentProfileIndex]
                pulse.changeCardProfile(currentCard, currentProfile)
            }
            userChange = false
        }
        onPressedChanged: { if (pressed) userChange = true }
        onModelChanged: {
            currentIndex = pulse.cards[currentCardIndex].activeProfileIndex
        }
    }

}




