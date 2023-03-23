TEMPLATE = lib
TARGET = mtgf
CONFIG -= qt
CONFIG += staticlib c++11

include(../../../../installerfw.pri)

DESTDIR = $$IFW_LIB_PATH

exists(mt-gpu-finder/mt_gpu_finder.pro) {
SOURCES += \

HEADERS += \
    mt-gpu-finder/mt_gpu_finder_global.h \
    mt-gpu-finder/mt_gpu_finder.h

linux {
SOURCES += mt-gpu-finder/mt_gpu_finder_linux.cpp
}

win32 {
SOURCES += mt-gpu-finder/mt_gpu_finder_win.cpp

LIBS += -lcfgmgr32 -lOneCoreUAP
}
}

target.path = $$[QT_INSTALL_LIBS]
INSTALLS += target
