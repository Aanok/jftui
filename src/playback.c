#include "playback.h"


////////// GLOBAL VARIABLES //////////
extern jf_global_state g_state;
extern jf_options g_options;
extern mpv_handle *g_mpv_ctx;
//////////////////////////////////////


////////// STATIC FUNCTIONS ///////////////
// playback_ticks refers to segment referred by id
// static void jf_post_session_progress(const char *id, int64_t playback_ticks);
// static void jf_post_session_stopped(const char *id, int64_t playback_ticks);
static void jf_post_session_update(const char *id,
        int64_t playback_ticks,
        const char *update_url);


//  - update_function: callback for session update networking. Should be either
//    jf_post_session_progress or jf_post_session_stopped.
static void jf_post_session(const int64_t playback_ticks,
        const char *update_url);
//         void (*update_function) (const char *, int64_t));


static inline void jf_playback_populate_video_ticks(jf_menu_item *item);
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
    size_t i, last_part, current_part;
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

    // update progress of current part and record last update
    jf_post_session_update(g_state.now_playing->children[current_part]->id,
                playback_ticks - current_tick_offset,
                update_url);
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
    jf_post_session(playback_ticks, "/sessions/playing/progress");
}


void jf_playback_update_stopped(const int64_t playback_ticks)
{
    jf_post_session(playback_ticks, "/sessions/playing/stopped");
}
///////////////////////////////////


////////// SUBTITLES //////////
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
    jf_growing_buffer *filename;
    char *tmp;
    char subs_language[4];
    size_t i, j;
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
        jf_growing_buffer_append(filename,
                jf_menu_item_get_request_url(child),
                0);
        jf_growing_buffer_append(filename, ";", 1);
    }
    jf_growing_buffer_append(filename, "", 1);
    const char *loadfile[] = { "loadfile", filename->buf, NULL };
    JF_MPV_ASSERT(mpv_command(g_mpv_ctx, loadfile));
    jf_growing_buffer_free(filename);

    // external subtitles
    // note: they unfortunately require loadfile to already have been issued
    subs_language[3] = '\0';
    for (i = 0; i < item->children_count; i++) {
        for (j = 0; j < item->children[i]->children_count; j++) {
            child = item->children[i]->children[j];
            if (child->type != JF_ITEM_TYPE_VIDEO_SUB) {
                fprintf(stderr,
                        "Warning: unrecognized item type (%s) for %s, part %zu, child %zu. This is a bug.\n",
                        jf_item_type_get_name(child->type),
                        item->name,
                        i,
                        j);
                continue;
            }
            tmp = jf_concat(2, g_options.server, child->name);
            strncpy(subs_language, child->id, 3);
            const char *command[] = { "sub-add",
                tmp,
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
            free(tmp);
        }
    }
}


void jf_playback_play_item(jf_menu_item *item)
{
    char *request_url;
    jf_reply *replies[2];

    if (item == NULL) {
        return;
    }

    if (JF_ITEM_TYPE_IS_FOLDER(item->type)) {
        fprintf(stderr, "Error: jf_menu_play_item invoked on folder item type. This is a bug.\n");
        return;
    }

    switch (item->type) {
        case JF_ITEM_TYPE_AUDIO:
        case JF_ITEM_TYPE_AUDIOBOOK:
            if ((request_url = jf_menu_item_get_request_url(item)) == NULL) {
                return;
            }
            jf_menu_ask_resume(item);
            JF_MPV_ASSERT(mpv_set_property_string(g_mpv_ctx, "title", item->name));
            const char *loadfile[] = { "loadfile", request_url, NULL };
            mpv_command(g_mpv_ctx, loadfile); 
            jf_menu_item_free(g_state.now_playing);
            g_state.now_playing = item;
            free(request_url);
            break;
        case JF_ITEM_TYPE_EPISODE:
        case JF_ITEM_TYPE_MOVIE:
            // check if item was already evaded re: split file and versions
            if (item->children_count > 0) {
                jf_menu_ask_resume(item);
                jf_playback_play_video(item);
            } else {
                request_url = jf_menu_item_get_request_url(item);
                replies[0] = jf_net_request(request_url,
                        JF_REQUEST_ASYNC_IN_MEMORY,
                        JF_HTTP_GET,
                        NULL);
                free(request_url);
                request_url = jf_concat(3, "/videos/", item->id, "/additionalparts");
                replies[1] = jf_net_request(request_url,
                        JF_REQUEST_IN_MEMORY,
                        JF_HTTP_GET,
                        NULL);
                free(request_url);
                if (JF_REPLY_PTR_HAS_ERROR(replies[1])) {
                    fprintf(stderr,
                            "Error: network request for /additionalparts of item %s failed: %s.\n",
                            item->name,
                            jf_reply_error_string(replies[1]));
                    jf_reply_free(replies[1]);
                    jf_reply_free(jf_net_await(replies[0]));
                    return;
                }
                if (JF_REPLY_PTR_HAS_ERROR(jf_net_await(replies[0]))) {
                    fprintf(stderr,
                            "Error: network request for item %s failed: %s.\n",
                            item->name,
                            jf_reply_error_string(replies[0]));
                    jf_reply_free(replies[0]);
                    jf_reply_free(replies[1]);
                    return;
                }
                jf_json_parse_video(item, replies[0]->payload, replies[1]->payload);
                jf_reply_free(replies[0]);
                jf_reply_free(replies[1]);
                jf_playback_populate_video_ticks(item);
                jf_menu_ask_resume(item);
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
            break;
    }
}


static inline void jf_playback_populate_video_ticks(jf_menu_item *item)
{
    jf_reply **replies;
    char *tmp;
    size_t i;

    if (item == NULL) return;
    if (item->type != JF_ITEM_TYPE_EPISODE
            && item->type != JF_ITEM_TYPE_MOVIE) return;

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
    assert((replies = malloc((item->children_count - 1) * sizeof(jf_menu_item *))) != NULL);
    for (i = 1; i < item->children_count; i++) {
        tmp = jf_concat(4,
                "/users/",
                g_options.userid,
                "/items/",
                item->children[i]->id);
        replies[i - 1] = jf_net_request(tmp,
                JF_REQUEST_ASYNC_IN_MEMORY,
                JF_HTTP_GET,
                NULL);
        free(tmp);
    }
    for (i = 1; i < item->children_count; i++) {
        jf_net_await(replies[i - 1]);
        jf_json_parse_playback_ticks(item->children[i], replies[i - 1]->payload);
        jf_reply_free(replies[i - 1]);
    }
    free(replies);
}
///////////////////////////////////


////////// PLAYLIST CONTROLS //////////
bool jf_playback_next()
{
    if (g_state.playlist_position < jf_disk_playlist_item_count()) {
        jf_playback_play_item(jf_disk_playlist_get_item(++g_state.playlist_position));
        return true;
    } else {
        return false;
    }
}


bool jf_playback_previous()
{
    if (g_state.playlist_position > 1) {
        jf_playback_play_item(jf_disk_playlist_get_item(--g_state.playlist_position));
        return true;
    } else {
        return false;
    }
}


void jf_playback_print_playlist(size_t slice_height)
{
    size_t i, low, high;
    size_t pos = g_state.playlist_position;

    if (slice_height == 0) {
        slice_height = jf_disk_playlist_item_count();
    }
    
    low = pos <= slice_height ? 1 : pos - slice_height;
    high = jf_clamp_zu(g_state.playlist_position + slice_height,
            pos,
            jf_disk_playlist_item_count());


    fprintf(stdout, "\n===== jftui playlist =====\n");
    for (i = low; i < pos; i++) {
        fprintf(stdout, "%zu: %s\n", i, jf_disk_playlist_get_item_name(i)); 
    }
    fprintf(stdout, "\t >>> %zu: %s <<<\n", i, g_state.now_playing->name);
    for (i = pos + 1; i <= high; i++) {
        fprintf(stdout, "%zu: %s\n", i, jf_disk_playlist_get_item_name(i));
    }
    fprintf(stdout, "\n");
}
///////////////////////////////////////
