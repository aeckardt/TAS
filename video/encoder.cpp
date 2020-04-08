#include "encoder.h"

extern "C"
{
#include "libavcodec/avcodec.h"
#include "libavutil/opt.h"
}

static const char* codec_name      = "libx264rgb";
static const AVPixelFormat pix_fmt = AV_PIX_FMT_BGR0;

// void(0) is used to enforce semicolon after the macro
#define errorMsgf(format, ...) \
{ char *buffer = new char[strlen(format) * 2 + 50]; sprintf(buffer, format, __VA_ARGS__); errorMsg(buffer); } (void)0

VideoEncoder::VideoEncoder()
    : av_error(0),
      width(0),
      height(0),
      frame_rate(0),
      ctx(nullptr),
      codec(nullptr),
      frame(nullptr),
      pkt(nullptr),
      pts(0),
      file(nullptr)
{
    av_log_set_level(AV_LOG_ERROR);
}

void VideoEncoder::allocContext()
{
    const AVCodec *codec;

    codec = avcodec_find_encoder_by_name(codec_name);
    if (codec == nullptr) {
        errorMsgf("Codec '%s' not found", codec_name);
        return;
    }

    ctx = avcodec_alloc_context3(codec);
    if (ctx == nullptr)
        return errorMsg("Could not allocate video codec context");

    ctx->width = width;
    ctx->height = height;
    ctx->time_base = {1, frame_rate};
    ctx->gop_size = 10;
    ctx->max_b_frames = 1;
    ctx->pix_fmt = pix_fmt;

    // Use optimal number of threads
    // see https://superuser.com/questions/155305/how-many-threads-does-ffmpeg-use-by-default
    ctx->thread_count = 0;

    // Set optimal compression / speed ratio for this use-case
    av_opt_set(ctx->priv_data, "preset", "fast", 0);

    // Set constant rate factor to lossless
    // see https://trac.ffmpeg.org/wiki/Encode/H.264
    av_opt_set(ctx->priv_data, "crf",    "0",    0);

    av_error = avcodec_open2(ctx, codec, nullptr);
    if (av_error < 0) {
        char ch[AV_ERROR_MAX_STRING_SIZE] = {0};
        errorMsgf("Could not open codec: '%s' -> %s", codec_name, av_make_error_string(ch, AV_ERROR_MAX_STRING_SIZE, av_error));
        avcodec_free_context(&ctx);
        return;
    }
}

void VideoEncoder::allocFrame()
{
    frame = av_frame_alloc();
    if (frame == nullptr)
        return errorMsg("Could not allocate video frame");

    frame->format = pix_fmt;
    frame->width  = width;
    frame->height = height;

    av_error = av_frame_get_buffer(frame, 32);
    if (av_error < 0) {
        av_frame_free(&frame);
        return errorMsg("Could not allocate the video frame data");
    }
}

void VideoEncoder::cleanUp()
{
    if (file != nullptr) {
        fclose(file);
        file = nullptr;
    }
    if (ctx != nullptr)
        avcodec_free_context(&ctx);
    if (frame != nullptr)
        av_frame_free(&frame);
    image = QImage();
    if (pkt != nullptr)
        av_packet_free(&pkt);
    pts = 0;
}

void VideoEncoder::create(int width, int height, int frame_rate)
{
    this->width = width;
    this->height = height;
    this->frame_rate = frame_rate;

    initialize();

    file = fopen(video_file.fileName().toStdString().c_str(), "wb");
    if (file == nullptr)
        return errorMsg("Could not open file");
}

void VideoEncoder::encodeFrames(bool flush)
{
    if (ctx == nullptr || frame == nullptr || file == nullptr) {
        errorMsg("Error initializing encoder");
        return;
    }

    if (!flush) {
        // Send the frame to the encoder
        av_error = avcodec_send_frame(ctx, frame);
        if (av_error < 0)
            return errorMsg("Error sending a frame for encoding");
    } else {
        // Enter draining mode by sending empty buffer
        av_error = avcodec_send_frame(ctx, nullptr);
        if (av_error < 0)
            return errorMsg("Error flushing");
    }

    while (av_error >= 0) {
        av_error = avcodec_receive_packet(ctx, pkt);
        if (av_error == AVERROR(EAGAIN) || av_error == AVERROR_EOF) {
            if (flush) {
                fclose(file);
                file = nullptr;
            }
            return;
        }

        else if (av_error < 0)
            return errorMsg("Error during encoding");

        frame->pts = pts++;

        fwrite(pkt->data, 1, pkt->size, file);
        av_packet_unref(pkt);
    }
}

void VideoEncoder::errorMsg(const char *msg)
{
    last_error = msg;
    fprintf(stderr, "%s", msg);
}

void VideoEncoder::initialize()
{
    cleanUp();

    allocContext();
    if (ctx == nullptr)
        return;

    codec = ctx->codec;

    allocFrame();
    if (frame == nullptr)
        return;

    // Be careful: The image can only be used as regular QImage,
    // if its linesize is aligned with 32 bytes, which in this case
    // is equivalent to its width being divisble by 8
    image = QImage(frame->data[0], width, height, QImage::Format_RGB32);

    pkt = av_packet_alloc();
    if (pkt == nullptr) {
        errorMsg("Could not allocate packet");
        return;
    }
}