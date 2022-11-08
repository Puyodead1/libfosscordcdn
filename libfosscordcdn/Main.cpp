#define BUILDING_NODE_EXTENSION

#include <stdio.h>
#include <node.h>

#include <node.h>
#include <node_object_wrap.h>

extern "C"
{
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
}

using namespace v8;

int FFMPEGGetDimensions(Isolate* isolate, char* path, int* width, int* height)
{
    AVFormatContext* pFormatCtx = NULL;
    const AVCodecParameters* pCodecParams = NULL;
    int ret;
    int videoStream = -1;

    if ((ret = avformat_open_input(&pFormatCtx, path, NULL, NULL)))
        return ret;

    if ((ret = avformat_find_stream_info(pFormatCtx, NULL)) < 0) {
        isolate->ThrowException(v8::Exception::Error(String::NewFromUtf8(isolate, "Cannot find stream information").ToLocalChecked()));
        return ret;
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
        return 1;
    }

    pCodecParams = pFormatCtx->streams[videoStream]->codecpar;

    *width = pCodecParams->width;
    *height = pCodecParams->height;

    avformat_close_input(&pFormatCtx);
    return 0;
}

void GetDimensions(const FunctionCallbackInfo<Value>& args) {
    int width, height;
    Isolate* isolate = args.GetIsolate();
    Local<Context> context = isolate->GetCurrentContext();

    Local<String> arg = args[0]->ToString(context).ToLocalChecked();
    v8::String::Utf8Value path(isolate, arg);

    int ret = FFMPEGGetDimensions(isolate, *path, &width, &height);

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