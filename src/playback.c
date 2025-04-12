#include "playback.h"
#include "disk.h"
#include "config.h"
#include "json.h"
#include "net.h"
#include "menu.h"
#include "shared.h"
#include "mpv.h"


#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>

////////// GLOBAL VARIABLES //////////
extern jf_global_state g_state;
extern jf_options g_options;
extern mpv_handle *g_mpv_ctx;
//////////////////////////////////////


////////// STATIC VARIABLES //////////
// element of the current playlist that was playing when we last printed the
// playlist to terminal
size_t s_last_playlist_print = 0;
//////////////////////////////////////


////////// STATIC FUNCTIONS ///////////////
// playback_ticks refers to segment referred by id
static void jf_post_session_update(const char *id,
        int64_t playback_ticks,
        const char *update_url);


static void jf_post_session(const int64_t playback_ticks,
        const char *update_url);


// Requests PlaybackPositionTicks for item's additionalparts (if any) and
// populates the field for item's children. item's own playback_ticks is set to
// 0.
//
// Parameters:
//  - item: the item to check (will be modified in place).
//
// Returns:
//  - true: on success;
//  - false: on failure, in which case playback_ticks may have been populated
//      for some of the children before encountering the failure.
static inline bool jf_playback_populate_video_ticks(jf_menu_item *item);


static void jf_playback_playlist_window(size_t window_size, size_t window[2]);
///////////////////////////////////////////


////////// PROGRESS SYNC //////////
static void jf_post_session_update(const char *id,
        int64_t playback_ticks,
        const char *update_url)
{
    char *progress_post;

    progress_post = jf_json_generate_progress_post(id, playback_ticks);
    jf_net_request(update_url,
            JF_REQUEST_ASYNC_DETACH,
            JF_HTTP_POST,
            progress_post);
    free(progress_post);
}


static void jf_post_session(const int64_t playback_ticks,
        const char *update_url)
{
    size_t current_part = (size_t)-1;
    size_t last_part = (size_t)-1;
    size_t i;
    int64_t accounted_ticks, current_tick_offset;

    // single-part items are blissfully simple and I lament my toil elsewise
    if (g_state.now_playing->children_count <= 1) {
        jf_post_session_update(g_state.now_playing->id,
                playback_ticks,
                update_url);
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

    // check error
    if (current_part == (size_t)-1 || last_part == (size_t)-1) {
        fprintf(stderr, "Warning: could not figure out playback position within a split-file. Playback progress was not notified to the server.\n");
        return;
    }

    // update progress of current part and record last update
    jf_post_session_update(g_state.now_playing->children[current_part]->id,
                playback_ticks - current_tick_offset,
                update_url);
    g_state.now_playing->playback_ticks = playback_ticks;
    
    // check if moved across parts and in case update
    if (last_part == current_part) return;
    for (i = 0; i < g_state.now_playing->children_count; i++) {
        if (i < current_part) {
            jf_menu_item_set_flag_detach(g_state.now_playing->children[i],
                    JF_FLAG_TYPE_PLAYED,
                    true);
        } else if (i > current_part) {
            jf_menu_item_set_flag_detach(g_state.now_playing->children[i],
                    JF_FLAG_TYPE_PLAYED,
                    false);
        }
    }
}


void jf_playback_update_playing(const int64_t playback_ticks)
{
    jf_post_session(playback_ticks, "/sessions/playing/playing");
}


void jf_playback_update_progress(const int64_t playback_ticks)
{
    jf_post_session(playback_ticks, "/sessions/playing/progress");
}


void jf_playback_update_stopped(const int64_t playback_ticks)
{
    jf_post_session(playback_ticks, "/sessions/playing/stopped");
}
///////////////////////////////////


////////// SUBTITLES //////////
void jf_playback_load_external_subtitles(void)
{
    char subs_language[4];
    size_t i, j;
    jf_menu_item *child;

    // external subtitles
    // note: they unfortunately require loadfile to already have been issued
    subs_language[3] = '\0';
    for (i = 0; i < g_state.now_playing->children_count; i++) {
        for (j = 0; j < g_state.now_playing->children[i]->children_count; j++) {
            child = g_state.now_playing->children[i]->children[j];
            if (child->type != JF_ITEM_TYPE_VIDEO_SUB) {
                fprintf(stderr,
                        "Warning: unrecognized g_state.now_playing type (%s) for %s, part %zu, child %zu. This is a bug.\n",
                        jf_item_type_get_name(child->type),
                        g_state.now_playing->name,
                        i,
                        j);
                continue;
            }

            // tmp = jf_menu_item_get_request_url(child);
            strncpy(subs_language, child->id, 3);
            const char *command[] = { "sub-add",
                jf_menu_item_get_request_url(child),
                "auto",
                child->id + 3,
                subs_language,
                NULL };
            if (mpv_command(g_mpv_ctx, command) < 0) {
                jf_reply *r = jf_net_request(child->name,
                        JF_REQUEST_IN_MEMORY,
                        JF_HTTP_GET,
                        NULL);
                fprintf(stderr,
                        "Warning: external subtitle %s could not be loaded.\n",
                        child->id[3] != '\0' ? child->id + 3 : child->name);
                if (r->state == JF_REPLY_ERROR_HTTP_400) {
                    fprintf(stderr, "Reason: %s.\n", r->payload);
                }
                jf_reply_free(r);
            }
        }
    }

}


void jf_playback_align_subtitle(const int64_t sid)
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


////////// ITEM PLAYBACK //////////
void jf_playback_play_video(const jf_menu_item *item)
{
    jf_growing_buffer filename;
    char *part_url;
    size_t i;
    jf_menu_item *child;

    // merge video files
    JF_MPV_ASSERT(mpv_set_property_string(g_mpv_ctx, "force-media-title", item->name));
    JF_MPV_ASSERT(mpv_set_property_string(g_mpv_ctx, "title", item->name));
    filename = jf_growing_buffer_new(128);
    jf_growing_buffer_append(filename, "edl://", JF_STATIC_STRLEN("edl://"));
    for (i = 0; i < item->children_count; i++) {
        child = item->children[i];
        if (child->type != JF_ITEM_TYPE_VIDEO_SOURCE) {
            fprintf(stderr,
                    "Warning: unrecognized item type (%s) for %s part %zu. This is a bug.\n",
                    jf_item_type_get_name(child->type), item->name, i);
            continue;
        }
        part_url = jf_menu_item_get_request_url(child);
        jf_growing_buffer_sprintf(filename, 0, "%%%zu%%%s", strlen(part_url), part_url);
        jf_growing_buffer_append(filename, ";", 1);
    }
    jf_growing_buffer_append(filename, "", 1);
    const char *loadfile[] = { "loadfile", filename->buf, NULL };
    JF_DEBUG_PRINTF("loadfile %s\n", filename->buf);
    JF_MPV_ASSERT(mpv_command(g_mpv_ctx, loadfile));
    jf_growing_buffer_free(filename);

    // external subtitles will be loaded at MPV_EVENT_START_FILE
    // after loadfile has been evaded
}


bool jf_playback_play_item(jf_menu_item *item)
{
    char *request_url;
    jf_reply *replies[2];

    if (item == NULL) {
        return false;
    }

    if (JF_ITEM_TYPE_IS_FOLDER(item->type)) {
        fprintf(stderr, "Error: jf_menu_play_item invoked on folder item type. This is a bug.\n");
        return false;
    }

    switch (item->type) {
        case JF_ITEM_TYPE_AUDIO:
        case JF_ITEM_TYPE_AUDIOBOOK:
            if ((request_url = jf_menu_item_get_request_url(item)) == NULL) {
                fprintf(stderr, "Error: jf_playback_play_item: jf_menu_item_get_request_url returned NULL. This is a bug.\n");
                jf_playback_end();
                return false;
            }
            if (jf_menu_ask_resume(item) == false) {
                jf_playback_end();
                return false;
            }
            JF_MPV_ASSERT(mpv_set_property_string(g_mpv_ctx, "title", item->name));
            const char *loadfile[] = { "loadfile", request_url, NULL };
            mpv_command(g_mpv_ctx, loadfile); 
            jf_menu_item_free(g_state.now_playing);
            g_state.now_playing = item;
            break;
        case JF_ITEM_TYPE_EPISODE:
        case JF_ITEM_TYPE_MOVIE:
        case JF_ITEM_TYPE_MUSIC_VIDEO:
            // check if item was already evaded re: split file and versions
            if (item->children_count > 0) {
                if (jf_menu_ask_resume(item) == false) {
                    jf_playback_end();
                    return false;
                }
                jf_playback_play_video(item);
            } else {
                request_url = jf_menu_item_get_request_url(item);
                replies[0] = jf_net_request(request_url,
                        JF_REQUEST_ASYNC_IN_MEMORY,
                        JF_HTTP_GET,
                        NULL);
                request_url = jf_concat(3, "/videos/", item->id, "/additionalparts");
                replies[1] = jf_net_request(request_url,
                        JF_REQUEST_IN_MEMORY,
                        JF_HTTP_GET,
                        NULL);
                if (JF_REPLY_PTR_HAS_ERROR(replies[1])) {
                    fprintf(stderr,
                            "Error: network request for /additionalparts of item %s failed: %s.\n",
                            item->name,
                            jf_reply_error_string(replies[1]));
                    jf_reply_free(replies[1]);
                    jf_reply_free(jf_net_await(replies[0]));
                    jf_playback_end();
                    return false;
                }
                if (JF_REPLY_PTR_HAS_ERROR(jf_net_await(replies[0]))) {
                    fprintf(stderr,
                            "Error: network request for item %s failed: %s.\n",
                            item->name,
                            jf_reply_error_string(replies[0]));
                    jf_reply_free(replies[0]);
                    jf_reply_free(replies[1]);
                    jf_playback_end();
                    return false;
                }
                jf_json_parse_video(item, replies[0]->payload, replies[1]->payload);
                jf_reply_free(replies[0]);
                jf_reply_free(replies[1]);
                if (jf_playback_populate_video_ticks(item) == false
                        || jf_menu_ask_resume(item) == false) {
                    jf_playback_end();
                    return false;
                }
                jf_playback_play_video(item);
                jf_disk_playlist_replace_item(g_state.playlist_position, item);
                jf_menu_item_free(g_state.now_playing);
                g_state.now_playing = item;
            }
            break;
        default:
            fprintf(stderr,
                    "Error: jf_menu_play_item unsupported type (%s). This is a bug.\n",
                    jf_item_type_get_name(item->type));
            return false;
    }

    return true;
}


static inline bool jf_playback_populate_video_ticks(jf_menu_item *item)
{
    jf_reply **replies;
    jf_growing_buffer part_url = jf_growing_buffer_new(0);
    size_t i;

    if (item == NULL) return true;
    if (item->type != JF_ITEM_TYPE_EPISODE
            && item->type != JF_ITEM_TYPE_MOVIE) return true;

    // the Emby interface was designed by a drunk gibbon. to check for
    // a progress marker, we have to request the items corresponding to
    // the additionalparts and look at them individually
    // ...and each may have its own bookmark!

    // parent and first child refer the same ID, thus the same part
    item->children[0]->playback_ticks = item->playback_ticks;
    // but at this point it makes no sense for the parent item to have a PB
    // tick since there may be multiple markers
    item->playback_ticks = 0;

    // now go and get all markers for all parts
    assert((replies = malloc((item->children_count - 1) * sizeof(jf_reply *))) != NULL);
    for (i = 1; i < item->children_count; i++) {
        jf_growing_buffer_empty(part_url);
        jf_growing_buffer_sprintf(part_url, 0,
                "/users/%s/items/%s",
                g_options.userid,
                item->children[i]->id);
        replies[i - 1] = jf_net_request(part_url->buf,
                JF_REQUEST_ASYNC_IN_MEMORY,
                JF_HTTP_GET,
                NULL);
    }
    jf_growing_buffer_free(part_url);

    for (i = 1; i < item->children_count; i++) {
        jf_net_await(replies[i - 1]);
        if (JF_REPLY_PTR_HAS_ERROR(replies[i - 1])) {
            fprintf(stderr,
                    "Error: could not fetch resume information for part %zu of item %s: %s.\n",
                    i + 1,
                    item->name,
                    jf_reply_error_string(replies[i - 1]));
            for (i = 1; i < item->children_count; i++) {
                if (replies[i - 1] != NULL) {
                    jf_net_await(replies[i - 1]);
                    jf_reply_free(replies[i - 1]);
                }
            }
            free(replies);
            return false;
        }
        jf_json_parse_playback_ticks(item->children[i], replies[i - 1]->payload);
        jf_reply_free(replies[i - 1]);
    }
    free(replies);
    return true;
}
///////////////////////////////////


////////// PLAYLIST CONTROLS //////////
bool jf_playback_next(void)
{
    jf_menu_item *item;
    bool more_playback;
    
    if (g_state.playlist_position == jf_disk_playlist_item_count()) {
        if (g_state.playlist_loops == 1 || g_state.playlist_loops == 0) return false;
        g_state.playlist_position = 1;
        g_state.playlist_loops--;
    } else {
        if (g_state.playlist_loops > 1) {
            g_state.loop_state = JF_LOOP_STATE_OUT_OF_SYNC;
        }
        g_state.playlist_position++;
    }

    item = jf_disk_playlist_get_item(g_state.playlist_position);
    more_playback = jf_playback_play_item(item);

#ifdef JF_DEBUG
    jf_menu_item_print(item);
#endif

    return more_playback;
}


bool jf_playback_previous(void)
{
    jf_menu_item *item;
    bool more_playback;
    
    if (g_state.playlist_position == 1) {
        if (g_state.playlist_loops == 1 || g_state.playlist_loops == 0) return false;
        g_state.playlist_position = jf_disk_playlist_item_count();
        // don't decrement the playlist loop counter going backwards
        // since that's how mpv does it
    } else {
        // NB going backwards will not trigger options/playlist-loop decrement
        g_state.playlist_position--;
    }

    item = jf_disk_playlist_get_item(g_state.playlist_position);
    more_playback = jf_playback_play_item(item);

#ifdef JF_DEBUG
    jf_menu_item_print(item);
#endif

    return more_playback;
}


void jf_playback_end(void)
{
    // kill playback core
    mpv_terminate_destroy(g_mpv_ctx);
    g_mpv_ctx = NULL;
    // enforce a clean state for the application
    jf_menu_item_free(g_state.now_playing);
    g_state.now_playing = NULL;
    g_state.playlist_position = 0;
    s_last_playlist_print = 0;
    // signal to enter UI mode
    g_state.state = JF_STATE_MENU_UI;
}


void jf_playback_shuffle_playlist(void)
{
    size_t pos = g_state.playlist_position - 1;
    size_t item_count_no_curr = jf_disk_playlist_item_count() - 1;
    size_t i;
    size_t j;

    for (i = 0; i < item_count_no_curr - 1; i++) {
        if (i == pos) continue;
        if ((j = (size_t)random() % (item_count_no_curr - i - 1) + i + 1) >= pos) {
            j++;
        }
        jf_disk_playlist_swap_items(i + 1, j + 1);
    }
}


static void jf_playback_playlist_window(size_t window_size, size_t window[2])
{
    size_t pos = g_state.playlist_position;
    size_t item_count = jf_disk_playlist_item_count();

    if (window_size == 0) {
        window_size = item_count;
    }

    // the window size is guaranteed (if there are enough items)
    // the window slides without ever going beyond boundaries
    if (pos <= window_size / 2) {
        window[0] = 1;
        window[1] = window_size > item_count ? item_count : window[0] + window_size - 1;
    } else if (pos + window_size / 2 > item_count) {
            window[1] = item_count;
            window[0] = window_size >= window[1] ? 1 : window[1] - window_size + 1;
    } else {
        window[0] = pos - window_size / 2;
        window[1] = window[0] + window_size - 1;
    }
}


void jf_playback_print_playlist(size_t window_size)
{
    size_t i;
    size_t pos = g_state.playlist_position;
    size_t terminal[2];
    int is_video;
    int64_t osd_h;
    int64_t osd_font_size;
    size_t osd[2]; 
    jf_growing_buffer osd_msg;
    const char *osd_cmd[3] = { "show-text", NULL, NULL };

    // print to terminal, but only if we didn't already do it for this item and 
    // this playlist. we must print manually because `show-text` doesn't print
    // to terminal during video playback. but we don't want to flood the term
    // with repeat prints
    if (g_state.playlist_position != s_last_playlist_print) {
        jf_mpv_terminal(g_mpv_ctx, false);
        
        jf_term_clear_bottom(NULL);

        jf_playback_playlist_window(window_size, terminal);
        fprintf(stdout, "\n===== jftui playlist (%zu items) =====\n", jf_disk_playlist_item_count());
        for (i = terminal[0]; i < pos; i++) {
            fprintf(stdout, "%zu: %s\n", i, jf_disk_playlist_get_item_name(i)); 
        }
        fprintf(stdout, "\t>>> %zu: %s <<<\n", i, g_state.now_playing->name);
        for (i = pos + 1; i <= terminal[1]; i++) {
            fprintf(stdout, "%zu: %s\n", i, jf_disk_playlist_get_item_name(i));
        }
        fprintf(stdout, "\n");

        jf_mpv_terminal(g_mpv_ctx, true);

        s_last_playlist_print = g_state.playlist_position;
    }

    // if there is a video output, print to OSD there too
    JF_MPV_ASSERT(mpv_get_property(g_mpv_ctx, "vo-configured", MPV_FORMAT_FLAG, &is_video));
    if (! is_video) return;

    // prepare OSD string
    JF_MPV_ASSERT(mpv_get_property(g_mpv_ctx, "osd-height", MPV_FORMAT_INT64, &osd_h));
    JF_MPV_ASSERT(mpv_get_property(g_mpv_ctx, "osd-font-size", MPV_FORMAT_INT64, &osd_font_size));
    // bad heuristic but better than nothing
    jf_playback_playlist_window((size_t)(osd_h/osd_font_size/2), osd);
    osd_msg = jf_growing_buffer_new(0);
    jf_growing_buffer_sprintf(osd_msg, 0, "===== jftui playlist (%zu items) =====", jf_disk_playlist_item_count());
    for (i = osd[0]; i < pos; i++) {
        jf_growing_buffer_sprintf(osd_msg, 0, "\n%zu: %s", i, jf_disk_playlist_get_item_name(i));
    }
    jf_growing_buffer_sprintf(osd_msg, 0, "\n\t>>> %zu: %s <<<", i, g_state.now_playing->name);
    for (i = pos + 1; i <= osd[1]; i++) {
        jf_growing_buffer_sprintf(osd_msg, 0, "\n%zu: %s", i, jf_disk_playlist_get_item_name(i));
    }
    jf_growing_buffer_append(osd_msg, "", 1);

    // print to OSD
    osd_cmd[1] = osd_msg->buf;
    JF_MPV_ASSERT(mpv_command(g_mpv_ctx, osd_cmd));
    jf_growing_buffer_free(osd_msg);
}
///////////////////////////////////////
