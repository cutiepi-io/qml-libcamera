# qml-libcamera
An oversimplified libcamera QML plugin, so you can write a camera application using QtQuick in less than 30 LoC: 

```
import QtQuick 2.15
import LibCamera 1.0

Rectangle {
    width: 800
    height: 600

    LibCamera {
        id: cam
        onImageCaptured: pic.source = filename
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
```

## ToDo

- add config properties (currently fixed in `2592x1944` resolution) 
- utilize video stream and provide viewfinder in QAbstractVideoSurface 
- expose more methods and align properties to be like [Camera](https://doc.qt.io/qt-6/qml-qtmultimedia-camera.html) component

## Compile 

```
sudo apt install libcamera-dev
qmake && make 
qmlscene test.qml -I . 
```
