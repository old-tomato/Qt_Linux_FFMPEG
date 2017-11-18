#include "reader.h"

Reader::Reader(QString filename , QObject *parent) : QObject(parent)
{
    this->filename = filename;
    flag = true;
    pthread_create(&tid , NULL , thread_fun , this);
}

void * Reader::thread_fun(void * arg)
{
    Reader * reader = (Reader *)arg;
    reader->read();
    reader->freeData();
}

void Reader::read()
{
    // 打开文件，同时获得里面的
    if(!openFile())
        return;

    // 获得流中的信息，这里是通过获得流以后判断流的类型，是音频，还是视频
    if(!getStreamInfo())
        return;

    // 获得编解码器
    if(!getDecode())
        return;

    // 初始化和声音相关的内容
    if(!initOutputAudioInfo())
        return;

    // 初始化 SWS 和 SWR
    if(!initSwsAndSwr())
        return;

//     初始化图片，包和帧s
    if(!initImageAndPktAndFrame())
        return;

    // 开始正式读取文件
    while(flag)
    {
        // 通过文件读取每一帧，存在到一个包中
        // 在AVPacket中保存了每一帧的数据（AVBufferRef *buf;）
        /**
         * Return the next frame of a stream.
         * This function returns what is stored in the file, and does not validate
         * that what is there are valid frames for the decoder. It will split what is
         * stored in the file into frames and return one for each call. It will not
         * omit invalid data between valid frames so as to give the decoder the maximum
         * information possible for decoding.
         *
         * If pkt->buf is NULL, then the packet is valid until the next
         * av_read_frame() or until avformat_close_input(). Otherwise the packet
         * is valid indefinitely. In both cases the packet must be freed with
         * av_packet_unref when it is no longer needed. For video, the packet contains
         * exactly one frame. For audio, it contains an integer number of frames if each
         * frame has a known fixed size (e.g. PCM or ADPCM data). If the audio frames
         * have a variable size (e.g. MPEG audio), then it contains one frame.
         *
         * pkt->pts, pkt->dts and pkt->duration are always set to correct
         * values in AVStream.time_base units (and guessed if the format cannot
         * provide them). pkt->pts can be AV_NOPTS_VALUE if the video format
         * has B-frames, so it is better to rely on pkt->dts if you do not
         * decompress the payload.
         *
         * @return 0 if OK, < 0 on error or end of file
         */
        int ret = av_read_frame(inputCtx , pkt);
        if(ret < 0)
        {
            qDebug() << "at end or error" << ret;
            break;
        }

        // 这个包的音频编号如果是之前设定的声音编号
        if(pkt->stream_index == audioStreamIndex)
        {
            // handler audio
            handlerAudio();
        }

        // 这个包的视频编号如果是之前设定的视频编号
        else if(pkt->stream_index == videoStreamIndex)
        {
            // handler video
            handlerVideo();
        }
    }
}

void Reader::handlerAudio()
{
    qDebug() << "handlerAudio";
    // 解码包中的数据
    avcodec_send_packet(audioCodecCtx , pkt);
    // 释放包
    av_packet_unref(pkt);
    // 从编解码器中获得帧
    avcodec_receive_frame(audioCodecCtx , frame);

    // frame data might be muilt sound track
    /** Convert audio. 转变音频
     *
     *
     * in and in_count can be set to 0 to flush the last few samples out at the
     * end.
     *
     * If more input is provided than output space, then the input will be buffered.
     * You can avoid this buffering by using swr_get_out_samples() to retrieve an
     * upper bound on the required number of output samples for the given number of
     * input samples. Conversion will run directly without copying whenever possible.
     *
     * @param s         allocated Swr context, with parameters set
     * @param out       output buffers, only the first one need be set in case of packed audio
     * @param out_count amount of space available for output in samples per channel
     * @param in        input buffers, only the first one need to be set in case of packed audio
     * @param in_count  number of input samples available in one channel
     *
     * @return number of samples output per channel, negative value on error
     */
    int ret = swr_convert(swr , // 之前设定好的转换主体
                          out , // 输入的缓冲区，只需要设定第一个用来处理音频包---需要一个足够大的空间来放音频的数据，本应用中，使用了固定但是足够大的大小
                          frame->nb_samples ,// 输出的每个频道的采样
                          (const uint8_t **)frame->data ,// 当前这一帧里面获得数据
                          frame->nb_samples);// 每一个频道中可用的输入采样
    // 往音频输出文件中写入长度为（每个频道的输出采样 * 输出的通道数量 * 每个采样的大小（这个是位） / 8（转成Kb））
    do_write( ret * audioFormat.channelCount() * audioFormat.sampleSize() / 8);
}

void Reader::do_write(int len)
{
    int ret = 0;
    int alreadyWrite = 0;
    while(alreadyWrite < len)
    {
        // 往音频文件中写数据
        // 可能返回0，表示当前无法写入，所以需要等待一段时间
        // 大于0表示写入成功，返回写入的数量
        // 小于0表示输错了
        ret = ioDevice->write((char *)(out[0]) + alreadyWrite , len - alreadyWrite);
        if(ret > 0)
        {
            alreadyWrite += ret;
        }
        else if(ret == 0)
        {
            QThread::msleep(20);
        }
        else if(ret < 0)
        {
            qDebug() << "write error end";
            break;
        }
    }
}

void Reader::handlerVideo()
{
    //==========================================
    QThread::msleep(20);
    avcodec_send_packet(videoCodecCtx , pkt);
    av_packet_unref(pkt);
    avcodec_receive_frame(videoCodecCtx , frame);
    //==============音频处理中内容相同====================

    // 将每一帧的画面，转换成一个QImage
    /**
     * Scale the image slice in srcSlice and put the resulting scaled
     * slice in the image in dst. A slice is a sequence of consecutive
     * rows in an image.
     *
     * Slices have to be provided in sequential order, either in
     * top-bottom or bottom-top order. If slices are provided in
     * non-sequential order the behavior of the function is undefined.
     *
     * @param c         the scaling context previously created with
     *                  sws_getContext()
     * @param srcSlice  the array containing the pointers to the planes of
     *                  the source slice
     * @param srcStride the array containing the strides for each plane of
     *                  the source image
     * @param srcSliceY the position in the source image of the slice to
     *                  process, that is the number (counted starting from
     *                  zero) in the image of the first row of the slice
     * @param srcSliceH the height of the source slice, that is the number
     *                  of rows in the slice
     * @param dst       the array containing the pointers to the planes of
     *                  the destination image
     * @param dstStride the array containing the strides for each plane of
     *                  the destination image
     * @return          the height of the output slice
     */
    sws_scale(sws , // 图形缩放的主体
              frame->data ,// 每一帧的数据
              frame->linesize , // 每一帧的行的数量
              0 ,// 每一帧从第几行开始读取
              videoCodecCtx->height ,// 每一帧需要读取的高度（读取几行）
              dst ,//每一帧需要写入的数据---就是之前写入到QImage中的内容
              dstStride);// 每一个帧的宽度
    emit this->onUpdateImage(image);// 通知界面进行更新操作
}


bool Reader::initImageAndPktAndFrame()
{
    // 初始化想要在界面中显示的图片，这里设定的格式是RGBA8888，与之前设定sws需要相对应
    image = QImage(videoCodecCtx->width , videoCodecCtx->height , QImage::Format_RGBA8888);
    // 图片数据的指针初始化
    dst[0] = image.bits();
    // 图片每行的宽度
    dstStride[0] = 4 * image.width();

    // 初始化一个包
    pkt = av_packet_alloc();
    // 初始化一个帧
    frame = av_frame_alloc();
    return true;
}

bool Reader::initSwsAndSwr()
{
    // AVSampleFormat 表示当前输出声音的采样格式，但是之前设定的是QT下的输出采样格式，所以需要进行转化成ffmpeg所认识的格式
    AVSampleFormat sampFmt;
    if(audioFormat.sampleSize() == 8 && audioFormat.sampleType() == QAudioFormat::UnSignedInt)
        sampFmt = AV_SAMPLE_FMT_U8;
    else if(audioFormat.sampleSize() == 16 && audioFormat.sampleType() == QAudioFormat::SignedInt)
        sampFmt = AV_SAMPLE_FMT_S16;
    else if(audioFormat.sampleSize() == 32 && audioFormat.sampleType() == QAudioFormat::SignedInt)
        sampFmt = AV_SAMPLE_FMT_S32;
    else if(audioFormat.sampleSize() == 32 && audioFormat.sampleType() == QAudioFormat::Float)
        sampFmt = AV_SAMPLE_FMT_FLT;
    else
    {
        qDebug() << "audio format swr init error";
        return false;
    }

    // 艹
    /**
     * 创建一个SwrContext如果需要设定/重新设定常见参数
     * Allocate SwrContext if needed and set/reset common parameters.
     *
     * 这个方法可以直接创建SwrContext而不需要调用swr_alloc()进行创建，
     * 另一方面，swr_alloc()这个方法可以使用swr_alloc_set_opts()进行设定参数
     * This function does not require s to be allocated with swr_alloc(). On the
     * other hand, swr_alloc() can use swr_alloc_set_opts() to set the parameters
     * on the allocated context.
     *
     * @param s               existing Swr context if available, or NULL if not
     * @param out_ch_layout   output channel layout (AV_CH_LAYOUT_*)
     * @param out_sample_fmt  output sample format (AV_SAMPLE_FMT_*).
     * @param out_sample_rate output sample rate (frequency in Hz)
     * @param in_ch_layout    input channel layout (AV_CH_LAYOUT_*)
     * @param in_sample_fmt   input sample format (AV_SAMPLE_FMT_*).
     * @param in_sample_rate  input sample rate (frequency in Hz)
     * @param log_offset      logging level offset
     * @param log_ctx         parent logging context, can be NULL
     *
     * @see swr_init(), swr_free()
     * @return NULL on error, allocated context otherwise
     */
    swr = swr_alloc_set_opts(NULL , // 可以是已经存在的主体，也可以为空（系统自动分配）
                     /**
                      * 通过给定的通道数量返回一个默认的通道布局，这里的参数是输出的通道数量
                      * Return default channel layout for a given number of channels.
                      */
                       av_get_default_channel_layout(audioFormat.channelCount()) ,// 输出的 通道布局（啥东西？）
                       sampFmt ,// ffmpeg认识的输出格式（上面进行了转换）
                       audioFormat.sampleRate() , // 输出的采样率（之前设定过了）
                       audioCodecCtx->channel_layout ,// 输入的通道布局
                       audioCodecCtx->sample_fmt , // 输入的采样格式
                       audioCodecCtx->sample_rate ,// 输入的采样率
                       0 ,// 日志等级偏移（啥？）
                       NULL);// 不知道

    // 在设定完成参数以后，通过这个方法进行初始化（必须调用）
     swr_init(swr);

     /**
      * Check if context can be reused, otherwise reallocate a new one.
      *
      * If context is NULL, just calls sws_getContext() to get a new
      * context. Otherwise, checks if the parameters are the ones already
      * saved in context. If that is the case, returns the current
      * context. Otherwise, frees context and gets a new context with
      * the new parameters.
      *
      * Be warned that srcFilter and dstFilter are not checked, they
      * are assumed to remain the same.
      */
    sws = sws_getCachedContext(NULL ,
                               videoCodecCtx->width ,// 输入的宽度
                               videoCodecCtx->height ,// 输入的宽度
                               videoCodecCtx->pix_fmt ,// 输入的格式
                               videoCodecCtx->width , // 输出的宽度（不需要进行更改）
                               videoCodecCtx->height ,// 输出的高度
                               AV_PIX_FMT_RGBA ,// 输出的格式（进来的格式由别人确定，但是这里需要输出的RGBA的格式，输入的可能是yuv）
                               SWS_BICUBIC ,// 一种图片转换的算法，据说这个性能和速度都还可以（不懂）
                               NULL ,// 输入过滤器
                               NULL ,// 输出过滤器
                               NULL);// 参数 const double *param 不知道是什么


    return true;
}

/**
 * 初始化和输出音频相关的信息
 * @brief Reader::initOutputAudioInfo
 * @return
 */
bool Reader::initOutputAudioInfo()
{
    // 设定是大端对齐，还是小段对其
    audioFormat.setByteOrder(QAudioFormat::LittleEndian);
    // 设定声音的频道数量，单声道？双声道？5.1？7.1？
    // 这个方法中的参数通过之前获得编解码器主体中的频道数量进行设定
    audioFormat.setChannelCount(audioCodecCtx->channels);
    // 设定输出音频的格式，这里使用的是原始的码流（据说这里只能够这么写）
    audioFormat.setCodec("audio/pcm");
    // 设定输出音频的采样率，这里不对采样率做其他修改，直接使用原始音频的采样率
    audioFormat.setSampleRate(audioCodecCtx->sample_rate);
    // 设定输出音频的每个采样的大小（我们希望的值）
    audioFormat.setSampleSize(32);
    // 设定输出音频的采样格式（我们希望的）
    audioFormat.setSampleType(QAudioFormat::Float);

    // QT框架提供的获得默认音频输出设备的方法
    deviceInfo = QAudioDeviceInfo::defaultOutputDevice();
    // 我们之前希望的格式信息是否能够和系统信息相匹配
    if(!deviceInfo.isFormatSupported(audioFormat))
    {
        // 如果不匹配，让系统设定一个和我们设定最相近的格式信息
        deviceInfo.nearestFormat(audioFormat);
    }

    // 通过我们设定的格式，打开一个音频文件
    // 注：这个方法有两种用法
    // 1. 返回一个音频文件（输出的虚拟文件--声卡），之后直接将数据想写文件一样，写入到声卡中
    // 2. 直接读取输入的音频文件
    output = new QAudioOutput(audioFormat);
    // 开始播放，但是在这个例子里面，之后再进行写入，但是提前开始是没有关系的，因为没有数据写入
    ioDevice = output->start();

    return true;
}

/**
 * @attention 该方法需要在调用了getStreamInfo（）之后使用，因为里面最重要的audioStream 和 videoStream两个变量需要在之前初始化
 *
 * @brief Reader::getDecode
 * @return
 */
bool Reader::getDecode()
{
    int ret = 0;
    // 通过音频流中的codecpar（编码参数）中的编码ID找到合适的编解码器
    audioCodec = avcodec_find_decoder(audioStream->codecpar->codec_id);


    /**
     * 创建一个AVCodecContext，并且在里面设置默认的值，这个结果结构体需要被avcodec_free_context进行释放
     * Allocate an AVCodecContext and set its fields to default values. The
     * resulting struct should be freed with avcodec_free_context().
     *
     *
     * 如果codec不会空，分配一个私有数据，并且通过给定的数据进行初始化，
     * 如果在调用该函数之后再通过另外一个编解码器调用avcode_open2是非法的
     * 如果参数为空，里面的默认参数就不会被初始化，
     * @param codec if non-NULL, allocate private data and initialize defaults
     *              for the given codec. It is illegal to then call avcodec_open2()
     *              with a different codec.
     *              If NULL, then the codec-specific defaults won't be initialized,
     *              which may result in suboptimal default settings (this is
     *              important mainly for encoders, e.g. libx264).
     *
     * 如果失败，就返回一个空
     * @return An AVCodecContext filled with default values or NULL on failure.
     */
    audioCodecCtx = avcodec_alloc_context3(audioCodec);

    /**
     * 通过提供的编解码参数填充这个编解码器的主体，（后面的翻译困哪···）
     * Fill the codec context based on the values from the supplied codec
     * parameters. Any allocated fields in codec that have a corresponding field in
     * par are freed and replaced with duplicates of the corresponding field in par.
     * Fields in codec that do not have a counterpart in par are not touched.
     *
     * @return >= 0 on success, a negative AVERROR code on failure.
     */
    ret = avcodec_parameters_to_context(audioCodecCtx , audioStream->codecpar);
    if(ret != 0)
    {
        qDebug() << "audio parm to error";
        return false;
    }

    /**
     * 通过给定的编解码器（AVCodec）初始化这个编解码器主体(AVCodecContext)，在调用这个方法之前
     * 需要调用avcodec_alloc_context3()《为了分配空间》
     * Initialize the AVCodecContext to use the given AVCodec. Prior to using this
     * function the context has to be allocated with avcodec_alloc_context3().
     *
     *
     * 使用下面列举的几个方法是一个简单的方式去返回一个编解码器
     * The functions avcodec_find_decoder_by_name(), avcodec_find_encoder_by_name(),
     * avcodec_find_decoder() and avcodec_find_encoder() provide an easy way for
     * retrieving a codec.
     *
     * 这个方法不会线程安全的
     * @warning This function is not thread safe!
     *
     * @note Always call this function before using decoding routines (such as
     * @ref avcodec_receive_frame()).
     *
     * @code
     * avcodec_register_all();
     * av_dict_set(&opts, "b", "2.5M", 0);
     * codec = avcodec_find_decoder(AV_CODEC_ID_H264);
     * if (!codec)
     *     exit(1);
     *
     * context = avcodec_alloc_context3(codec);
     *
     * if (avcodec_open2(context, codec, opts) < 0)
     *     exit(1);
     * @endcode
     *
     * @param avctx The context to initialize.
     * @param codec The codec to open this context for. If a non-NULL codec has been
     *              previously passed to avcodec_alloc_context3() or
     *              for this context, then this parameter MUST be either NULL or
     *              equal to the previously passed codec.
     * @param options A dictionary filled with AVCodecContext and codec-private options.
     *                On return this object will be filled with options that were not found.
     *
     * @return zero on success, a negative value on error
     * @see avcodec_alloc_context3(), avcodec_find_decoder(), avcodec_find_encoder(),
     *      av_dict_set(), av_opt_find().
     */
    ret = avcodec_open2(audioCodecCtx , audioCodec , NULL);
    if(ret != 0)
    {
        qDebug() << "open audio codec error";
        return false;
    }

    videoCodec = avcodec_find_decoder(videoStream->codecpar->codec_id);
    videoCodecCtx = avcodec_alloc_context3(videoCodec);
    ret = avcodec_parameters_to_context(videoCodecCtx , videoStream->codecpar);
    if(ret != 0)
    {
        qDebug() << "video parm to error";
        return false;
    }

    ret = avcodec_open2(videoCodecCtx , videoCodec , NULL);
    if(ret != 0)
    {
        qDebug() << "open video codec error";
        return false;
    }

    return true;
}

bool Reader::getStreamInfo()
{
    /**
     * 读取媒体文件包去获得流的信息，这是很有用的去获得那些没有头的文件格式比如MPEG
     * 这个方法同样会去计算真正的参数比方说MPEG-2
     *
     * Read packets of a media file to get stream information. This
     * is useful for file formats with no headers such as MPEG. This
     * function also computes the real framerate in case of MPEG-2 repeat
     * frame mode.
     * The logical file position is not changed by this function;
     * examined packets may be buffered for later processing.
     * @brief ret
     */
    int ret = avformat_find_stream_info(inputCtx , NULL);
    if(ret != 0)
    {
        qDebug() << "get stream info error";
        return false;
    }
    /**
      * 获得文件中的流的数量
      */
    for(int x = 0; x < inputCtx->nb_streams ; ++ x)
    {
        AVStream * stream = inputCtx->streams[x];
        // 如果是音频流，就记录下来
        if(stream->codecpar->codec_type == AVMEDIA_TYPE_AUDIO)
        {
            audioStream = stream;
            audioStreamIndex = x;
        }
        // 如果是视频流，就记录下来
        else if(stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
        {
            videoStream = stream;
            videoStreamIndex = x;
        }
        // TODO 这个方法存在问题，所有的流遍历以后，只会记录下流号最小的音频和视频流，但是这里不做其他的处理了
    }

    // 初始化的时候，都设定为-1，如果两个在遍历结束以后，都是-1就意味着没有获得任何的流的信息，说明读取流信息失败
    if(audioStreamIndex == -1 && videoStreamIndex == -1)
    {
        qDebug() << "get stream index error" << audioStreamIndex << "  " << videoStreamIndex;
        return false;
    }
    qDebug() << "audioStreamIndex  " << audioStreamIndex << "  videoStreamIndex  " << videoStreamIndex;
    return true;
}

bool Reader::openFile()
{
    /**
     * 打开一个输入流并且读取文件头信息
     * 但是编解码器并会被打开，在调用该方法之前，需要保证流是关闭的，或者是通过avformat_close_input进行调用
     * 第一个参数如果为空，将有函数分配空间
     * 第二个参数：文件的路径，但是需要从QString 转成 char*
     * 第三个参数：如果不为空，强制使用这个输入format，如果为空，就自动判断
     * 第四个参数：一个demux（解码器《？》）的配置参数，可以为空（该参数不是很清楚）
     * @brief ret
     */
    int ret = avformat_open_input(&inputCtx , filename.toLocal8Bit().data() , NULL , NULL);
    if(ret != 0)
    {
        return false;
    }
    return true;
}

void Reader::freeData()
{

}
