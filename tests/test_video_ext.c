#include "kew_test.h"
#include "utils/video_ext.h"

int main(void)
{
        /* Disabled: nothing is video, regexes are the old audio ones. */
        video_ext_configure("");
        CHECK(!video_enabled());
        CHECK(!is_video_path("/a/b/clip.mp4"));
        CHECK_STR(library_extensions_regex(),
                  "(m4a|aac|mp3|ogg|flac|wav|aiff|opus|webm|m3u|m3u8)$");
        CHECK_STR(music_extensions_regex(),
                  "(m4a|aac|mp3|ogg|flac|wav|aiff|opus|webm)$");

        /* Default list. */
        video_ext_configure("mp4|mkv|mov|avi|m4v");
        CHECK(video_enabled());
        CHECK(is_video_path("/a/b/clip.mp4"));
        CHECK(is_video_path("/a/b/CLIP.MKV"));
        CHECK(is_video_path("clip.m4v"));
        CHECK(!is_video_path("/a/b/song.m4a"));
        CHECK(!is_video_path("/a/b/song.webm"));
        CHECK(!is_video_path("noextension"));
        CHECK(!is_video_path("/dir.mp4/song.flac"));
        CHECK(!is_video_path(NULL));
        CHECK_STR(library_extensions_regex(),
                  "(m4a|aac|mp3|ogg|flac|wav|aiff|opus|webm|m3u|m3u8|mp4|mkv|mov|avi|m4v)$");
        CHECK_STR(music_extensions_regex(),
                  "(m4a|aac|mp3|ogg|flac|wav|aiff|opus|webm|mp4|mkv|mov|avi|m4v)$");

        /* User adds webm as video; it is still in the audio list, so the
         * regex must not repeat it. */
        video_ext_configure("mp4|webm");
        CHECK(is_video_path("x.webm"));
        CHECK_STR(music_extensions_regex(),
                  "(m4a|aac|mp3|ogg|flac|wav|aiff|opus|webm|mp4)$");

        /* Whitespace and stray separators are tolerated. */
        video_ext_configure(" mp4 | mkv ||");
        CHECK(is_video_path("x.mkv"));
        CHECK_STR(music_extensions_regex(),
                  "(m4a|aac|mp3|ogg|flac|wav|aiff|opus|webm|mp4|mkv)$");

        KT_MAIN_END();
}
