QT += core qml quick
CONFIG += plugin c++17
TEMPLATE = lib

CONFIG += link_pkgconfig
PKGCONFIG += libcamera

SOURCES += plugin.cpp

DESTDIR = LibCamera
TARGET  = qmllibcameraplugin