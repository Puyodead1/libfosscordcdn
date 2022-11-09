#define BUILDING_NODE_EXTENSION

#include <stdio.h>
#include <node.h>
#include <node_object_wrap.h>
#include <node_buffer.h>
#include <windows.h>

extern "C"
{
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
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


int FFMPEGGetDimensions(Isolate* isolate, uint8_t* buf, int bufLen, int* width, int* height)
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

void GetDimensions(const FunctionCallbackInfo<Value>& args) {
    int width, height;
    Isolate* isolate = args.GetIsolate();
    Local<Context> context = isolate->GetCurrentContext();

    uint8_t* buf = (uint8_t*)node::Buffer::Data(args[0]->ToObject(context).ToLocalChecked());
    int bufSize = args[1]->Int32Value(context).ToChecked();

    int ret = FFMPEGGetDimensions(isolate, buf, bufSize, &width, &height);

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

void InitAll(Handle<Object> exports)
{
    Isolate* isolate = exports->GetIsolate();
    Local<Context> context = isolate->GetCurrentContext();

    Local<ObjectTemplate> addon_data_tpl = ObjectTemplate::New(isolate);
    addon_data_tpl->SetInternalFieldCount(1);
    Local<Object> addon_data =
        addon_data_tpl->NewInstance(context).ToLocalChecked();
    
    Local<FunctionTemplate> tpl = FunctionTemplate::New(isolate, GetDimensions, addon_data);
    Local<Function> func = tpl->GetFunction(context).ToLocalChecked();

    exports->Set(context, String::NewFromUtf8(
        isolate, "getDimensions").ToLocalChecked(),
        func).FromJust();
}

NODE_MODULE(libfosscordcdn, InitAll);