import QtQuick 2.15
import LibCamera 1.0
import QtQuick.Window 2.15
import QtQuick.Controls 2.15

Rectangle {
    id: root
    width: 1920
    height: 1080
    color: "black"

    function snap() {
        anim.start()
        cam.saveToFile()
    }

    MouseArea {
        anchors.fill: parent
        enabled: false
        onClicked: {
            var orientation = cam.rotation
            orientation += 45
            if (orientation == 360)
                orientation = 0
            cam.rotation = orientation
        }
    }

    Rectangle {
        id: captureAnimation
        anchors.centerIn: parent
        width: radius
        height: width
        property real initialRadius: Math.sqrt(root.width*root.width + root.height*root.height)
        radius: initialRadius
        border.width: 1
        border.color: "red"
	clip: true

        SequentialAnimation {
            id: anim
            NumberAnimation { target: captureAnimation; property: "radius"; from: captureAnimation.initialRadius; to: 0; duration: 400 }
            NumberAnimation { target: captureAnimation; property: "radius"; from: 0; to: captureAnimation.initialRadius; duration: 400 }
            onFinished: {
                captureImg.visible = true
            }
        }

	focus: true
	Keys.onPressed: {
		if (event.key == Qt.Key_Return) {
            root.snap();
		}
		else if (event.key == Qt.Key_Escape) {
			console.log("exiting app")
			Qt.quit();
		}
	}
	LibCamera {
           id: cam
           width: 1920
           height: 1080
           anchors.centerIn: parent
    	}
    }

    Button {
        id: captureImg
        width: 200
	height: 60
        background: Rectangle {
            radius: width/2
            color: "green"
        }
	contentItem: Text {
		text:" Hit me to capture image !"
		font.pointSize: 12
		wrapMode: Text.WordWrap
		horizontalAlignment: Text.AlignHCenter
		verticalAlignment: Text.AlignVCenter
	}
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 20
        anchors.horizontalCenter: parent.horizontalCenter
        onClicked: {
            root.snap()
            captureAnimation.focus = true
        }
    }
}
