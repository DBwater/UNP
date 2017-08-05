QT += core
QT -= gui

TARGET = threadpool
CONFIG += console
CONFIG -= app_bundle

TEMPLATE = app

SOURCES += main.cpp \
    http_conn.cpp

HEADERS += \
    locker.h \
    threadpool.h \
    http_conn.h

