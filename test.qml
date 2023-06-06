import QtQuick 2.15
import LibCamera 1.0

Rectangle {
    width: 800
    height: 600

    LibCamera { 
        id: cam
        onImageCaptured: { 
            pic.source = filename
            console.log('image captured: ' + filename)
        }
        orientation: 90
    }

    Image { 
        id: pic
        anchors.centerIn: parent
        anchors.fill: parent
        fillMode: Image.PreserveAspectFit
    }

    MouseArea { 
        anchors.fill: parent
        onClicked: cam.captureImage()
    }
}