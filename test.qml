import QtQuick 2.15
import LibCamera 1.0

Rectangle {
    width: 800
    height: 600

    LibCamera {
        id: cam
        anchors.fill: parent
    }
    MouseArea {
        anchors.fill: parent
        onClicked: {
            var orientation = cam.rotation
            orientation += 45
            if (orientation == 360)
                orientation = 0
            cam.rotation = orientation
        }
    }
}