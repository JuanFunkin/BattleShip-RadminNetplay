/* minimp3 (CC0, lieff/minimp3) — decode a whole MP3 buffer to interleaved s16. */
#define MINIMP3_IMPLEMENTATION
#include "minimp3.h"
#include <stdlib.h>
#include <string.h>

/* Returns frames decoded (per channel) or 0 on failure. *out is malloc'd; caller free()s. */
int port_mp3_decode(const unsigned char *data, int size, short **out, int *channels, int *hz)
{
    mp3dec_t dec;
    mp3dec_frame_info_t info;
    short pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];
    size_t cap = 0, len = 0; /* in shorts */
    short *buf = NULL;
    int pos = 0, ch = 0, rate = 0;

    mp3dec_init(&dec);
    while (pos < size) {
        int n = mp3dec_decode_frame(&dec, data + pos, size - pos, pcm, &info);
        if (info.frame_bytes <= 0) break;
        pos += info.frame_bytes;
        if (n <= 0) continue;
        if (ch == 0) { ch = info.channels; rate = info.hz; }
        if (info.channels != ch) continue;
        if (len + (size_t)n * ch > cap) {
            cap = cap ? cap * 2 : (1u << 20);
            while (cap < len + (size_t)n * ch) cap *= 2;
            short *nb = (short *)realloc(buf, cap * sizeof(short));
            if (!nb) { free(buf); return 0; }
            buf = nb;
        }
        memcpy(buf + len, pcm, (size_t)n * ch * sizeof(short));
        len += (size_t)n * ch;
    }
    if (!buf || ch == 0) { free(buf); return 0; }
    *out = buf; *channels = ch; *hz = rate;
    return (int)(len / ch);
}
