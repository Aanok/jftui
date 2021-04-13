#include "json.h"
#include "config.h"
#include "shared.h"
#include "menu.h"
#include "disk.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <assert.h>
#include <yajl/yajl_parse.h>
#include <yajl/yajl_tree.h>
#include <yajl/yajl_gen.h>


////////// GLOBALS //////////
extern jf_options g_options;
extern jf_global_state g_state;
/////////////////////////////


////////// STATIC VARIABLES //////////
static char s_error_buffer[JF_PARSER_ERROR_BUFFER_SIZE];
//////////////////////////////////////


////////// STATIC FUNCTIONS //////////
static int jf_sax_items_start_map(void *ctx);
static int jf_sax_items_end_map(void *ctx);
static int jf_sax_items_map_key(void *ctx, const unsigned char *key, size_t key_len);
static int jf_sax_items_start_array(void *ctx);
static int jf_sax_items_end_array(void *ctx);
static int jf_sax_items_string(void *ctx, const unsigned char *string, size_t strins_len);
static int jf_sax_items_number(void *ctx, const char *string, size_t strins_len);

// Allocates a new yajl parser instance, registering callbacks and context and
// setting yajl_allow_multiple_values to let it digest multiple JSON messages
// in a row.
// Failures cause SIGABRT.
//
// Parameters:
//  - callbacks: Pointer to callbacks struct to register.
//  - context: Pointer to json parser context to register.
//
// Returns:
//  The yajl_handle of the new parser.
static inline yajl_handle jf_sax_yajl_parser_new(yajl_callbacks *callbacks, jf_sax_context *context);

static inline bool jf_sax_current_item_is_valid(const jf_sax_context *context);
static inline void jf_sax_current_item_make_and_print_name(jf_sax_context *context);
static inline void jf_sax_context_init(jf_sax_context *context, jf_thread_buffer *tb);
static inline void jf_sax_context_current_item_clear(jf_sax_context *context);

// DO NOT USE THIS! Call the macro with the same name sans leading __
static inline yajl_val __jf_yajl_tree_get_assert(const int lineno,
        yajl_val parent,
        const char **path,
        yajl_type type);

static jf_menu_item *jf_json_parse_versions(const jf_menu_item *item, const yajl_val media_sources);
//////////////////////////////////////


////////// SAX PARSER CALLBACKS //////////
static int jf_sax_items_start_map(void *ctx)
{
    jf_sax_context *context = (jf_sax_context *)(ctx);
    switch (context->parser_state) {
        case JF_SAX_IDLE:
            context->tb->item_count = 0;
            jf_sax_context_current_item_clear(context);
            jf_disk_refresh();
            context->parser_state = JF_SAX_IN_QUERYRESULT_MAP;
            break;
        case JF_SAX_IN_LATEST_ARRAY:
            context->latest_array = true;
            context->parser_state = JF_SAX_IN_ITEM_MAP;
            break;
        case JF_SAX_IN_ITEMS_ARRAY:
            context->parser_state = JF_SAX_IN_ITEM_MAP;
            break;
        case JF_SAX_IN_USERDATA_VALUE:
            context->parser_state = JF_SAX_IN_USERDATA_MAP;
            break;
        case JF_SAX_IN_ITEM_MAP:
            context->state_to_resume = JF_SAX_IN_ITEM_MAP;
            context->parser_state = JF_SAX_IGNORE;
            context->maps_ignoring = 1;
            break;
        case JF_SAX_IGNORE:
            context->maps_ignoring++;
            break;
        default:
            JF_SAX_BAD_STATE();
    }
    return 1;
}


static int jf_sax_items_end_map(void *ctx)
{
    jf_sax_context *context = (jf_sax_context *)(ctx);
    switch (context->parser_state) {
        case JF_SAX_IN_QUERYRESULT_MAP:
            context->parser_state = JF_SAX_IDLE;
            break;
        case JF_SAX_IN_ITEMS_VALUE:
            context->parser_state = JF_SAX_IN_QUERYRESULT_MAP;
            break;
        case JF_SAX_IN_USERDATA_MAP:
            context->parser_state = JF_SAX_IN_ITEM_MAP;
            break;
        case JF_SAX_IN_ITEM_MAP:
            if (jf_sax_current_item_is_valid(context)) {
                context->tb->item_count++;
                jf_sax_current_item_make_and_print_name(context);

                jf_menu_item *item = jf_menu_item_new(context->current_item_type,
                        NULL,
                        (const char*)(context->parsed_content.buf
                            + context->id_start),
                        context->current_item_display_name.buf,
                        context->runtime_ticks,
                        context->playback_ticks);
                jf_disk_payload_add_item(item);
                jf_menu_item_free(item);
            }
            jf_sax_context_current_item_clear(context);

            if (context->latest_array) {
                context->parser_state = JF_SAX_IN_LATEST_ARRAY;
                context->latest_array = false;
            } else {
                context->parser_state = JF_SAX_IN_ITEMS_ARRAY;
            }
            break;
        case JF_SAX_IGNORE:
            context->maps_ignoring--;
            if (context->maps_ignoring == 0 && context->arrays_ignoring == 0) {
                context->parser_state = context->state_to_resume;
                context->state_to_resume = JF_SAX_NO_STATE;
            }
        default:
            break;
    }
    return 1;
}


static int jf_sax_items_map_key(void *ctx, const unsigned char *key, size_t key_len)
{
    jf_sax_context *context = (jf_sax_context *)(ctx);
    switch (context->parser_state) {
        case JF_SAX_IN_QUERYRESULT_MAP:
            if (JF_SAX_KEY_IS("Items")) {
                context->parser_state = JF_SAX_IN_ITEMS_VALUE;
            }
            break;
        case JF_SAX_IN_ITEM_MAP:
            if (JF_SAX_KEY_IS("Name")) {
                context->parser_state = JF_SAX_IN_ITEM_NAME_VALUE;
            } else if (JF_SAX_KEY_IS("Type")) {
                context->parser_state = JF_SAX_IN_ITEM_TYPE_VALUE;
            } else if (JF_SAX_KEY_IS("CollectionType")) {
                context->parser_state = JF_SAX_IN_ITEM_COLLECTION_TYPE_VALUE;
            } else if (JF_SAX_KEY_IS("Id")) {
                context->parser_state = JF_SAX_IN_ITEM_ID_VALUE;
            } else if (JF_SAX_KEY_IS("AlbumArtist")) {
                context->parser_state = JF_SAX_IN_ITEM_ALBUMARTIST_VALUE;
            } else if (JF_SAX_KEY_IS("Album")) {
                context->parser_state = JF_SAX_IN_ITEM_ALBUM_VALUE;
            } else if (JF_SAX_KEY_IS("SeriesName")) {
                context->parser_state = JF_SAX_IN_ITEM_SERIES_VALUE;
            } else if (JF_SAX_KEY_IS("ProductionYear")) {
                context->parser_state = JF_SAX_IN_ITEM_YEAR_VALUE;
            } else if (JF_SAX_KEY_IS("IndexNumber")) {
                context->parser_state = JF_SAX_IN_ITEM_INDEX_VALUE;
            } else if (JF_SAX_KEY_IS("ParentIndexNumber")) {
                context->parser_state = JF_SAX_IN_ITEM_PARENT_INDEX_VALUE;
            } else if (JF_SAX_KEY_IS("RunTimeTicks")) {
                context->parser_state = JF_SAX_IN_ITEM_RUNTIME_TICKS_VALUE;
            } else if (JF_SAX_KEY_IS("UserData")) {
                context->parser_state = JF_SAX_IN_USERDATA_VALUE;
            }
            break;
        case JF_SAX_IN_USERDATA_MAP:
            if (JF_SAX_KEY_IS("PlaybackPositionTicks")) {
                context->parser_state = JF_SAX_IN_USERDATA_TICKS_VALUE;
            }
        default:
            break;
    }
    return 1;
}

static int jf_sax_items_start_array(void *ctx)
{
    jf_sax_context *context = (jf_sax_context *)(ctx);
    switch (context->parser_state) {
        case JF_SAX_IDLE:
            context->parser_state = JF_SAX_IN_LATEST_ARRAY;
            context->tb->item_count = 0;
            jf_sax_context_current_item_clear(context);
            break;
        case JF_SAX_IN_ITEMS_VALUE:
            context->parser_state = JF_SAX_IN_ITEMS_ARRAY;
            break;
        case JF_SAX_IN_ITEM_MAP:
            context->parser_state = JF_SAX_IGNORE;
            context->state_to_resume = JF_SAX_IN_ITEM_MAP;
            context->arrays_ignoring = 1;
            break;
        case JF_SAX_IGNORE:
            context->arrays_ignoring++;
            break;
        default:
            JF_SAX_BAD_STATE();
    }
    return 1;
}


static int jf_sax_items_end_array(void *ctx)
{
    jf_sax_context *context = (jf_sax_context *)(ctx);
    switch (context->parser_state) {
        case JF_SAX_IN_LATEST_ARRAY:
            context->parser_state = JF_SAX_IDLE;
            break;
        case JF_SAX_IN_ITEMS_ARRAY:
            context->parser_state = JF_SAX_IN_QUERYRESULT_MAP;
            break;
        case JF_SAX_IGNORE:
            context->arrays_ignoring--;
            if (context->arrays_ignoring == 0 && context->maps_ignoring == 0) {
                context->parser_state = context->state_to_resume;
                context->state_to_resume = JF_SAX_NO_STATE;
            }
            break;
        default:
            JF_SAX_BAD_STATE();
    }
    return 1;
}


static int jf_sax_items_string(void *ctx, const unsigned char *string, size_t string_len)
{
    jf_sax_context *context = (jf_sax_context *)(ctx);
    switch (context->parser_state) {
        case JF_SAX_IN_ITEM_NAME_VALUE:
            JF_SAX_ITEM_FILL(name);
            context->parser_state = JF_SAX_IN_ITEM_MAP;
            break;
        case JF_SAX_IN_ITEM_TYPE_VALUE:
            if (JF_SAX_STRING_IS("CollectionFolder")
                    && context->current_item_type == JF_ITEM_TYPE_NONE) {
                // don't overwrite if we already got more specific information
                context->current_item_type = JF_ITEM_TYPE_COLLECTION;
            } else if (JF_SAX_STRING_IS("Folder")
                    || JF_SAX_STRING_IS("UserView")
                    || JF_SAX_STRING_IS("PlaylistsFolder")) {
                context->current_item_type = JF_ITEM_TYPE_FOLDER;
            } else if (JF_SAX_STRING_IS("Playlist")) {
                context->current_item_type = JF_ITEM_TYPE_PLAYLIST;
            } else if (JF_SAX_STRING_IS("Audio")) {
                context->current_item_type = JF_ITEM_TYPE_AUDIO;
            } else if (JF_SAX_STRING_IS("Artist")
                    || JF_SAX_STRING_IS("MusicArtist")) {
                context->current_item_type = JF_ITEM_TYPE_ARTIST;
            } else if (JF_SAX_STRING_IS("MusicAlbum")) {
                context->current_item_type = JF_ITEM_TYPE_ALBUM;
            } else if (JF_SAX_STRING_IS("Episode")) {
                context->current_item_type = JF_ITEM_TYPE_EPISODE;
            } else if (JF_SAX_STRING_IS("Season")) {
                context->current_item_type = JF_ITEM_TYPE_SEASON;
            } else if (JF_SAX_STRING_IS("SeriesName")
                    || JF_SAX_STRING_IS("Series")) {
                context->current_item_type = JF_ITEM_TYPE_SERIES;
            } else if (JF_SAX_STRING_IS("Movie")) {
                context->current_item_type = JF_ITEM_TYPE_MOVIE;
            } else if (JF_SAX_STRING_IS("MusicVideo")) {
                context->current_item_type = JF_ITEM_TYPE_MUSIC_VIDEO;
            } else if (JF_SAX_STRING_IS("AudioBook")) {
                context->current_item_type = JF_ITEM_TYPE_AUDIOBOOK;
            }
            context->parser_state = JF_SAX_IN_ITEM_MAP;
            break;
        case JF_SAX_IN_ITEM_COLLECTION_TYPE_VALUE:
            if (JF_SAX_STRING_IS("music")) {
                context->current_item_type = JF_ITEM_TYPE_COLLECTION_MUSIC;
            } else if (JF_SAX_STRING_IS("tvshows")) {
                context->current_item_type = JF_ITEM_TYPE_COLLECTION_SERIES;
            } else if (JF_SAX_STRING_IS("movies") || JF_SAX_STRING_IS("homevideos")) {
                context->current_item_type = JF_ITEM_TYPE_COLLECTION_MOVIES;
            } else if (JF_SAX_STRING_IS("musicvideos")) {
                context->current_item_type = JF_ITEM_TYPE_COLLECTION_MUSIC_VIDEOS;
            } else if (JF_SAX_STRING_IS("folders")) {
                context->current_item_type = JF_ITEM_TYPE_FOLDER;
            }
            context->parser_state = JF_SAX_IN_ITEM_MAP;
            break;
        case JF_SAX_IN_ITEM_ID_VALUE:
            JF_SAX_ITEM_FILL(id);
            context->parser_state = JF_SAX_IN_ITEM_MAP;
            break;
        case JF_SAX_IN_ITEM_ALBUMARTIST_VALUE:
            JF_SAX_ITEM_FILL(artist);
            context->parser_state = JF_SAX_IN_ITEM_MAP;
            break;
        case JF_SAX_IN_ITEM_ALBUM_VALUE:
            JF_SAX_ITEM_FILL(album);
            context->parser_state = JF_SAX_IN_ITEM_MAP;
            break;
        case JF_SAX_IN_ITEM_SERIES_VALUE:
            JF_SAX_ITEM_FILL(series);
            context->parser_state = JF_SAX_IN_ITEM_MAP;
            break;
        default:
            break;
    }
    return 1;
}


static int jf_sax_items_number(void *ctx, const char *string, size_t string_len)
{
    jf_sax_context *context = (jf_sax_context *)(ctx);
    switch (context->parser_state) {
        case JF_SAX_IN_ITEM_RUNTIME_TICKS_VALUE:
            context->runtime_ticks = strtoll(string, NULL, 10);
            context->parser_state = JF_SAX_IN_ITEM_MAP;
            break;
        case JF_SAX_IN_USERDATA_TICKS_VALUE:
            context->playback_ticks = strtoll(string, NULL, 10);
            context->parser_state = JF_SAX_IN_USERDATA_MAP;
            break;
        case JF_SAX_IN_ITEM_YEAR_VALUE:
            JF_SAX_ITEM_FILL(year);
            context->parser_state = JF_SAX_IN_ITEM_MAP;
            break;
        case JF_SAX_IN_ITEM_INDEX_VALUE:
            JF_SAX_ITEM_FILL(index);
            context->parser_state = JF_SAX_IN_ITEM_MAP;
            break;
        case JF_SAX_IN_ITEM_PARENT_INDEX_VALUE:
            JF_SAX_ITEM_FILL(parent_index);
            context->parser_state = JF_SAX_IN_ITEM_MAP;
            break;
        default:
            // ignore everything else
            break;
    }
    return 1;
}
//////////////////////////////////////////


////////// SAX PARSER //////////
static inline bool jf_sax_current_item_is_valid(const jf_sax_context *context)
{
    if (JF_ITEM_TYPE_IS_FOLDER(context->current_item_type)) {
        return context->name_len != 0 && context->id_len != 0;
    } else {
        return context->name_len != 0
            && context->id_len != 0
            && context->runtime_ticks != 0;
    }
}


static inline void jf_sax_current_item_make_and_print_name(jf_sax_context *context)
{
    jf_growing_buffer_empty(&context->current_item_display_name);
    switch (context->current_item_type) {
        case JF_ITEM_TYPE_AUDIO:
        case JF_ITEM_TYPE_AUDIOBOOK:
            JF_SAX_PRINT_LEADER("T");
            if (context->tb->promiscuous_context) {
                JF_SAX_TRY_APPEND_NAME("", artist, " - ");
                JF_SAX_TRY_APPEND_NAME("", album, " - ");
            }
            JF_SAX_TRY_APPEND_NAME("", parent_index, ".");
            JF_SAX_TRY_APPEND_NAME("", index, " - ");
            jf_growing_buffer_append(&context->current_item_display_name,
                    context->parsed_content.buf + context->name_start,
                    context->name_len);
            break;
        case JF_ITEM_TYPE_ALBUM:
            JF_SAX_PRINT_LEADER("D");
            if (context->tb->promiscuous_context) {
                JF_SAX_TRY_APPEND_NAME("", artist, " - ");
            }
            jf_growing_buffer_append(&context->current_item_display_name,
                    context->parsed_content.buf + context->name_start,
                    context->name_len);
            JF_SAX_TRY_APPEND_NAME(" (", year, ")");
            break;
        case JF_ITEM_TYPE_EPISODE:
            JF_SAX_PRINT_LEADER("V");
            if (context->tb->promiscuous_context) {
                JF_SAX_TRY_APPEND_NAME("", series, " - ");
                JF_SAX_TRY_APPEND_NAME("S", parent_index, "");
            }
            JF_SAX_TRY_APPEND_NAME("E", index, " ");
            jf_growing_buffer_append(&context->current_item_display_name,
                    context->parsed_content.buf + context->name_start,
                    context->name_len);
            break;
        case JF_ITEM_TYPE_SEASON:
            JF_SAX_PRINT_LEADER("D");
            if (context->tb->promiscuous_context) {
                JF_SAX_TRY_APPEND_NAME("", series, " - ");
            }
            jf_growing_buffer_append(&context->current_item_display_name,
                    context->parsed_content.buf + context->name_start,
                    context->name_len);
            break;
        case JF_ITEM_TYPE_MOVIE:
        case JF_ITEM_TYPE_MUSIC_VIDEO:
            JF_SAX_PRINT_LEADER("V");
            jf_growing_buffer_append(&context->current_item_display_name,
                    context->parsed_content.buf + context->name_start,
                    context->name_len);
            JF_SAX_TRY_APPEND_NAME(" (", year, ")");
            break;
        case JF_ITEM_TYPE_ARTIST:
        case JF_ITEM_TYPE_SERIES:
        case JF_ITEM_TYPE_PLAYLIST:
        case JF_ITEM_TYPE_FOLDER:
        case JF_ITEM_TYPE_COLLECTION:
        case JF_ITEM_TYPE_COLLECTION_MUSIC:
        case JF_ITEM_TYPE_COLLECTION_SERIES:
        case JF_ITEM_TYPE_COLLECTION_MOVIES:
        case JF_ITEM_TYPE_COLLECTION_MUSIC_VIDEOS:
        case JF_ITEM_TYPE_USER_VIEW:
            JF_SAX_PRINT_LEADER("D");
            jf_growing_buffer_append(&context->current_item_display_name,
                    context->parsed_content.buf + context->name_start,
                    context->name_len);
            break;
        default:
            fprintf(stderr, "Warning: jf_sax_items_end_map: unexpected jf_item_type. This is a bug.\n");
    }

    jf_growing_buffer_append(&context->current_item_display_name, "", 1);
    printf("%s\n", context->current_item_display_name.buf);
}


static inline yajl_handle jf_sax_yajl_parser_new(yajl_callbacks *callbacks, jf_sax_context *context)
{
    yajl_handle parser;
    assert((parser = yajl_alloc(callbacks, NULL, (void *)(context))) != NULL);
    // allow persistent parser to digest many JSON objects
    assert(yajl_config(parser, yajl_allow_multiple_values, 1) != 0);
    return parser;
}


static inline void jf_sax_context_init(jf_sax_context *context, jf_thread_buffer *tb)
{
    *context = (jf_sax_context){ 0 };
    context->parser_state = JF_SAX_IDLE;
    context->state_to_resume = JF_SAX_NO_STATE;
    context->latest_array = false;
    context->tb = tb;
    context->current_item_type = JF_ITEM_TYPE_NONE;
    jf_growing_buffer_init(&context->current_item_display_name, 0);
    jf_growing_buffer_init(&context->parsed_content, 0);
}


static inline void jf_sax_context_current_item_clear(jf_sax_context *context)
{
    context->current_item_type = JF_ITEM_TYPE_NONE;
    jf_growing_buffer_empty(&context->parsed_content);
    context->name_len = 0;
    context->id_len = 0;
    context->artist_len = 0;
    context->album_len = 0;
    context->series_len = 0;
    context->year_len = 0;
    context->index_len = 0;
    context->parent_index_len = 0;
    context->runtime_ticks = 0;
    context->playback_ticks = 0;
}


void *jf_json_sax_thread(void *arg)
{
    jf_sax_context context;
    yajl_status status;
    yajl_handle parser;
    yajl_callbacks callbacks = {
        .yajl_null = NULL,
        .yajl_boolean = NULL,
        .yajl_integer = NULL,
        .yajl_double = NULL,
        .yajl_number = jf_sax_items_number,
        .yajl_string = jf_sax_items_string,
        .yajl_start_map = jf_sax_items_start_map,
        .yajl_map_key = jf_sax_items_map_key,
        .yajl_end_map = jf_sax_items_end_map,
        .yajl_start_array = jf_sax_items_start_array,
        .yajl_end_array = jf_sax_items_end_array
    };
    unsigned char *error_str;

    jf_sax_context_init(&context, (jf_thread_buffer *)arg);

    assert((parser = jf_sax_yajl_parser_new(&callbacks, &context)) != NULL);

    pthread_mutex_lock(&context.tb->mut);
    while (true) {
        while (context.tb->state != JF_THREAD_BUFFER_STATE_PENDING_DATA) {
            pthread_cond_wait(&context.tb->cv_no_data, &context.tb->mut);
        }
        if ((status = yajl_parse(parser, (unsigned char*)context.tb->data, context.tb->used)) != yajl_status_ok) {
            error_str = yajl_get_error(parser, 1, (unsigned char*)context.tb->data, context.tb->used);
            strcpy(context.tb->data, "yajl_parse error: ");
            strncat(context.tb->data, (char *)error_str, JF_PARSER_ERROR_BUFFER_SIZE - strlen(context.tb->data));
            context.tb->state = JF_THREAD_BUFFER_STATE_PARSER_ERROR;
            pthread_mutex_unlock(&context.tb->mut);
            yajl_free_error(parser, error_str);
            // the parser never recovers after an error; we must free and reallocate it
            yajl_free(parser);
            parser = jf_sax_yajl_parser_new(&callbacks, &context);
        } else if (context.parser_state == JF_SAX_IDLE) {
            // JSON fully parsed
            yajl_complete_parse(parser);
            context.tb->state = JF_THREAD_BUFFER_STATE_CLEAR;
        } else {
            // we've still more to go
            context.tb->state = JF_THREAD_BUFFER_STATE_AWAITING_DATA;
        }
        
        context.tb->used = 0;
        pthread_cond_signal(&context.tb->cv_has_data);
    }
}
////////////////////////////////


////////// VIDEO PARSING //////////
static jf_menu_item *jf_json_parse_versions(const jf_menu_item *item, const yajl_val media_sources)
{
    jf_menu_item **subs = NULL;
    size_t subs_count = 0;
    size_t i, j;
    char *tmp;
    yajl_val media_streams, source, stream;

    if (YAJL_GET_ARRAY(media_sources)->len > 1) {
        printf("\nThere are multiple versions available of %s.\n", item->name);
        printf("Please choose one:\n");
        for (i = 0; i < YAJL_GET_ARRAY(media_sources)->len; i++) {
            assert((source = YAJL_GET_ARRAY(media_sources)->values[i]) != NULL);
            printf("%zu: %s (",
                    i + 1,
                    YAJL_GET_STRING(jf_yajl_tree_get_assert(source,
                            ((const char *[]){ "Name", NULL }),
                            yajl_t_string)));
            media_streams = jf_yajl_tree_get_assert(source,
                    ((const char *[]){ "MediaStreams", NULL }),
                    yajl_t_array);
            for (j = 0; j < YAJL_GET_ARRAY(media_streams)->len; j++) {
                assert((stream = YAJL_GET_ARRAY(media_streams)->values[j]) != NULL);
                printf(" %s",
                        YAJL_GET_STRING(jf_yajl_tree_get_assert(stream,
                                ((const char *[]){ "DisplayTitle", NULL }),
                                yajl_t_string)));
            }
            printf(")\n");
        }
        i = jf_menu_user_ask_selection(1, YAJL_GET_ARRAY(media_sources)->len);
        i--;
    } else {
        i = 0;
    }

    assert((source = YAJL_GET_ARRAY(media_sources)->values[i]) != NULL);
    media_streams = jf_yajl_tree_get_assert(source,
            ((const char *[]){ "MediaStreams", NULL }),
            yajl_t_array);
    for (j = 0; j < YAJL_GET_ARRAY(media_streams)->len; j++) {
        assert((stream = YAJL_GET_ARRAY(media_streams)->values[j]) != NULL);
        char *codec = YAJL_GET_STRING(jf_yajl_tree_get_assert(stream,
                    ((const char*[]){ "Codec", NULL}),
                    yajl_t_string));
        if (strcmp(YAJL_GET_STRING(jf_yajl_tree_get_assert(stream,
                            ((const char *[]){ "Type", NULL }),
                            yajl_t_string)),
                    "Subtitle") == 0
                && YAJL_IS_TRUE(jf_yajl_tree_get_assert(stream,
                        ((const char *[]){ "IsExternal", NULL }),
                        yajl_t_any))
                && strcmp(codec, "sub") != 0) {
            char *id = YAJL_GET_STRING(jf_yajl_tree_get_assert(source, ((const char *[]){ "Id", NULL }), yajl_t_string));
            tmp = jf_concat(8,
                    "/videos/",
                    id,
                    "/",
                    id,
                    "/subtitles/",
                    YAJL_GET_NUMBER(jf_yajl_tree_get_assert(stream, ((const char *[]){ "Index", NULL }), yajl_t_number)),
                    "/0/stream.",
                    codec);
            assert((subs = realloc(subs, ++subs_count * sizeof(jf_menu_item *))) != NULL);
            subs[subs_count - 1] = jf_menu_item_new(JF_ITEM_TYPE_VIDEO_SUB,
                    NULL, // children
                    NULL, // id
                    tmp,
                    0, 0); // ticks
            free(tmp);
            if ((tmp = YAJL_GET_STRING(yajl_tree_get(stream, ((const char *[]){ "Language", NULL }), yajl_t_string))) == NULL) {
                subs[subs_count - 1]->id[0] = '\0';
            } else {
                strncpy(subs[subs_count - 1]->id, tmp, 3);
            }
            strncpy(subs[subs_count - 1]->id + 3,
                    YAJL_GET_STRING(jf_yajl_tree_get_assert(stream, ((const char *[]){ "DisplayTitle", NULL }), yajl_t_string)),
                    JF_ID_LENGTH - 3);
            subs[subs_count - 1]->id[JF_ID_LENGTH] = '\0';
        }
    }
    // NULL-terminate children
    assert((subs = realloc(subs, (subs_count + 1) * sizeof(jf_menu_item *))) != NULL);
    subs[subs_count] = NULL;

    return jf_menu_item_new(JF_ITEM_TYPE_VIDEO_SOURCE,
            subs,
            YAJL_GET_STRING(jf_yajl_tree_get_assert(source, ((const char *[]){ "Id", NULL }), yajl_t_string)),
            NULL,
            YAJL_GET_INTEGER(jf_yajl_tree_get_assert(source, ((const char *[]){ "RunTimeTicks", NULL }), yajl_t_number)), // RT ticks
            0);
}


void jf_json_parse_video(jf_menu_item *item, const char *video, const char *additional_parts)
{
    yajl_val parsed, part_count, part_item;
    size_t i;

    JF_JSON_TREE_PARSE_ASSERT((parsed = yajl_tree_parse(video, s_error_buffer, JF_PARSER_ERROR_BUFFER_SIZE)) != NULL);
    // PartCount is not defined when it is == 1
    if ((part_count =  yajl_tree_get(parsed, (const char *[]){ "PartCount", NULL }, yajl_t_number)) == NULL) {
        item->children_count = 1;
    } else {
        item->children_count = (size_t)YAJL_GET_INTEGER(part_count);
    }
    assert((item->children = malloc(item->children_count * sizeof(jf_menu_item *))) != NULL);
    item->children[0] = jf_json_parse_versions(item,
            jf_yajl_tree_get_assert(parsed,
                ((const char *[]){ "MediaSources", NULL }),
                yajl_t_array));
    yajl_tree_free(parsed);

    // check for additional parts
    if (item->children_count > 1) {
        s_error_buffer[0] = '\0';
        if ((parsed = yajl_tree_parse(additional_parts,
                        s_error_buffer,
                        JF_PARSER_ERROR_BUFFER_SIZE)) == NULL) {
            fprintf(stderr, "FATAL: jf_json_parse_additional_parts: %s\n",
                    s_error_buffer[0] == '\0' ? "yajl_tree_parse unknown error" : s_error_buffer);
            jf_exit(JF_EXIT_FAILURE);
        }
        for (i = 1; i < item->children_count; i++) {
            part_item = YAJL_GET_ARRAY(jf_yajl_tree_get_assert(parsed,
                        ((const char *[]){ "Items", NULL }),
                        yajl_t_array))->values[i - 1];
            item->children[i] = jf_json_parse_versions(item,
                    jf_yajl_tree_get_assert(part_item,
                        ((const char *[]){ "MediaSources", NULL }),
                        yajl_t_array));
        }
        yajl_tree_free(parsed);
    }

    // the parent item refers the same part as the first child. for the sake
    // of the resume interface, copy playback_ticks from parent to firstborn
    item->children[0]->playback_ticks = item->playback_ticks;
}


void jf_json_parse_playback_ticks(jf_menu_item *item, const char *payload)
{
    yajl_val parsed, ticks;

    JF_JSON_TREE_PARSE_ASSERT((parsed = yajl_tree_parse(payload, s_error_buffer, JF_PARSER_ERROR_BUFFER_SIZE)) != NULL);
    ticks = yajl_tree_get(parsed, (const char *[]){ "UserData", "PlaybackPositionTicks", NULL}, yajl_t_number);
    if (ticks != NULL) {
        item->playback_ticks = YAJL_GET_INTEGER(ticks);
    }
    yajl_tree_free(parsed);
}
///////////////////////////////////


////////// MISCELLANEOUS GARBAGE //////////
char *jf_json_error_string()
{
    return s_error_buffer;
}


static inline yajl_val __jf_yajl_tree_get_assert(const int lineno,
        yajl_val parent,
        const char **path,
        yajl_type type)
{
    yajl_val v = yajl_tree_get(parent, path, type);
    if (v == NULL) {
        const char **curr = path;
        fprintf(stderr, "%s:%d: jf_yajl_tree_get_assert failed.\n", __FILE__, lineno);
        fprintf(stderr, "FATAL: couldn't find JSON element \"");
        while (*curr != NULL) {
            fprintf(stderr, ".%s", *curr);
            curr++;
        }
        fprintf(stderr, "\".\n");
        jf_exit(JF_EXIT_FAILURE);
    }
    return v;
}


void jf_json_parse_login_response(const char *payload)
{
    yajl_val parsed;
    char *tmp;

    JF_JSON_TREE_PARSE_ASSERT((parsed = yajl_tree_parse(payload,
                    s_error_buffer,
                    JF_PARSER_ERROR_BUFFER_SIZE)) != NULL);
    free(g_options.userid);
    assert((tmp = YAJL_GET_STRING(jf_yajl_tree_get_assert(parsed,
                    ((const char *[]){ "User", "Id", NULL }),
                    yajl_t_string))) != NULL);
    g_options.userid = strdup(tmp);
    free(g_options.token);
    assert((tmp = YAJL_GET_STRING(jf_yajl_tree_get_assert(parsed,
                    ((const char *[]){ "AccessToken", NULL }),
                    yajl_t_string))) != NULL);
    g_options.token = strdup(tmp);
    yajl_tree_free(parsed);
}


void jf_json_parse_system_info_response(const char *payload)
{
    yajl_val parsed;
    char *tmp, *endptr;
    unsigned long major, minor, patch;

    JF_JSON_TREE_PARSE_ASSERT((parsed = yajl_tree_parse(payload,
                    s_error_buffer,
                    JF_PARSER_ERROR_BUFFER_SIZE)) != NULL);
    assert((tmp = YAJL_GET_STRING(jf_yajl_tree_get_assert(parsed,
                    ((const char *[]){ "ServerName", NULL }),
                    yajl_t_string))) != NULL);
    g_state.server_name = strdup(tmp);
    
    assert((tmp = YAJL_GET_STRING(jf_yajl_tree_get_assert(parsed,
                    ((const char *[]){ "Version", NULL }),
                    yajl_t_string))) != NULL);
    major = strtoul(tmp, &endptr, 10);
    assert(endptr != NULL && *endptr == '.');
    tmp = endptr + 1;
    minor = strtoul(tmp, &endptr, 10);
    assert(endptr != NULL && *endptr == '.');
    tmp = endptr + 1;
    patch = strtoul(tmp, &endptr, 10);
    assert(endptr != NULL && *endptr == '\0');
    g_state.server_version = JF_SERVER_VERSION_MAKE(major, minor, patch);
    
    yajl_tree_free(parsed);
}


char *jf_json_generate_login_request(const char *username, const char *password)
{
    yajl_gen gen;
    char *json = NULL;
    size_t json_len;

    assert((gen = yajl_gen_alloc(NULL)) != NULL);
    assert(yajl_gen_map_open(gen) == yajl_status_ok);
    assert(yajl_gen_string(gen, (const unsigned char *)"Username", JF_STATIC_STRLEN("Username")) == yajl_status_ok);
    assert(yajl_gen_string(gen, (const unsigned char *)username, strlen(username)) == yajl_status_ok);
    assert(yajl_gen_string(gen, (const unsigned char *)"Pw", JF_STATIC_STRLEN("Pw")) == yajl_status_ok);
    assert(yajl_gen_string(gen, (const unsigned char *)password, strlen(password)) == yajl_status_ok);
    assert(yajl_gen_map_close(gen) == yajl_status_ok);
    assert(yajl_gen_get_buf(gen, (const unsigned char **)&json, &json_len) == yajl_status_ok);
    assert((json = strndup(json, json_len)) != NULL);

    yajl_gen_free(gen);
    return json;
}


char *jf_json_generate_progress_post(const char *id, const long long ticks)
{
    yajl_gen gen;
    char *json = NULL;
    size_t json_len;

    assert((gen = yajl_gen_alloc(NULL)) != NULL);
    assert(yajl_gen_map_open(gen) == yajl_status_ok);
    assert(yajl_gen_string(gen,
                (const unsigned char *)"ItemId",
                JF_STATIC_STRLEN("ItemId")) == yajl_status_ok);
    assert(yajl_gen_string(gen,
                (const unsigned char *)id,
                JF_ID_LENGTH) == yajl_status_ok);
    assert(yajl_gen_string(gen,
                (const unsigned char *)"PositionTicks",
                JF_STATIC_STRLEN("PositionTicks")) == yajl_status_ok);
    assert(yajl_gen_integer(gen, ticks) == yajl_status_ok);
    assert(yajl_gen_map_close(gen) == yajl_status_ok);
    assert(yajl_gen_get_buf(gen,
                (const unsigned char **)&json,
                &json_len) == yajl_status_ok);
    assert((json = strndup(json, json_len)) != NULL);

    yajl_gen_free(gen);
    return json;
}
///////////////////////////////////////////
