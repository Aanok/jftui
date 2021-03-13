#ifndef _JF_JSON
#define _JF_JSON


#include "shared.h"

#include <stddef.h>
#include <stdbool.h>


////////// CODE MACROS //////////
#define JF_SAX_BAD_STATE()                      \
do {                                            \
    fprintf(stderr,                             \
            "%s:%d: JF_SAX_BAD_STATE (%zu).\n", \
            __FILE__, __LINE__,                 \
            context->parser_state);             \
    fprintf(stderr, "This is a bug.\n");        \
    return 0;                                   \
} while (false)

#define JF_SAX_ITEM_FILL(field)                     \
do {                                                \
    context->field = (const unsigned char *)string; \
    context->field ## _len = string_len;            \
} while (false)

#define JF_SAX_CONTEXT_COPY(field)                              \
do {                                                            \
    if (context->field ## _len > 0) {                           \
        strncpy(buf + used, (const char *)context->field,       \
                (size_t)context->field ## _len);                \
        context->field = (const unsigned char *)(buf + used);   \
        used += (size_t)context->field ## _len;                 \
    }                                                           \
} while (false)

#define JF_SAX_CONTEXT_PTR_PARSED_DATA_LENGTH(_c) ((_c)->name_len \
        + (_c)->id_len                                            \
        + (_c)->artist_len                                        \
        + (_c)->album_len                                         \
        + (_c)->series_len                                        \
        + (_c)->year_len                                          \
        + (_c)->index_len                                         \
        + (_c)->parent_index_len)

#define JF_SAX_KEY_IS(name) (JF_STATIC_STRLEN(name) == key_len && strncmp((const char *)key, name, JF_STATIC_STRLEN(name)) == 0)

#define JF_SAX_STRING_IS(name) (JF_STATIC_STRLEN(name) == string_len && strncmp((const char *)string, name, JF_STATIC_STRLEN(name)) == 0)

#define JF_SAX_PRINT_LEADER(tag) printf(tag " %zu: ", context->tb->item_count)


// NB THIS WILL NOT BE NULL-TERMINATED ON ITS OWN!!!
#define JF_SAX_TRY_APPEND_NAME(prefix, field, suffix)                   \
do {                                                                    \
    if (context->field ## _len > 0) {                                   \
        jf_growing_buffer_append(context->current_item_display_name,    \
                prefix, JF_STATIC_STRLEN(prefix));                      \
        jf_growing_buffer_append(context->current_item_display_name,    \
                context->field, context->field ## _len);                \
        jf_growing_buffer_append(context->current_item_display_name,    \
                suffix, JF_STATIC_STRLEN(suffix));                      \
    }                                                                   \
} while (false)


#define JF_JSON_TREE_PARSE_ASSERT(_s)                                       \
do {                                                                        \
    s_error_buffer[0] = '\0';                                               \
    bool _success = _s;                                                     \
    if (! _success) {                                                       \
        fprintf(stderr, "%s:%d: " #_s " failed.\n", __FILE__, __LINE__);    \
        fprintf(stderr, "FATAL: yajl_parse error: %s\n",                    \
                s_error_buffer[0] == '\0' ? "unknown" : s_error_buffer);    \
        jf_exit(JF_EXIT_FAILURE);                                           \
    }                                                                       \
} while (false)

#define jf_yajl_tree_get_assert(_parent, _path, _type) __jf_yajl_tree_get_assert(__LINE__, (_parent), (_path), (_type))
/////////////////////////////////


////////// SAX PARSER //////////
typedef enum jf_sax_parser_state {
    JF_SAX_NO_STATE = 0,
    JF_SAX_IDLE = 1,
    JF_SAX_IN_LATEST_ARRAY = 2,
    JF_SAX_IN_QUERYRESULT_MAP = 3,
    JF_SAX_IN_ITEMS_VALUE = 4,
    JF_SAX_IN_ITEMS_ARRAY = 5,
    JF_SAX_IN_ITEM_MAP = 6,
    JF_SAX_IN_ITEM_TYPE_VALUE = 7,
    JF_SAX_IN_ITEM_COLLECTION_TYPE_VALUE = 8,
    JF_SAX_IN_ITEM_NAME_VALUE = 9,
    JF_SAX_IN_ITEM_ID_VALUE = 10,
    JF_SAX_IN_ITEM_ALBUMARTIST_VALUE = 11,
    JF_SAX_IN_ITEM_ALBUM_VALUE = 12,
    JF_SAX_IN_ITEM_SERIES_VALUE = 13,
    JF_SAX_IN_ITEM_YEAR_VALUE = 14,
    JF_SAX_IN_ITEM_INDEX_VALUE = 15,
    JF_SAX_IN_ITEM_PARENT_INDEX_VALUE = 16,
    JF_SAX_IN_ITEM_RUNTIME_TICKS_VALUE = 17,
    JF_SAX_IN_USERDATA_MAP = 18,
    JF_SAX_IN_USERDATA_VALUE = 19,
    JF_SAX_IN_USERDATA_TICKS_VALUE = 20,
    JF_SAX_IGNORE = 127
} jf_sax_parser_state;


#define JF_PARSER_ERROR_BUFFER_SIZE 1024


typedef struct jf_sax_context {
    jf_sax_parser_state parser_state;
    jf_sax_parser_state state_to_resume;
    size_t maps_ignoring;
    size_t arrays_ignoring;
    bool latest_array;
    jf_thread_buffer *tb;
    jf_item_type current_item_type;
    char *copy_buffer;
    jf_growing_buffer *current_item_display_name;
    const unsigned char *name;          size_t name_len;
    const unsigned char *id;            size_t id_len;
    const unsigned char *artist;        size_t artist_len;
    const unsigned char *album;         size_t album_len;
    const unsigned char *series;        size_t series_len;
    const unsigned char *year;          size_t year_len;
    const unsigned char *index;         size_t index_len;
    const unsigned char *parent_index;  size_t parent_index_len;
    long long runtime_ticks;
    long long playback_ticks;
} jf_sax_context;


void *jf_json_sax_thread(void *arg);
////////////////////////////////


////////// VIDEO PARSING //////////
void jf_json_parse_video(jf_menu_item *item, const char *video, const char *additional_parts);
void jf_json_parse_playback_ticks(jf_menu_item *item, const char *payload);
///////////////////////////////////


////////// MISCELLANEOUS GARBAGE //////////
char *jf_json_error_string(void);
void jf_json_parse_login_response(const char *payload);
char *jf_json_generate_login_request(const char *username, const char *password);
void jf_json_parse_server_info_response(const char *payload);
char *jf_json_generate_progress_post(const char *id, const long long ticks);
///////////////////////////////////////////
#endif
