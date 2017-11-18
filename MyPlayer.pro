HEADERS += \
    surface.h \
    reader.h

SOURCES += \
    surface.cpp \
    reader.cpp

QT += gui core widgets multimedia

#这里是ffmpeg编译过程中需要的库文件
linux{
LIBS +=  -pthread -lavdevice -lavfilter -lswscale -lpostproc -lavformat -lavcodec \
-lxcb-xfixes -lxcb-render -lxcb-shape   -lxcb -lXext -lXv -lX11 -lasound \
 -lx264 -lpthread -ldl -lfaac -lz -lswresample -lavutil -lm
}
