import QtQuick 2.15
import LibCamera 1.0

Rectangle {
    width: 800
    height: 600

    LibCamera {
        id: cam
        anchors.fill: parent
    }

}