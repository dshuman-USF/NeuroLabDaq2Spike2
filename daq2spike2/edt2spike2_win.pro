#-------------------------------------------------

TARGET = edt2spike2
TEMPLATE = app

win32 {
   CONFIG -= debug_and_release
   CONFIG += warn_off
   CONFIG += console
   CONFIG +=c++17
   DEFINES -= UNICODE
   DEFINES -= _UNICODE
   QT -= gui

   SOURCES += edt2spike2.cpp

   DEFINES += S64_NOTDLL
   CONFIG -= debug
   QMAKE_CXXFLAGS += -D__USE_MINGW_ANSI_STDIO -D_GNU_SOURCE -static
   QMAKE_LFLAGS += -static
   OBJECTS_DIR = mswin
   LIBS += -lson64
   LIBS += -lwinpthread
   CONFIG -= debug
   MAKEFILE=Makefile_edt2spike2_win.qt
}

