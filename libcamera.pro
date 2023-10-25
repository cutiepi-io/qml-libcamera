QT += core qml quick
CONFIG += plugin c++17
TEMPLATE = lib

CONFIG += link_pkgconfig
PKGCONFIG += libcamera

SOURCES += plugin.cpp image.cpp frameconverter.cpp

DESTDIR = LibCamera
TARGET  = qmllibcameraplugin
