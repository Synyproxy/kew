/**
 * @file waveform.h
 * @brief Whole-track loudness envelope for the waveform progress view.
 *
 * Decodes a file once on a background thread and keeps one RMS value per
 * short window of audio. The UI maps those windows onto terminal columns
 * to draw a SoundCloud style waveform where bar height follows volume.
 * Finished envelopes are cached on disk so a track only gets decoded once.
 */

#ifndef WAVEFORM_H
#define WAVEFORM_H

#include <stdbool.h>
#include <stddef.h>

/** Seconds of audio summarised by one envelope value. */
#define WAVEFORM_WINDOW_SECONDS 0.25

/**
 * @brief Snapshot of the envelope for one file, safe to read from the UI.
 *
 * `values` holds `count` linear RMS levels, one per window. While
 * `complete` is false the values are only known for the first
 * `count * WAVEFORM_WINDOW_SECONDS` seconds of the track.
 *
 * `peak` is the loudest window seen so far. `low_db`, `mid_db` and
 * `high_db` are the 5th, 50th and 95th percentile loudness in decibels
 * below that peak. `waveform_level` stretches a value between them so
 * the body of any track, however compressed, spreads across the height.
 */
typedef struct {
        const float *values;
        size_t count;
        bool complete;
        float peak;
        float low_db;
        float mid_db;
        float high_db;
} WaveformView;

/** Rows the waveform uses at most, however tall the visualizer area is. */
#define WAVEFORM_MAX_ROWS 3

/** @brief Maps a linear RMS value to a display height in [0,1] on a dB scale. */
float waveform_level(const WaveformView *view, float rms);

/**
 * @brief Ensures an envelope exists or is being computed for `file_path`.
 *
 * Cheap when called repeatedly with the same path. A different path cancels
 * any computation in progress and starts a new one.
 */
void waveform_request(const char *file_path);

/**
 * @brief Locks the envelope for `file_path` for reading.
 *
 * Returns false if no envelope is known for that file. When true is
 * returned, `waveform_release` must be called once the view is no longer
 * used.
 */
bool waveform_acquire(const char *file_path, WaveformView *view);

/** @brief Unlocks the envelope acquired with `waveform_acquire`. */
void waveform_release(void);

/** @brief Stops any running computation and frees everything. */
void waveform_shutdown(void);

#endif
