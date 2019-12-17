#include "playback.h"


////////// GLOBAL VARIABLES //////////
extern jf_global_state g_state;
extern mpv_handle *g_mpv_ctx;
//////////////////////////////////////


////////// STATIC FUNCTIONS ///////////////
// playback_ticks refers to segment referred by id
static void jf_post_session_progress(const char *id, int64_t playback_ticks);
static void jf_post_session_stopped(const char *id, int64_t playback_ticks);


//  - update_function: callback for session update networking. Should be either
//    jf_post_session_progress or jf_post_session_stopped.
static void jf_post_session(const int64_t playback_ticks,
        void (*update_function) (const char *, int64_t));


///////////////////////////////////////////


////////// PROGRESS SYNC //////////
static void jf_post_session_progress(const char *id, int64_t playback_ticks)
{
    char *progress_post;

    progress_post = jf_json_generate_progress_post(id, playback_ticks);
    jf_net_request("/sessions/playing/progress",
            JF_REQUEST_ASYNC_DETACH,
            JF_HTTP_POST,
            progress_post);
    free(progress_post);
}


static void jf_post_session_stopped(const char *id, int64_t playback_ticks)
{
    char *progress_post;

    progress_post = jf_json_generate_progress_post(id, playback_ticks);
    jf_net_request("/sessions/playing/stopped",
            JF_REQUEST_ASYNC_DETACH,
            JF_HTTP_POST,
            progress_post);
    free(progress_post);
}


static void jf_post_session(const int64_t playback_ticks,
        void (*update_function)(const char *, int64_t))
{
    size_t i, last_part, current_part;
    int64_t accounted_ticks, current_tick_offset;

    // single-part items are blissfully simple and I lament my toil elsewise
    if (g_state.now_playing->children_count <= 1) {
        update_function(g_state.now_playing->id, playback_ticks);
        g_state.now_playing->playback_ticks = playback_ticks;
        return;
    }

    // split-part: figure out part number of current pos and last update
    accounted_ticks = 0;
    current_tick_offset = 0;
    for (i = 0; i < g_state.now_playing->children_count; i++) {
        if (accounted_ticks <= playback_ticks) {
            if (playback_ticks < accounted_ticks + g_state.now_playing->children[i]->runtime_ticks) {
                current_part = i;
            } else {
                current_tick_offset += g_state.now_playing->children[i]->runtime_ticks;
            }
        }
        if (accounted_ticks <= g_state.now_playing->playback_ticks
                && g_state.now_playing->playback_ticks < accounted_ticks + g_state.now_playing->children[i]->runtime_ticks) {
            last_part = i;
        }
        accounted_ticks += g_state.now_playing->children[i]->runtime_ticks;
    }

    // update progress of current part and record last update
    update_function(g_state.now_playing->children[current_part]->id,
            playback_ticks - current_tick_offset);
    g_state.now_playing->playback_ticks = playback_ticks;
    
    // check if moved across parts and in case update
    if (last_part == current_part) return;
    for (i = 0; i < g_state.now_playing->children_count; i++) {
        if (i < current_part) {
            jf_menu_mark_played(g_state.now_playing->children[i]);
        } else if (i > current_part) {
            jf_menu_mark_unplayed(g_state.now_playing->children[i]);
        }
    }
}


void jf_playback_update_progress(const int64_t playback_ticks)
{
    jf_post_session(playback_ticks, jf_post_session_progress);
}


void jf_playback_update_stopped(const int64_t playback_ticks)
{
    jf_post_session(playback_ticks, jf_post_session_stopped);
}
///////////////////////////////////


////////// SUBTITLES //////////
extern inline void jf_playback_align_subtitle(const int64_t sid)
{
    int64_t track_count, track_id, playback_ticks, sub_delay;
    size_t i;
    long long offset_ticks;
    int success, is_external;
    bool is_sub;
    char num[3];
    char *track_type, *tmp;

    if (g_state.now_playing->children_count <= 1) return;

    // look for right track
    if (mpv_get_property(g_mpv_ctx, "track-list/count", MPV_FORMAT_INT64, &track_count) != 0) return;
    i = 0; // track-numbers are 0-based
    while (true) {
        if ((int64_t)i >= track_count) return;
        success = snprintf(num, 3, "%ld", i);
        if (success < 0 || success >= 3) {
            i++;
            continue;
        }
        tmp = jf_concat(3, "track-list/", num, "/id");
        success = mpv_get_property(g_mpv_ctx, tmp, MPV_FORMAT_INT64, &track_id);
        free(tmp);
        if (success != 0) {
            i++;
            continue;
        }
        tmp = jf_concat(3, "track-list/", num, "/type");
        success = mpv_get_property(g_mpv_ctx, tmp, MPV_FORMAT_STRING, &track_type);
        free(tmp);
        if (success != 0) {
            i++;
            continue;
        }
        is_sub = strcmp(track_type, "sub") == 0;
        mpv_free(track_type);
        if (track_id == sid && is_sub) break;
        i++;
    }

    // check if external
    tmp = jf_concat(3, "track-list/", num, "/external");
    success = mpv_get_property(g_mpv_ctx, tmp, MPV_FORMAT_FLAG, &is_external);
    free(tmp);
    if (success != 0) {
        fprintf(stderr, 
                "Warning: could not align subtitle track to split-file: mpv_get_property (external): %s.\n",
                mpv_error_string(success));
        return;
    }
    if (is_external) {
        // compute offset
        success = mpv_get_property(g_mpv_ctx, "time-pos", MPV_FORMAT_INT64, &playback_ticks);
        if (success != 0) {
            fprintf(stderr, 
                    "Warning: could not align subtitle track to split-file: mpv_get_property (time-pos): %s.\n",
                    mpv_error_string(success));
            return;
        }
        playback_ticks = JF_SECS_TO_TICKS(playback_ticks);
        offset_ticks = 0;
        i = 0;
        while (i < g_state.now_playing->children_count
                && offset_ticks + g_state.now_playing->children[i]->runtime_ticks <= playback_ticks) {
            offset_ticks += g_state.now_playing->children[i]->runtime_ticks;
            i++;
        }
        sub_delay = JF_TICKS_TO_SECS(offset_ticks);

        // apply
        success = mpv_set_property(g_mpv_ctx, "sub-delay", MPV_FORMAT_INT64, &sub_delay);
        if (success != 0) {
            fprintf(stderr,
                    "Warning: could not align subtitle track to split-file: mpv_set_property: %s.\n",
                    mpv_error_string(success));
        }
    } else {
        // internal are graciously aligned by EDL protocol: 0 offset
        sub_delay = 0;
        success = mpv_set_property(g_mpv_ctx, "sub-delay", MPV_FORMAT_INT64, &sub_delay);
        if (success != 0) {
            fprintf(stderr,
                    "Warning: could not align subtitle track to split-file: mpv_set_property: %s.\n",
                    mpv_error_string(success));
        }
    }
}
///////////////////////////////


////////// PLAYLIST CONTROLS //////////
bool jf_playback_next()
{
    if (g_state.playlist_position < jf_disk_playlist_item_count()) {
        jf_menu_play_item(jf_disk_playlist_get_item(++g_state.playlist_position));
        return true;
    } else {
        return false;
    }
}


bool jf_playback_previous()
{
    if (g_state.playlist_position > 1) {
        jf_menu_play_item(jf_disk_playlist_get_item(--g_state.playlist_position));
        return true;
    } else {
        return false;
    }
}
///////////////////////////////////////
