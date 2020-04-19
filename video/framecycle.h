#ifndef VIDEOFRAME_H
#define VIDEOFRAME_H

#include <vector>

#include "image/image.h"

class FrameCycle
{
public:
    FrameCycle() : FrameCycle(0, 0) {}
    FrameCycle(int width, int height);
    ~FrameCycle() { cleanUpAll(); }

    bool isValid() const;

    void resize(int width, int height);

    struct AVFrame *frame() { return cycle_infos[current].frame; }
    const Image &image() const { return cycle_infos[current].image; }

    void shift();
    void reset(); // -> shift one cycle

private:
    void alloc(size_t frame_index);
    void allocAll();
    void cleanUp(size_t frame_index);
    void cleanUpAll();

    void errorMsg(const char *msg);

    int width;
    int height;
    int num_bytes;

    int current;

    struct CycleInfo
    {
        Image image;
        struct AVFrame *frame;
        uint8_t *buffer;
        bool need_resize;
        bool has_errors;
    };

    std::vector<CycleInfo> cycle_infos;
};

#endif // VIDEOFRAME_H
