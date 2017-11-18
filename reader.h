#ifndef READER_H
#define READER_H

#include <QObject>
#include <QDebug>
#include <QAudioDeviceInfo>
#include <QAudioFormat>
#include <QAudioOutput>
#include <QIODevice>
#include <QImage>
#include <pthread.h>
#include <QThread>
extern "C"
{

#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavdevice/avdevice.h>

}

class Reader : public QObject
{
    Q_OBJECT
public:
    explicit Reader(QString filename , QObject *parent = nullptr);

signals:
    void onUpdateImage(QImage image);
public slots:

private:
    QString filename;
    pthread_t tid;
    bool flag;

    /**
     * 文件信息的主题内容，里面存放了文件流，流的数量等多种内容
     * @brief inputCtx
     */
    AVFormatContext * inputCtx = NULL;

    // AVCodec 编解码器，可以同时用于解码或者是编码
    /**
     * 一个音频的编解码器
     * @brief audioCodec
     */
    AVCodec * audioCodec = NULL;
    AVCodec * videoCodec = NULL;
    // AVCodeContext ffmpeg中的主要API（但是具体有些什么并不清楚）
    AVCodecContext * audioCodecCtx = NULL;
    AVCodecContext * videoCodecCtx = NULL;

    int audioStreamIndex = -1;
    int videoStreamIndex = -1;
    AVStream * audioStream = NULL;
    AVStream * videoStream = NULL;

    // for video
    SwsContext * sws = NULL;

    /**
     * 这个是libswresample的主体，不像libavcodec和libavformat，这个结构体是不透明的，这意味着如果你想要对其进行设置，
     * 需要使用里面的提供的API方法
     * 这个结构体是一个用于进行输入和输出转换设定的类（不是很确定）
     * The libswresample context. Unlike libavcodec and libavformat, this structure
     * is opaque. This means that if you would like to set options, you must use
     * the @ref avoptions API and cannot directly set values to members of the
     * structure.
     */
    SwrContext * swr = NULL;

    AVPacket * pkt = NULL;
    AVFrame * frame = NULL;

    QAudioDeviceInfo deviceInfo;
    QAudioOutput * output = NULL;
    QIODevice * ioDevice = NULL;
    QAudioFormat audioFormat;

    QImage image;
    uint8_t* dst[1];
    int dstStride[1];
    uint8_t* outBuffer = new uint8_t[65536*4];
    uint8_t* out[1] = {outBuffer};

    static void * thread_fun(void * arg);
    void read();
    void freeData();
    bool openFile();
    bool getStreamInfo();
    bool getDecode();
    bool initOutputAudioInfo();
    bool initSwsAndSwr();
    bool initImageAndPktAndFrame();
    void handlerAudio();
    void handlerVideo();
    void do_write(int len);
};

#endif // READER_H
