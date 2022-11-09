#define BUILDING_NODE_EXTENSION

#include <stdio.h>
#include <format>
#include <node.h>
#include <node_object_wrap.h>
#include <node_buffer.h>
#include <windows.h>

extern "C"
{
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/pixdesc.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
}

using namespace v8;


struct buffer_data {
    uint8_t* ptr;
    size_t size;
};

static int read_packet(void* opaque, uint8_t* buf, int buf_size)
{
    struct buffer_data* bd = (struct buffer_data*)opaque;
    buf_size = FFMIN(buf_size, bd->size);
    memcpy(buf, bd->ptr, buf_size);
    bd->ptr += buf_size;
    bd->size -= buf_size;
    return buf_size;
}

static void fill_yuv_image(uint8_t* data[4], int linesize[4],
    int width, int height, int frame_index)
{
    int x, y;

    /* Y */
    for (y = 0; y < height; y++)
        for (x = 0; x < width; x++)
            data[0][y * linesize[0] + x] = x + y + frame_index * 3;

    /* Cb and Cr */
    for (y = 0; y < height / 2; y++) {
        for (x = 0; x < width / 2; x++) {
            data[1][y * linesize[1] + x] = 128 + y + frame_index * 2;
            data[2][y * linesize[2] + x] = 64 + x + frame_index * 5;
        }
    }
}

/// <summary>
/// FFMPEG function for extracting video dimensions of a buffer
/// </summary>
/// <param name="isolate"></param>
/// <param name="buf"></param>
/// <param name="bufLen"></param>
/// <param name="width"></param>
/// <param name="height"></param>
/// <returns></returns>
int FFMPEGGetDimensions(Isolate* isolate, unsigned char* buf, int bufLen, int* width, int* height)
{
    AVFormatContext* pFormatCtx = NULL;
    AVIOContext* pAvioCtx = NULL;
    const AVCodecParameters* pCodecParams = NULL;
    int ret;
    int videoStream = -1;
    struct buffer_data bd = { 0 };
    uint8_t* avio_ctx_buffer = NULL;
    size_t avio_ctx_buffer_size = 4096;

    bd.ptr = buf;
    bd.size = bufLen;

    if (!(pFormatCtx = avformat_alloc_context())) {
        ret = AVERROR(ENOMEM);
        goto end;
    }

    avio_ctx_buffer = (uint8_t*)av_malloc(avio_ctx_buffer_size);
    if (!avio_ctx_buffer) {
        ret = AVERROR(ENOMEM);
        goto end;
    }
    pAvioCtx = avio_alloc_context(avio_ctx_buffer, avio_ctx_buffer_size,
        0, &bd, &read_packet, NULL, NULL);
    if (!pAvioCtx) {
        ret = AVERROR(ENOMEM);
        goto end;
    }
    pFormatCtx->pb = pAvioCtx;
    ret = avformat_open_input(&pFormatCtx, NULL, NULL, NULL);
    if (ret < 0) {
        isolate->ThrowException(v8::Exception::Error(String::NewFromUtf8(isolate, "Failed to open input").ToLocalChecked()));
        goto end;
    }

    for (int i = 0; i < pFormatCtx->nb_streams; i++)
    {
        if (pFormatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO && videoStream < 0)
        {
            videoStream = i;
        }
    }

    if (videoStream == -1)
    {
        isolate->ThrowException(v8::Exception::Error(String::NewFromUtf8(isolate, "Cannot find video stream").ToLocalChecked()));
        goto end;
    }

    pCodecParams = pFormatCtx->streams[videoStream]->codecpar;

    *width = pCodecParams->width;
    *height = pCodecParams->height;


end:
    avformat_close_input(&pFormatCtx);
    av_freep(&pAvioCtx->buffer);
    av_freep(&pAvioCtx);
    if (ret < 0) {
        isolate->ThrowException(v8::Exception::Error(String::NewFromUtf8(isolate, "something happened while cleaning up").ToLocalChecked()));
        return 1;
    }

    return 0;
}

bool DecodeVideoPacket(AVPacket* pPacket, AVCodecContext* pCodecContext, AVFrame* pFrame)
{
    int ret = avcodec_send_packet(pCodecContext, pPacket);
    if (ret < 0)
    {
        return false;
    }

    ret = avcodec_receive_frame(pCodecContext, pFrame);
    if (ret != 0)
    {
        return false;
    }

    char avPixelFormat[10] = { 0 };
    av_get_pix_fmt_string(&avPixelFormat[0], sizeof(avPixelFormat), (AVPixelFormat)pFrame->format);
    return true;
}

int FFMPEGScale(Isolate* isolate, AVFrame* frame1, int dst_w, int dst_h, AVPixelFormat src_pix_fmt, AVPixelFormat dst_pix_fmt, AVFrame* frame2)
{
    int ret;
    struct SwsContext* sws_ctx;

    frame2 = av_frame_alloc();

    printf("get sws context\n");
    sws_ctx = sws_getContext(frame1->width, frame1->height, src_pix_fmt,
        dst_w, dst_h, dst_pix_fmt,
        SWS_BILINEAR, NULL, NULL, NULL);

    if (!sws_ctx)
    {
        isolate->ThrowException(v8::Exception::SyntaxError(String::NewFromUtf8(isolate, "Cannot create scale context").ToLocalChecked()));
        ret = AVERROR(EINVAL);
        goto end;
    }

    printf("do scale\n");
    sws_scale(sws_ctx, frame1->data, frame1->linesize, 0, frame1->height, frame2->data, frame2->linesize);

end:
    //av_freep(&src_data[0]);
    av_frame_free(&frame2);
    sws_freeContext(sws_ctx);
    return ret < 0;
}

int FFMPEGExtractThumbnail(Isolate* isolate, uint8_t* buf, int bufLen, uint8_t** outBuf, int* outBufSize)
{
    AVPacket* pPacket;
    AVFormatContext* pFormatCtx = NULL;
    AVIOContext* pAvioCtx = NULL;
    const AVCodecParameters* pCodecParams = NULL;
    const AVCodec* pCodec = NULL;
    AVCodecContext* pCodecCtx = NULL;
    AVFrame* pFrame;
    int ret;
    int videoStream = -1;
    struct buffer_data bd = { 0 };
    uint8_t* avio_ctx_buffer = NULL;
    size_t avio_ctx_buffer_size = 4096;
    AVFormatContext* pOutputFormatContext = NULL;
    AVStream* pStream = NULL;
    bool bDecodeResult = false;
    const AVOutputFormat* pOutputFormat;
    AVCodecParameters* parameters;
    const AVCodec* codec;
    AVCodecContext* codecContext = NULL;
    int numFrames;
    int numOfDivision;
    int64_t timeStampLength, timeStampStepSize, timeStampIter;
    int frameCounter = 0;

    bd.ptr = buf;
    bd.size = bufLen;

    printf("allocate format ctx\n");
    if (!(pFormatCtx = avformat_alloc_context())) {
        ret = AVERROR(ENOMEM);
        goto end;
    }

    printf("allocate avio context buffer\n");
    avio_ctx_buffer = (uint8_t*)av_malloc(avio_ctx_buffer_size);
    if (!avio_ctx_buffer) {
        ret = AVERROR(ENOMEM);
        goto end;
    }
    printf("allocate avio context\n");
    pAvioCtx = avio_alloc_context(avio_ctx_buffer, avio_ctx_buffer_size,
        0, &bd, &read_packet, NULL, NULL);
    if (!pAvioCtx) {
        ret = AVERROR(ENOMEM);
        goto end;
    }
    pFormatCtx->pb = pAvioCtx;
    printf("open input\n");
    ret = avformat_open_input(&pFormatCtx, NULL, NULL, NULL);
    if (ret < 0) {
        //printf("open input fail\n");
        isolate->ThrowException(v8::Exception::Error(String::NewFromUtf8(isolate, "Failed to open input").ToLocalChecked()));
        goto end;
    }

    printf("find stream info\n");
    ret = avformat_find_stream_info(pFormatCtx, NULL);
    if (ret < 0)
    {
        isolate->ThrowException(v8::Exception::Error(String::NewFromUtf8(isolate, "Failed to find stream info").ToLocalChecked()));
        goto end;
    }

    printf("looking for video stream index\n");
    for (int i = 0; i < pFormatCtx->nb_streams; i++)
    {
        if (pFormatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO && videoStream < 0)
        {
            videoStream = i;
            pCodecParams = pFormatCtx->streams[i]->codecpar;
            break;
        }
    }

    printf("exit loop\n");
    if (videoStream == -1 || pCodecParams->width == 0 || pCodecParams->height == 0)
    {
        isolate->ThrowException(v8::Exception::Error(String::NewFromUtf8(isolate, "No video streams or no suitable codec found").ToLocalChecked()));
        goto end;
    }

    printf("get decoder\n");
    pCodec = avcodec_find_decoder(pCodecParams->codec_id);
    if (pCodec == NULL)
    {
        //const char* codecName = avcodec_get_name(pCodecParams->codec_id);
        isolate->ThrowException(v8::Exception::Error(String::NewFromUtf8(isolate, "Failed to find decoder for codec").ToLocalChecked()));
        goto end;
    }

    printf("allocate codec context\n");
    pCodecCtx = avcodec_alloc_context3(pCodec);
    if (pCodecCtx == NULL)
    {
        isolate->ThrowException(v8::Exception::Error(String::NewFromUtf8(isolate, "Failed to allocate codec context").ToLocalChecked()));
        goto end;
    }

    printf("copy params to context\n");
    ret = avcodec_parameters_to_context(pCodecCtx, pCodecParams);
    if (ret < 0)
    {
        isolate->ThrowException(v8::Exception::Error(String::NewFromUtf8(isolate, "Failed to copy params to ctx").ToLocalChecked()));
        goto end;
    }

    printf("open decoder\n");
    ret = avcodec_open2(pCodecCtx, pCodec, NULL);
    if (ret < 0)
    {
        isolate->ThrowException(v8::Exception::Error(String::NewFromUtf8(isolate, "Failed to initialize decoder").ToLocalChecked()));
        goto end;
    }

    printf("allocate frame\n");
    pFrame = av_frame_alloc();
    if (pFrame == NULL)
    {
        isolate->ThrowException(v8::Exception::Error(String::NewFromUtf8(isolate, "Failed to allocate frame").ToLocalChecked()));
        goto end;
    }

    printf("allocate packet\n");
    pPacket = av_packet_alloc();
    if (pPacket == NULL)
    {
        isolate->ThrowException(v8::Exception::Error(String::NewFromUtf8(isolate, "Failed to allocate packet").ToLocalChecked()));
        goto end;
    }

    numFrames = 1;
    frameCounter = numFrames;
    numOfDivision = numFrames + 1;

    timeStampLength = pFormatCtx->streams[videoStream]->duration;
    timeStampStepSize = timeStampLength / numOfDivision;
    timeStampIter = timeStampStepSize;

    printf("Reading frames\n");
    while (1)
    {
        //printf("read frame\n");
        ret = av_read_frame(pFormatCtx, pPacket);
        if (ret != 0)
        {
            //printf("read frame fail\n");
            goto end;
        }
        //printf("A: %d\n", pPacket->size);
        // check the packet type
        if (pPacket->stream_index == videoStream)
        {
            //printf("stream index match, decoding\n");
            // decode the packet into the pFrame
            bDecodeResult = DecodeVideoPacket(pPacket, pCodecCtx, pFrame);
            // ...
            if (bDecodeResult)
            {
                // ...
                if (--frameCounter <= 0) break;
            }
            // ...
            // move to the next frame of interest
            timeStampIter += timeStampStepSize;
            //printf("Seeking forward\n");
            av_seek_frame(pFormatCtx, videoStream, timeStampIter, AVSEEK_FLAG_BACKWARD);
        }
        // release the resource  read in the previous loop
        //printf("unref\n");
        av_packet_unref(pPacket);
    }

    printf("guessing output format\n");
    pOutputFormat = av_guess_format("mjpeg", NULL, NULL);
    if (pOutputFormat == NULL)
    {
        isolate->ThrowException(v8::Exception::Error(String::NewFromUtf8(isolate, "Failed to get output format").ToLocalChecked()));
        goto end;
    }

    printf("allocate output codec params\n");
    parameters = avcodec_parameters_alloc();
    parameters->codec_id = pOutputFormat->video_codec;
    parameters->codec_type = AVMEDIA_TYPE_VIDEO;
    parameters->format = AV_PIX_FMT_YUVJ422P;
    parameters->width = pFrame->width;
    parameters->height = pFrame->height;

    // find AVCodec encoder
    printf("find encoder\n");
    codec = avcodec_find_encoder(parameters->codec_id);
    if (codec == NULL)
    {
        isolate->ThrowException(v8::Exception::Error(String::NewFromUtf8(isolate, "Failed to find encoder").ToLocalChecked()));
        goto end;
    }

    printf("allocate output codec context\n");
    codecContext = avcodec_alloc_context3(codec);
    if (codecContext == NULL)
    {
        isolate->ThrowException(v8::Exception::Error(String::NewFromUtf8(isolate, "Failed to allocate codec context").ToLocalChecked()));
        goto end;
    }

    printf("copy output params to context\n");
    ret = avcodec_parameters_to_context(codecContext, parameters);
    if (ret < 0)
    {
        isolate->ThrowException(v8::Exception::Error(String::NewFromUtf8(isolate, "Failed to copy params").ToLocalChecked()));
        goto end;
    }

    codecContext->time_base = AVRational{ 1, 25 };

    printf("init output av context\n");
    ret = avcodec_open2(codecContext, codec, NULL);
    if (ret < 0)
    {
        isolate->ThrowException(v8::Exception::Error(String::NewFromUtf8(isolate, "Failed to init codec context").ToLocalChecked()));
        goto end;
    }

    printf("sending frame to decoder\n");
    ret = avcodec_send_frame(codecContext, pFrame);
    if (ret < 0)
    {
       /* char buffer[AV_ERROR_MAX_STRING_SIZE];
        auto s = av_make_error_string(buffer, AV_ERROR_MAX_STRING_SIZE, ret);*/
        isolate->ThrowException(v8::Exception::Error(String::NewFromUtf8(isolate, "cannot send frame").ToLocalChecked()));
        goto end;
    }

    printf("allocate output packet\n");
    pPacket = av_packet_alloc();
    if (pPacket == NULL)
    {
        isolate->ThrowException(v8::Exception::Error(String::NewFromUtf8(isolate, "Failed to allocate packet").ToLocalChecked()));
        goto end;
    }


    printf("recieve packet\n");
    ret = avcodec_receive_packet(codecContext, pPacket);
    if (ret != 0)
    {
        char* a = av_make_error_string(new char[AV_ERROR_MAX_STRING_SIZE] {0}, AV_ERROR_MAX_STRING_SIZE, ret);
        //printf("Ret: %s\n", a);
        isolate->ThrowException(v8::Exception::Error(String::NewFromUtf8(isolate, "Failed to receive packet").ToLocalChecked()));
        goto end;
    }

    *outBufSize = pPacket->buf->size;
    *outBuf = pPacket->buf->data;

    printf("cleanup\n");
end:
    // release resource
    avformat_close_input(&pFormatCtx);
    printf("a\n");
    avcodec_free_context(&pCodecCtx);
    printf("b\n");
    /*av_free(&pFrame);*/
    //av_free(&pPacket);
    av_free(pAvioCtx);
    printf("c\n");
    av_free(&pOutputFormat);
    printf("d\n");
    //av_free(&parameters);
    av_free(codecContext);
    printf("e\n");
    //avcodec_close(codecContext);
    if (pOutputFormatContext != NULL)
        avio_close(pOutputFormatContext->pb);

    //av_packet_unref(pPacket);
    avcodec_parameters_free(&parameters);
    printf("f\n");
    //avcodec_free_context(&codecContext);
    printf("g\n");
    avformat_free_context(pOutputFormatContext);
    printf("h\n");
    if (ret < 0) {
        return ret;
    }

    return 0;
}

/// <summary>
/// Node function for getting dimensions of a video buffer
/// </summary>
/// <param name="args"></param>
void GetDimensions(const FunctionCallbackInfo<Value>& args) {
    int width, height;
    Isolate* isolate = args.GetIsolate();
    Local<Context> context = isolate->GetCurrentContext();

    if (args.Length() != 2)
    {
        isolate->ThrowException(v8::Exception::SyntaxError(String::NewFromUtf8(isolate, "Invalid Arguments").ToLocalChecked()));
        return;
    }

    unsigned char* buf = (uint8_t*)node::Buffer::Data(args[0]->ToObject(context).ToLocalChecked());
    int bufSize = args[1]->Int32Value(context).ToChecked();

    int ret = FFMPEGGetDimensions(isolate, buf, bufSize, &width, &height);
    if (ret != 0)
    {
        // we've already thrown errors, so we shouldn't need to here right?
        return;
    }

    Local<Object> retObj = Object::New(isolate);
    retObj->Set(context,
        String::NewFromUtf8(isolate,
            "width").ToLocalChecked(),
        Number::New(isolate, width))
        .FromJust();
    retObj->Set(context,
        String::NewFromUtf8(isolate,
            "height").ToLocalChecked(),
        Number::New(isolate, height))
        .FromJust();

    args.GetReturnValue().Set(retObj);
}


/// <summary>
/// Node function for extracting a thumbnail from a video buffer
/// </summary>
/// <param name="args"></param>
void ExtractThumbnail(const FunctionCallbackInfo<Value>& args)
{
    uint8_t* outBuf;
    int outBufSize;
    Isolate* isolate = args.GetIsolate();
    Local<Context> context = isolate->GetCurrentContext();

    if (args.Length() != 2)
    {
        isolate->ThrowException(v8::Exception::SyntaxError(String::NewFromUtf8(isolate, "Invalid Arguments").ToLocalChecked()));
        return;
    }

    unsigned char* buf = (unsigned char*)node::Buffer::Data(args[0]->ToObject(context).ToLocalChecked());
    int bufSize = args[1]->Int32Value(context).ToChecked();

    int ret = FFMPEGExtractThumbnail(isolate, buf, bufSize, &outBuf, &outBufSize);
    if (ret != 0)
    {
        // we've already thrown errors, so we shouldn't need to here right?
        return;
    }

    Local<v8::Object> js_buffer =
        node::Buffer::Copy(isolate,
            (const char*)outBuf,
            (size_t)outBufSize)
        .ToLocalChecked();

    args.GetReturnValue().Set(js_buffer);
}

void InitAll(Handle<Object> exports)
{
    Isolate* isolate = exports->GetIsolate();
    Local<Context> context = isolate->GetCurrentContext();

    Local<ObjectTemplate> addon_data_tpl = ObjectTemplate::New(isolate);
    addon_data_tpl->SetInternalFieldCount(2);
    Local<Object> addon_data =
        addon_data_tpl->NewInstance(context).ToLocalChecked();
    
    Local<FunctionTemplate> tplGd = FunctionTemplate::New(isolate, GetDimensions, addon_data);
    Local<Function> gdFunc = tplGd->GetFunction(context).ToLocalChecked();

    exports->Set(context, String::NewFromUtf8(
        isolate, "getDimensions").ToLocalChecked(),
        gdFunc).FromJust();

    Local<FunctionTemplate> tplEt = FunctionTemplate::New(isolate, ExtractThumbnail, addon_data);
    Local<Function> etFunc = tplEt->GetFunction(context).ToLocalChecked();

    exports->Set(context, String::NewFromUtf8(
        isolate, "extractThumbnail").ToLocalChecked(),
        etFunc).FromJust();
}

NODE_MODULE(libfosscordcdn, InitAll);