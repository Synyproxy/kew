/**
 * @file video_decoder.h
 * @brief A miniaudio data source that plays silence while mpv shows a video.
 *
 * The sound engine treats it like any other codec: it is initialised when a
 * track is probed or preloaded, read from the decode thread, seeked, and
 * uninitialised when the track is dropped. mpv is only started on the first
 * read, so preloading the next track does not open a window early.
 */
#ifndef VIDEO_DECODER_H
#define VIDEO_DECODER_H

#include "video_ipc.h"

#include <miniaudio.h>
#include <stdbool.h>

#define VIDEO_SAMPLE_RATE 48000
#define VIDEO_CHANNELS 2
/* Seconds of silence allowed past the probed length before giving up on eof. */
#define VIDEO_END_GRACE_SECONDS 5

typedef struct {
        ma_data_source_base ds;
        ma_uint32 channels;
        ma_uint32 sample_rate;
        ma_uint64 cursor;
        ma_uint64 length;
        char path[4096];
        bool started;
        bool failed;
} ma_video;

ma_result ma_video_init_file(const char *path, const ma_decoding_backend_config *cfg, ma_video *v);
void ma_video_uninit(ma_video *v);
ma_result ma_video_read_pcm_frames(ma_video *v, void *out, ma_uint64 count, ma_uint64 *read);
ma_result ma_video_seek_to_pcm_frame(ma_video *v, ma_uint64 frame);
ma_result ma_video_get_data_format(ma_video *v, ma_format *f, ma_uint32 *ch, ma_uint32 *sr,
                                   ma_channel *map, size_t cap);
ma_result ma_video_get_cursor_in_pcm_frames(ma_video *v, ma_uint64 *cursor);
ma_result ma_video_get_length_in_pcm_frames(ma_video *v, ma_uint64 *length);

/** @brief Same shape as the other get_*_file_info helpers in audio_file_info.h. */
void get_video_file_info(const char *filename, ma_format *format, ma_uint32 *channels,
                         ma_uint32 *sample_rate, ma_channel *channel_map);

/* Seams to the player; tests provide their own. */
bool video_backend_load(const char *path, const void *owner);
void video_backend_release(const void *owner);
void video_backend_poll(VideoIpcStatus *out, bool *gone);
void video_backend_seek(double seconds);
double video_backend_probe_duration(const char *path);

#endif
