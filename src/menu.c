#include "menu.h"
#include "shared.h"
#include "config.h"
#include "net.h"
#include "disk.h"
#include "playback.h"
#include "linenoise.h"

#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <assert.h>
#include <time.h>


////////// COMMAND PARSER //////////
#include "cmd.c"
////////////////////////////////////


////////// GLOBAL VARIABLES //////////
extern jf_options g_options;
extern jf_global_state g_state;
extern mpv_handle *g_mpv_ctx;
//////////////////////////////////////


////////// STATIC VARIABLES //////////
static jf_menu_item *s_root_menu = &(jf_menu_item){
        JF_ITEM_TYPE_MENU_ROOT,
        (jf_menu_item *[]){
            &(jf_menu_item){
                JF_ITEM_TYPE_MENU_FAVORITES,
                NULL,
                0,
                "",
                "Favorites",
                NULL,
                0, 0
            },
            &(jf_menu_item){
                JF_ITEM_TYPE_MENU_CONTINUE,
                NULL,
                0,
                "",
                "Continue Watching",
                NULL,
                0, 0
            },
            &(jf_menu_item){
                JF_ITEM_TYPE_MENU_NEXT_UP,
                NULL,
                0,
                "",
                "Next Up",
                NULL,
                0, 0
            },
            &(jf_menu_item){
                JF_ITEM_TYPE_MENU_LATEST_ADDED,
                NULL,
                0,
                "",
                "Latest Added",
                NULL,
                0, 0
            },
            &(jf_menu_item){
                JF_ITEM_TYPE_MENU_LIBRARIES,
                NULL,
                0,
                "",
                "User Views",
                NULL,
                0, 0
            }
        },
        5,
        "",
        "",
        NULL,
        0, 0
    };
static jf_menu_stack s_menu_stack = (jf_menu_stack){ 0 };
static jf_menu_item *s_context = NULL;

// PLAYED STATUS REQUESTS TRACKING
static jf_reply *s_played_status_requests[JF_PLAYED_STATUS_REQUESTS_LEN];
static struct timespec s_25msec = (struct timespec){ 0, 25 * 1000000 };

// FILTERS STUFF
static jf_filter_mask s_filters = JF_FILTER_NONE;
static jf_filter_mask s_filters_cmd = JF_FILTER_NONE;
static char s_filters_query[128];
static size_t s_filters_query_len;
//////////////////////////////////////


////////// STATIC FUNCTIONS //////////
// Pushes a jf_menu_item on the global menu stack. No-op if NULL is passed.
//
// Parameters:
//  menu_item - A pointer to the item to be pushed (if NULL, no-op).
// CAN FATAL.
static inline void jf_menu_stack_push(jf_menu_item *menu_item);


// Pops the top item out of the global menu stack.
// The caller assumes ownership of the popped item (i.e. will have to free it).
//
// Returns:
//  A pointer to the item popped or NULL if the stack is empty.
// CAN'T FAIL.
static inline jf_menu_item *jf_menu_stack_pop(void);


// Returns a const pointer to an item in a certain position on the stack without
// popping it.
//
// Parameters:
//  pos - The position to peek at. 0 is the top of the stack, 1 is the item
//      below the stack etc.
//
// Returns:
//  A const pointer to the requested item on the stack or NULL if the stack is
//  too short.
// CAN'T FAIL.
static inline const jf_menu_item *jf_menu_stack_peek(const size_t pos);

static inline void jf_menu_played_status_request_resolve(jf_reply *r);

static const char *jf_menu_filter_string(const jf_filter filter);
static bool jf_menu_item_type_allows_filter(const jf_item_type type, const jf_filter filter);
static bool jf_menu_filters_try_print(const bool first_filter, const jf_filter filter);
static void jf_menu_filters_print(void);
static bool jf_menu_filters_query_try_append(const bool first_filter, const jf_filter filter);
static void jf_menu_filters_apply(void);

static jf_menu_item *jf_menu_child_get(size_t n);
static bool jf_menu_print_context(void);
static bool jf_menu_ask_resume_yn(const jf_menu_item *item, const long long ticks);
static void jf_menu_try_play(void);
//////////////////////////////////////


////////// JF_MENU_STACK //////////
static inline void jf_menu_stack_push(jf_menu_item *menu_item)
{
    if (menu_item == NULL) {
        return;
    }

    assert(s_menu_stack.items != NULL);
    if (s_menu_stack.size == s_menu_stack.used) {
        s_menu_stack.size *= 2;
        assert((s_menu_stack.items = realloc(s_menu_stack.items,
                        s_menu_stack.size * sizeof(jf_menu_item *))) != NULL);
    }
    s_menu_stack.items[s_menu_stack.used++] = menu_item;
}


static inline jf_menu_item *jf_menu_stack_pop()
{
    jf_menu_item *retval;

    if (s_menu_stack.used == 0) {
        return NULL;
    }

    retval = s_menu_stack.items[--s_menu_stack.used];
    s_menu_stack.items[s_menu_stack.used] = NULL;
    return retval;
}


static inline const jf_menu_item *jf_menu_stack_peek(const size_t pos)
{
    return pos >= s_menu_stack.used ? NULL
        : s_menu_stack.items[s_menu_stack.used - pos - 1];
}
///////////////////////////////////


////////// QUERY FILTERS //////////
static const char *jf_menu_filter_string(const enum jf_filter filter)
{
    switch (filter) {
        case JF_FILTER_NONE:
            return "none";
        case JF_FILTER_FAVORITE:
            return "isFavorite";
        case JF_FILTER_IS_PLAYED:
            return "isPlayed";
        case JF_FILTER_IS_UNPLAYED:
            return "isUnPlayed";
        case JF_FILTER_RESUMABLE:
            return "isResumable";
        case JF_FILTER_LIKES:
            return "likes";
        case JF_FILTER_DISLIKES:
            return "dislikes";
    }
}


static bool jf_menu_item_type_allows_filter(const jf_item_type type,
        const jf_filter filter)
{
    switch (type) {
        case JF_ITEM_TYPE_NONE:
        case JF_ITEM_TYPE_AUDIO:
        case JF_ITEM_TYPE_AUDIOBOOK:
        case JF_ITEM_TYPE_EPISODE:
        case JF_ITEM_TYPE_MOVIE:
        case JF_ITEM_TYPE_MUSIC_VIDEO:
        case JF_ITEM_TYPE_VIDEO_SOURCE:
        case JF_ITEM_TYPE_VIDEO_SUB:
        case JF_ITEM_TYPE_MENU_ROOT:
        case JF_ITEM_TYPE_MENU_CONTINUE:
        case JF_ITEM_TYPE_MENU_NEXT_UP:
        case JF_ITEM_TYPE_MENU_LIBRARIES:
            // the last three are here due to bugs
            // for these two see issue #2687
        case JF_ITEM_TYPE_COLLECTION_MUSIC:
        case JF_ITEM_TYPE_ARTIST:
            // this last one instead is just a nice present from good ole emby
            // which sets the Played property correctly for seasons
            // but then considers everything unplayed when you query :)
        case JF_ITEM_TYPE_SERIES:
            return false;
        case JF_ITEM_TYPE_COLLECTION:
        case JF_ITEM_TYPE_COLLECTION_SERIES:
        case JF_ITEM_TYPE_COLLECTION_MOVIES:
        case JF_ITEM_TYPE_COLLECTION_MUSIC_VIDEOS:
        case JF_ITEM_TYPE_USER_VIEW:
        case JF_ITEM_TYPE_FOLDER:
        case JF_ITEM_TYPE_PLAYLIST:
        case JF_ITEM_TYPE_ALBUM:
        case JF_ITEM_TYPE_SEASON:
        case JF_ITEM_TYPE_SEARCH_RESULT:
            return true;
            break;
        case JF_ITEM_TYPE_MENU_FAVORITES:
            return filter != JF_FILTER_FAVORITE;
        case JF_ITEM_TYPE_MENU_LATEST_ADDED:
            return filter == JF_FILTER_IS_PLAYED || filter == JF_FILTER_IS_UNPLAYED;
    }
}


static bool jf_menu_filters_try_print(const bool first_filter,
        const jf_filter filter)
{
    if (filter == JF_FILTER_NONE) {
        return first_filter;
    }

    if (first_filter == false) {
        printf(", ");
    }
    printf("%s%s%s",
            jf_menu_item_type_allows_filter(s_context->type, filter) ? "" : "~",
            jf_menu_filter_string(filter),
            jf_menu_item_type_allows_filter(s_context->type, filter) ? "" : "~");

    return false;
}


static void jf_menu_filters_print(void)
{
    bool first_filter = true;

    if (s_filters == JF_FILTER_NONE) return;

    printf(" (");
    first_filter = jf_menu_filters_try_print(first_filter, s_filters & JF_FILTER_IS_PLAYED);
    first_filter = jf_menu_filters_try_print(first_filter, s_filters & JF_FILTER_IS_UNPLAYED);
    first_filter = jf_menu_filters_try_print(first_filter, s_filters & JF_FILTER_RESUMABLE);
    first_filter = jf_menu_filters_try_print(first_filter, s_filters & JF_FILTER_FAVORITE);
    first_filter = jf_menu_filters_try_print(first_filter, s_filters & JF_FILTER_LIKES);
    first_filter = jf_menu_filters_try_print(first_filter, s_filters & JF_FILTER_DISLIKES);
    printf(")");
}


static bool jf_menu_filters_query_try_append(const bool first_filter,
        const jf_filter filter)
{
    if (! (s_filters & filter)
            || ! jf_menu_item_type_allows_filter(s_context->type, filter)) {
        return first_filter;
    }

    if (first_filter) {
        s_filters_query_len += (size_t)snprintf(s_filters_query,
                sizeof(s_filters_query),
                "&filters=");
    } else {
        s_filters_query[s_filters_query_len] = ',';
        s_filters_query_len++;
    }
    s_filters_query_len += (size_t)snprintf(s_filters_query + s_filters_query_len,
            sizeof(s_filters_query) - s_filters_query_len,
            "%s",
            jf_menu_filter_string(filter));

    return false;
}


static void jf_menu_filters_apply(void)
{
    bool first_filter = s_context->type != JF_ITEM_TYPE_MENU_FAVORITES;

    s_filters = s_filters_cmd;
    s_filters_query[0] = '\0';
    s_filters_query_len = 0;

    if (s_filters == JF_FILTER_NONE) return;
    // special case due to Emby idiocy
    if (s_context->type == JF_ITEM_TYPE_MENU_LATEST_ADDED) return;

    first_filter = jf_menu_filters_query_try_append(first_filter, JF_FILTER_IS_PLAYED);
    first_filter = jf_menu_filters_query_try_append(first_filter, JF_FILTER_IS_UNPLAYED);
    first_filter = jf_menu_filters_query_try_append(first_filter, JF_FILTER_RESUMABLE);
    first_filter = jf_menu_filters_query_try_append(first_filter, JF_FILTER_FAVORITE);
    first_filter = jf_menu_filters_query_try_append(first_filter, JF_FILTER_LIKES);
    first_filter = jf_menu_filters_query_try_append(first_filter, JF_FILTER_DISLIKES);

    s_filters_query[s_filters_query_len] = '\0';
}
///////////////////////////////////


////////// USER INTERFACE LOOP //////////
char *jf_menu_item_get_request_url(const jf_menu_item *item)
{
    const jf_menu_item *parent;

    if (item == NULL) return NULL;

    switch (item->type) {
        // Atoms
        case JF_ITEM_TYPE_AUDIO:
        case JF_ITEM_TYPE_AUDIOBOOK:
            return jf_concat(4, g_options.server, "/items/", item->id, "/file");
        case JF_ITEM_TYPE_VIDEO_SOURCE:
            return jf_concat(5,
                    g_options.server,
                    "/videos/",
                    item->id,
                    "/stream?static=true&mediasourceid=",
                    item->id);
        case JF_ITEM_TYPE_EPISODE:
        case JF_ITEM_TYPE_MOVIE:
        case JF_ITEM_TYPE_MUSIC_VIDEO:
            return jf_concat(4, "/users/", g_options.userid, "/items/", item->id);
        case JF_ITEM_TYPE_VIDEO_SUB:
            return strdup(item->name);
        // Folders
        case JF_ITEM_TYPE_COLLECTION:
        case JF_ITEM_TYPE_FOLDER:
        case JF_ITEM_TYPE_ALBUM:
        case JF_ITEM_TYPE_SEASON:
        case JF_ITEM_TYPE_SERIES:
        case JF_ITEM_TYPE_COLLECTION_MUSIC_VIDEOS:
                return jf_concat(5,
                        "/users/",
                        g_options.userid,
                        "/items?sortby=isfolder,parentindexnumber,indexnumber,productionyear,sortname&parentid=",
                        item->id,
                        s_filters_query);
        case JF_ITEM_TYPE_COLLECTION_MUSIC:
            if ((parent = jf_menu_stack_peek(0)) != NULL && parent->type == JF_ITEM_TYPE_FOLDER) {
                // we are inside a "by folders" view
                return jf_concat(5,
                        "/users/",
                        g_options.userid,
                        "/items?sortby=isfolder,sortname&parentid=",
                        item->id,
                        s_filters_query);
            } else {
                return jf_concat(5,
                        "/artists/albumartists?parentid=",
                        item->id,
                        "&userid=",
                        g_options.userid,
                        s_filters_query);
            }
        case JF_ITEM_TYPE_COLLECTION_SERIES:
            return jf_concat(5,
                    "/users/",
                    g_options.userid,
                    "/items?includeitemtypes=series&recursive=true&sortby=isfolder,sortname&parentid=",
                    item->id,
                    s_filters_query);
        case JF_ITEM_TYPE_COLLECTION_MOVIES:
            return jf_concat(5,
                    "/users/",
                    g_options.userid,
                    "/items?includeitemtypes=Movie&recursive=true&sortby=isfolder,sortname&parentid=",
                    item->id,
                    s_filters_query);
        case JF_ITEM_TYPE_PLAYLIST:
            return jf_concat(4,
                    "/playlists/",
                    item->id,
                    "/items?userid=",
                    g_options.userid);
        case JF_ITEM_TYPE_ARTIST:
            return jf_concat(5,
                    "/users/",
                    g_options.userid,
                    "/items?recursive=true&includeitemtypes=musicalbum&sortby=isfolder,productionyear,sortname&sortorder=ascending&albumartistids=",
                    item->id,
                    s_filters_query);
        case JF_ITEM_TYPE_SEARCH_RESULT:
            return jf_concat(5,
                    "/users/",
                    g_options.userid,
                    "/items?recursive=true&searchterm=",
                    item->name,
                    s_filters_query);
        // Persistent folders
        case JF_ITEM_TYPE_MENU_FAVORITES:
            return jf_concat(4,
                    "/users/",
                    g_options.userid,
                    "/items?recursive=true&sortby=sortname&filters=isfavorite",
                    s_filters_query);
        case JF_ITEM_TYPE_MENU_CONTINUE:
            return jf_concat(3,
                    "/users/",
                    g_options.userid,
                    "/items/resume?recursive=true");
        case JF_ITEM_TYPE_MENU_NEXT_UP:
            return jf_concat(3, "/shows/nextup?userid=", g_options.userid, "&limit=15");
        case JF_ITEM_TYPE_MENU_LATEST_ADDED:
            if (s_filters & JF_FILTER_IS_PLAYED) {
                return jf_concat(3,
                        "/users/",
                        g_options.userid,
                        "/items/latest?recurisve=true&groupitems=true&includeitemtypes=audiobook,episode,movie,audio&limit=20&isplayed=true");
            }
            if (s_filters & JF_FILTER_IS_UNPLAYED) {
                return jf_concat(3,
                        "/users/",
                        g_options.userid,
                        "/items/latest?recurisve=true&groupitems=true&includeitemtypes=audiobook,episode,movie,audio&limit=20&isplayed=false");
            }
            return jf_concat(3,
                    "/users/",
                    g_options.userid,
                    "/items?recursive=true&includeitemtypes=audiobook,episode,movie,musicalbum&excludelocationtypes=virtual&sortby=datecreated,sortname&sortorder=descending&limit=20");
        case JF_ITEM_TYPE_MENU_LIBRARIES:
            return jf_concat(3, "/users/", g_options.userid, "/views");
        default:
            fprintf(stderr,
                    "Error: get_request_url was called on an unsupported item_type (%d). This is a bug.\n",
                    item->type);
            return NULL;
    }
}


static jf_menu_item *jf_menu_child_get(size_t n)
{
    if (s_context == NULL) return NULL;

    if (JF_ITEM_TYPE_HAS_DYNAMIC_CHILDREN(s_context->type)) {
        return jf_disk_payload_get_item(n);
    } else {
        return n - 1 <= s_context->children_count ? s_context->children[n - 1]
            : NULL;
    }
}


static bool jf_menu_print_context()
{
    size_t i;
    jf_request_type request_type = JF_REQUEST_SAX;
    jf_reply *reply;
    char *request_url;

    if (s_context == NULL) {
        fprintf(stderr, "Error: jf_menu_print_context: s_context == NULL. This is a bug.\n");
        return false;
    }

    switch (s_context->type) {
        // DYNAMIC FOLDERS: fetch children, parser prints entries
        case JF_ITEM_TYPE_COLLECTION:
        case JF_ITEM_TYPE_USER_VIEW:
        case JF_ITEM_TYPE_FOLDER:
        case JF_ITEM_TYPE_PLAYLIST:
        case JF_ITEM_TYPE_MENU_FAVORITES:
        case JF_ITEM_TYPE_MENU_CONTINUE:
        case JF_ITEM_TYPE_MENU_NEXT_UP:
        case JF_ITEM_TYPE_MENU_LATEST_ADDED:
        case JF_ITEM_TYPE_MENU_LIBRARIES:
        case JF_ITEM_TYPE_SEARCH_RESULT:
            request_type = JF_REQUEST_SAX_PROMISCUOUS;
            // no break
        case JF_ITEM_TYPE_COLLECTION_MUSIC:
        case JF_ITEM_TYPE_COLLECTION_SERIES:
        case JF_ITEM_TYPE_COLLECTION_MOVIES:
        case JF_ITEM_TYPE_COLLECTION_MUSIC_VIDEOS:
        case JF_ITEM_TYPE_ARTIST:
        case JF_ITEM_TYPE_ALBUM:
        case JF_ITEM_TYPE_SEASON:
        case JF_ITEM_TYPE_SERIES:
            printf("\n===== %s", s_context->name);
            jf_menu_filters_print();
            printf(" =====\n");
            if ((request_url = jf_menu_item_get_request_url(s_context)) == NULL) {
                jf_menu_item_free(s_context);
                return false;
            }
            JF_DEBUG_PRINTF("%s URL: %s\n",
                    jf_item_type_get_name(s_context->type),
                    request_url);
            reply = jf_net_request(request_url, request_type, JF_HTTP_GET, NULL);
            free(request_url);
            if (JF_REPLY_PTR_HAS_ERROR(reply)) {
                jf_menu_item_free(s_context);
                fprintf(stderr, "Error: %s.\n", jf_reply_error_string(reply));
                jf_reply_free(reply);
                jf_thread_buffer_clear_error();
                return false;
            }
            jf_reply_free(reply);
            jf_menu_stack_push(s_context);
            break;
        // PERSISTENT FOLDERS
        case JF_ITEM_TYPE_MENU_ROOT:
            printf("\n===== %s", s_context->name);
            jf_menu_filters_print();
            printf(" =====\n");
            for (i = 0; i < s_context->children_count; i++) {
                printf("D %zu: %s\n", i + 1, s_context->children[i]->name);
            }
            // push on stack to allow backtracking
            jf_menu_stack_push(s_context);
            break;
        default:
            fprintf(stderr,
                    "Error: jf_menu_dispatch_context unsupported menu item type (%s). This is a bug.\n",
                    jf_item_type_get_name(s_context->type));
            jf_menu_item_free(s_context);
            return false;
    }

    return true;
}


static bool jf_menu_ask_resume_yn(const jf_menu_item *item, const long long ticks)
{
    char *timestamp;
    char *question;
    bool go_on = true;

    if (ticks == 0) return go_on;

    timestamp = jf_make_timestamp(ticks);
    question = jf_concat(5,
                    "\nWould you like to resume ",
                    item->name,
                    " at the ",
                    timestamp,
                    " mark?");
    switch (jf_menu_user_ask_ync(question)) {
        case JF_YNC_YES:
            JF_MPV_ASSERT(mpv_set_property_string(g_mpv_ctx, "start", timestamp));
            g_state.state = JF_STATE_PLAYBACK_START_MARK;
            // control flow fall-through
        case JF_YNC_NO:
            break;
        case JF_YNC_CANCEL:
            go_on = false;
            break;
    }

    free(timestamp);
    free(question);

    return go_on;
}


bool jf_menu_ask_resume(jf_menu_item *item)
{
    char **timestamps;
    long long ticks;
    size_t i, j, markers_count;

    assert(item != NULL);

    if (item->children_count == 0) {
        return item->playback_ticks != 0 ?
            jf_menu_ask_resume_yn(item, item->playback_ticks) : true;
    }

    markers_count = 0;
    for (i = 0; i < item->children_count; i++) {
        if (item->children[i]->playback_ticks != 0) {
            markers_count++;
        }
    }
    if (markers_count == 0) return true;
    if (markers_count == 1) {
        i = 0;
        ticks = 0;
        while (item->children[i]->playback_ticks == 0
                && i < item->children_count - 1) {
            ticks += item->children[i]->runtime_ticks;
            i++;
        }
        ticks += item->children[i]->playback_ticks;
        return jf_menu_ask_resume_yn(item, ticks);
    }
    assert((timestamps = malloc(markers_count * sizeof(char *))) != NULL);
    ticks = 0;
    j = 2;
    printf("\n%s is a split-file on the server and there is progress marked on more than one part.\n",
            item->name);
    printf("Please choose at what time you'd like to start watching:\n");
    printf("1: 00:00:00\n");
    for (i = 0; i < item->children_count; i++) {
        if (item->children[i]->playback_ticks != 0) {
            ticks += item->children[i]->playback_ticks;
            timestamps[j - 2] = jf_make_timestamp(ticks);
            printf("%zu: %s\n", j, timestamps[j - 2]);
            ticks += item->children[i]->runtime_ticks - item->children[i]->playback_ticks;
            j++;
        } else {
            ticks += item->children[i]->runtime_ticks;
        }
    }
    printf("%zu: Cancel\n", markers_count + 2);
    j = jf_menu_user_ask_selection(1, markers_count + 2);
    if (j != 1 && j != markers_count + 2){
        JF_MPV_ASSERT(mpv_set_property_string(g_mpv_ctx, "start", timestamps[j - 2]));
    }
    for (i = 0; i < markers_count; i++) {
        free(timestamps[i]);
    }
    free(timestamps);

    if (j == markers_count + 2) return false;

    g_state.state = JF_STATE_PLAYBACK_START_MARK;
    return true;
}


static void jf_menu_try_play()
{
    jf_menu_item *item;
    int mpv_flag_yes = 1;
    char *x_emby_token;

    if (jf_disk_playlist_item_count() == 0) return;

    // init mpv core
    assert((g_mpv_ctx = mpv_create()) != NULL);
    JF_MPV_ASSERT(JF_MPV_SET_OPTPROP(g_mpv_ctx, "config-dir", MPV_FORMAT_STRING, &g_state.config_dir));
    JF_MPV_ASSERT(JF_MPV_SET_OPTPROP(g_mpv_ctx, "config", MPV_FORMAT_FLAG, &mpv_flag_yes));
    JF_MPV_ASSERT(JF_MPV_SET_OPTPROP(g_mpv_ctx, "osc", MPV_FORMAT_FLAG, &mpv_flag_yes));
    JF_MPV_ASSERT(JF_MPV_SET_OPTPROP(g_mpv_ctx, "input-default-bindings", MPV_FORMAT_FLAG, &mpv_flag_yes));
    JF_MPV_ASSERT(JF_MPV_SET_OPTPROP(g_mpv_ctx, "input-vo-keyboard", MPV_FORMAT_FLAG, &mpv_flag_yes));
    JF_MPV_ASSERT(JF_MPV_SET_OPTPROP(g_mpv_ctx, "input-terminal", MPV_FORMAT_FLAG, &mpv_flag_yes));
    assert((x_emby_token = jf_concat(2, "x-emby-token: ", g_options.token)) != NULL);
    JF_MPV_ASSERT(JF_MPV_SET_OPTPROP_STRING(g_mpv_ctx, "http-header-fields", x_emby_token));
    free(x_emby_token);
    JF_MPV_ASSERT(mpv_observe_property(g_mpv_ctx, 0, "time-pos", MPV_FORMAT_INT64));
    JF_MPV_ASSERT(mpv_observe_property(g_mpv_ctx, 0, "sid", MPV_FORMAT_INT64));
    JF_MPV_ASSERT(mpv_observe_property(g_mpv_ctx, 0, "options/loop-playlist", MPV_FORMAT_NODE));

    JF_MPV_ASSERT(mpv_initialize(g_mpv_ctx));

    // profile must be applied as a command
    if (g_options.mpv_profile != NULL) {
        const char *apply_profile[] = { "apply-profile", g_options.mpv_profile, NULL };
        if (mpv_command(g_mpv_ctx, apply_profile) < 0) {
            fprintf(stderr,
                    "FATAL: could not apply mpv profile \"%s\". Are you sure it exists in mpv.conf?\n",
                    g_options.mpv_profile);
            jf_exit(JF_EXIT_FAILURE);
        }
    }

    // set global application state
    g_state.state = JF_STATE_PLAYBACK_INIT;

    // reset playlist
    g_state.playlist_loops = 0;
    g_state.loop_state = JF_LOOP_STATE_IN_SYNC;

    // actually try and play
    item = jf_disk_playlist_get_item(1);
    g_state.playlist_position = 1;
    if (jf_playback_play_item(item) == false) return;
#ifdef JF_DEBUG
    jf_menu_item_print(item);
#endif
    if (g_mpv_ctx != NULL) {
        JF_MPV_ASSERT(JF_MPV_SET_OPTPROP(g_mpv_ctx, "terminal", MPV_FORMAT_FLAG, &mpv_flag_yes));
    }
}


jf_item_type jf_menu_child_get_type(size_t n)
{
    if (s_context == NULL) return JF_ITEM_TYPE_NONE;

    if (JF_ITEM_TYPE_HAS_DYNAMIC_CHILDREN(s_context->type)) {
        return jf_disk_payload_get_type(n);
    } else {
        return n - 1 < s_context->children_count ?
            s_context->children[n - 1]->type : JF_ITEM_TYPE_NONE;
    }
}


bool jf_menu_child_dispatch(size_t n)
{
    jf_menu_item *child = jf_menu_child_get(n);

    if (child == NULL) return true;

    switch (child->type) {
        // ATOMS: add to playlist
        case JF_ITEM_TYPE_AUDIO:
        case JF_ITEM_TYPE_AUDIOBOOK:
        case JF_ITEM_TYPE_EPISODE:
        case JF_ITEM_TYPE_MOVIE:
        case JF_ITEM_TYPE_MUSIC_VIDEO:
            jf_disk_playlist_add_item(child);
            jf_menu_item_free(child);
            break;
        // FOLDERS: push on stack
        case JF_ITEM_TYPE_COLLECTION:
        case JF_ITEM_TYPE_USER_VIEW:
        case JF_ITEM_TYPE_FOLDER:
        case JF_ITEM_TYPE_PLAYLIST:
        case JF_ITEM_TYPE_MENU_FAVORITES:
        case JF_ITEM_TYPE_MENU_CONTINUE:
        case JF_ITEM_TYPE_MENU_NEXT_UP:
        case JF_ITEM_TYPE_MENU_LATEST_ADDED:
        case JF_ITEM_TYPE_MENU_LIBRARIES:
        case JF_ITEM_TYPE_COLLECTION_MUSIC:
        case JF_ITEM_TYPE_COLLECTION_SERIES:
        case JF_ITEM_TYPE_COLLECTION_MOVIES:
        case JF_ITEM_TYPE_COLLECTION_MUSIC_VIDEOS:
        case JF_ITEM_TYPE_ARTIST:
        case JF_ITEM_TYPE_ALBUM:
        case JF_ITEM_TYPE_SEASON:
        case JF_ITEM_TYPE_SERIES:
            jf_menu_stack_push(child);
            break;
        default:
            fprintf(stderr,
                    "Error: jf_menu_child_dispatch unsupported menu item type (%d) for item %zu. This is a bug.\n",
                    child->type,
                    n);
            jf_menu_item_free(child);
            return false;
    }

    return true;
}


size_t jf_menu_child_count()
{
    if (s_context == NULL) return 0;

    if (JF_ITEM_TYPE_HAS_DYNAMIC_CHILDREN(s_context->type)) {
        return jf_disk_payload_item_count();
    } else {
        return s_context->children_count;
    }
}


void jf_menu_dotdot()
{
    jf_menu_item *menu_item = jf_menu_stack_pop();

    if (menu_item == NULL) return;

    if (menu_item->type == JF_ITEM_TYPE_MENU_ROOT) {
        // root entry should be pushed back to not cause memory leaks due to its children
        jf_menu_stack_push(menu_item);
    } else {
        jf_menu_item_free(menu_item);
    }
}


void jf_menu_quit()
{
    g_state.state = JF_STATE_USER_QUIT;
}


////////// PLAYED STATUS //////////
static inline void jf_menu_played_status_request_resolve(jf_reply *r)
// TODO save the menu index contextually with the request so we can fetch item name
{
    jf_net_await(r);
    if (JF_REPLY_PTR_HAS_ERROR(r)) {
        fprintf(stderr,
                "Warning: played status of item could not be updated: %s.\n",
                jf_reply_error_string(r));
    }
    jf_reply_free(r);
}


void jf_menu_child_mark_played(const size_t n, const jf_played_status status)
{
    jf_menu_item *child;
    char *url;
    size_t i;

    if ((child = jf_menu_child_get(n)) == NULL) return;

    url = jf_concat(4, "/users/", g_options.userid, "/playeditems/", child->id);

    // look for next clear spot
    for (i = 0; i < sizeof(s_played_status_requests) / sizeof(*s_played_status_requests); i++) {
        if (s_played_status_requests[i] == NULL) break;
    }

    while (i >= sizeof(s_played_status_requests) / sizeof(*s_played_status_requests)
                || s_played_status_requests[i] != NULL) {
        // the buffer is full
        for (i = 0; i < sizeof(s_played_status_requests) / sizeof(*s_played_status_requests); i++) {
            if (! JF_REPLY_PTR_IS_PENDING(s_played_status_requests[i])) {
                jf_menu_played_status_request_resolve(s_played_status_requests[i]);
                s_played_status_requests[i] = NULL;
                break;
            }
        }
        // take a nap and try again
        // a precise notification mechanism would be hugely overkill
        // this subsystem is overengineered enough
        nanosleep(&s_25msec, NULL);
        // don't bother dealing with early wakeup
    }
            
    s_played_status_requests[i] = jf_net_request(url,
            JF_REQUEST_ASYNC_IN_MEMORY,
            status == JF_PLAYED_STATUS_YES ? JF_HTTP_POST : JF_HTTP_DELETE,
            NULL);

    free(url);
    free(child);
}


void jf_menu_item_mark_played_detach(const jf_menu_item *item, const jf_played_status status)
{
    char *url = jf_concat(4, "/users/", g_options.userid, "/playeditems/", item->id);
    jf_net_request(url,
            JF_REQUEST_ASYNC_DETACH,
            status == JF_PLAYED_STATUS_YES ? JF_HTTP_POST : JF_HTTP_DELETE,
            NULL);
}


void jf_menu_item_mark_played_await_all(void)
{
    size_t i;

    for (i = 0; i < sizeof(s_played_status_requests) / sizeof (*s_played_status_requests); i++) {
        if (s_played_status_requests[i] == NULL) continue;
        jf_menu_played_status_request_resolve(s_played_status_requests[i]);
        s_played_status_requests[i] = NULL;
    }
}
///////////////////////////////////


void jf_menu_filters_clear()
{
    s_filters_cmd = JF_FILTER_NONE;
}


bool jf_menu_filters_add(const enum jf_filter filter)
{
    s_filters_cmd |= filter;

    // check for contradictory filters
    if (s_filters_cmd & JF_FILTER_IS_PLAYED && s_filters_cmd & JF_FILTER_IS_UNPLAYED) {
        fprintf(stderr,
                "Error: filters \"isPlayed\" and \"isUnPlayed\" are incompatible.\n");
        s_filters_cmd = s_filters;
        return false;
    }
    if (s_filters_cmd & JF_FILTER_LIKES && s_filters_cmd & JF_FILTER_DISLIKES) {
        fprintf(stderr,
                "Error: filters \"likes\" and \"dislikes\" are incompatible.\n");
        s_filters_cmd = s_filters;
        return false;
    }

    return true;
}


void jf_menu_search(const char *s)
{
    jf_menu_item *menu_item;
    char *escaped;

    escaped = jf_net_urlencode(s);
    menu_item = jf_menu_item_new(JF_ITEM_TYPE_SEARCH_RESULT,
            NULL,
            NULL,
            escaped,
            NULL,
            0, 0);
    free(escaped);
    jf_menu_stack_push(menu_item);
}


void jf_menu_ui()
{
    yycontext yy;
    char *line = NULL;

    // ACQUIRE ITEM CONTEXT
    if ((s_context = jf_menu_stack_pop()) == NULL) {
        // expected on first run
        // in case of error it's a solid fallback
        s_context = s_root_menu;
    }

    // APPLY FILTERS
    jf_menu_filters_apply();

    while (true) {
        // CLEAR DISK CACHE
        jf_disk_refresh();

        // PRINT MENU
        if (! jf_menu_print_context()) {
            return;
        }
        // READ AND PROCESS USER COMMAND
        memset(&yy, 0, sizeof(yycontext));
        while (true) {
            switch (yy_cmd_get_parser_state(&yy)) {
                case JF_CMD_VALIDATE_START:
                    // read input and do first pass (validation)
                    line = jf_menu_linenoise("> ");
                    linenoiseHistoryAdd(line);
                    yy.input = line;
                    yyparse(&yy);
                    break;
                case JF_CMD_VALIDATE_OK:
                    // reset parser but preserve state and input for second pass (dispatch)
                    yyrelease(&yy);
                    memset(&yy, 0, sizeof(yycontext));
                    yy.state = JF_CMD_VALIDATE_OK;
                    yy.input = line;
                    yyparse(&yy);
                    break;
                case JF_CMD_SUCCESS:
                    free(line);
                    yyrelease(&yy);
                    jf_menu_try_play();
                    return;
                case JF_CMD_FAIL_FOLDER:
                    fprintf(stderr, "Error: cannot open many folders or both folders and items with non-recursive command.\n");
                    free(line);
                    yyrelease(&yy);
                    memset(&yy, 0, sizeof(yycontext));
                    break;
                case JF_CMD_FAIL_SYNTAX:
                    fprintf(stderr, "Error: malformed command.\n");
                    // no break
                case JF_CMD_FAIL_SPECIAL:
                    free(line);
                    yyrelease(&yy);
                    memset(&yy, 0, sizeof(yycontext));
                    break;
                case JF_CMD_FAIL_DISPATCH:
                    // exit silently
                    free(line);
                    yyrelease(&yy);
                    return;
                default:
                    fprintf(stderr, "Error: command parser ended in unexpected state. This is a bug.\n");
                    free(line);
                    yyrelease(&yy);
                    memset(&yy, 0, sizeof(yycontext));
                    break;
            }
        }
    }
}
/////////////////////////////////////////


////////// AGNOSTIC USER PROMPTS //////////
bool jf_menu_user_ask_yn(const char *question)
{
    char *str;

    printf("%s [y/n]\n", question);
    while (true) {
        str = jf_menu_linenoise("> ");
        if (strcasecmp(str, "y") == 0) return true;
        if (strcasecmp(str, "n") == 0) return false;
        printf("Error: please answer \"y\" or \"n\".\n");
    }
}


enum jf_ync jf_menu_user_ask_ync(const char *question)
{
    char *str;

    printf("%s [y/n/c]\n", question);
    while (true) {
        str = jf_menu_linenoise("> ");
        if (strcasecmp(str, "y") == 0) return JF_YNC_YES;
        if (strcasecmp(str, "n") == 0) return JF_YNC_NO;
        if (strcasecmp(str, "c") == 0) return JF_YNC_CANCEL;
        printf("Error: please answer \"y\", \"n\" or \"c\".\n");
    }
}


size_t jf_menu_user_ask_selection(const size_t l, const size_t r)
{
    char *tmp;
    size_t i;

    // read the number, kronk
    while (true) {
        tmp = jf_menu_linenoise("> ");
        if (sscanf(tmp, " %zu ", &i) == 1 && l <= i && i <= r) {
            free(tmp);
            return i;
        }
        // wrong numbeeeeeer...
        fprintf(stderr, "Error: please choose exactly one listed item.\n");
    }
}
///////////////////////////////////////////


////////// MISCELLANEOUS //////////
void jf_menu_init()
{
    size_t i;

    // all linenoise setup
    linenoiseHistorySetMaxLen(16);
    
    // update server name
    s_root_menu->name = g_state.server_name;

    // init menu stack
    assert((s_menu_stack.items = malloc(10 * sizeof(jf_menu_item *))) != NULL);
    s_menu_stack.size = 10;
    s_menu_stack.used = 0;

    // init played status tracker
    for (i = 0; i < JF_PLAYED_STATUS_REQUESTS_LEN; i++) {
        s_played_status_requests[i] = NULL;
    }
}


void jf_menu_clear()
{
    // clear menu stack
    while (s_menu_stack.used > 0) {
        jf_menu_item_free(jf_menu_stack_pop());
    }
}


char *jf_menu_linenoise(const char *prompt)
{
    char *str;
    if ((str = linenoise(prompt)) == NULL) {
        if (errno != EAGAIN) {
            perror("FATAL: jf_menu_linenoise");
        }
        jf_exit(JF_EXIT_FAILURE);
    }
    return str;
}
///////////////////////////////////
