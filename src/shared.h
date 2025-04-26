#ifndef _JF_SHARED
#define _JF_SHARED


#include <stddef.h>
#include <stdio.h>
#include <stdbool.h>
#include <pthread.h>

#include <curl/curl.h>
#include <mpv/client.h>


////////// CODE MACROS //////////
// for hardcoded strings
#define JF_STATIC_STRLEN(str) (sizeof(str) - 1)

// for progress
#define JF_TICKS_TO_SECS(t) (t) / 10000000
#define JF_SECS_TO_TICKS(s) (s) * 10000000

#define JF_MPV_ASSERT(_s)                                                   \
do {                                                                        \
    int _status = _s;                                                       \
    if (_status < 0) {                                                      \
        fprintf(stderr, "%s:%d: " #_s " failed.\n", __FILE__, __LINE__);    \
        fprintf(stderr, "FATAL: mpv API error: %s.\n",                      \
                mpv_error_string(_status));                                 \
        jf_exit(JF_EXIT_FAILURE);                                           \
    }                                                                       \
} while (false)

#ifdef JF_DEBUG
#define JF_PRINTF_INDENT(...)       \
do {                                \
    for (i = 0; i < level; i++) {   \
        putchar('\t');              \
    }                               \
    printf(__VA_ARGS__);            \
} while (false)
#endif


#ifdef JF_DEBUG
#define JF_DEBUG_PRINTF(...) fprintf(stderr, "DEBUG: " __VA_ARGS__)
#else
#define JF_DEBUG_PRINTF(...)
#endif


// server version
// lower 16 bits reserved for potential devel releases
#define JF_SERVER_VERSION_MAKE(_major,_minor,_patch) \
    (((uint64_t)(_major) << 48) | ((uint64_t)(_minor) << 32) | ((uint64_t)(_patch) << 16))
#define JF_SERVER_VERSION_GET_MAJOR(_version) ((_version) >> 48)
#define JF_SERVER_VERSION_GET_MINOR(_version) (((_version) >> 32) & 0xFF)
#define JF_SERVER_VERSION_GET_PATCH(_version) (((_version) >> 16) & 0xFF)


#define JF_CURL_VERSION_GE(_major,_minor) \
    (LIBCURL_VERSION_MAJOR == _major && LIBCURL_VERSION_MINOR >= _minor) || LIBCURL_VERSION_MAJOR >= _major
/////////////////////////////////


////////// CONSTANTS //////////
#define JF_VERSION "0.7.3"
#define JF_THREAD_BUFFER_DATA_SIZE (CURL_MAX_WRITE_SIZE +1)
#define JF_ID_LENGTH 32
///////////////////////////////


// make sure all custom exit codes are not positive to avoid collisions
// with UNIX signal identifiers
#define JF_EXIT_SUCCESS 0
#define JF_EXIT_FAILURE -1


////////// PROGRAM TERMINATION //////////
// Note: the code for this function is defined in the main.c TU.
// Exits the program after making an attempt to perform required cleanup.
// Meant as a catch-all, including normal termination and signal handling.
//
// Parameters:
//  - sig: can be a UNIX signal identifier or either JF_EXIT_FAILURE or
//      JF_EXIT_SUCCESS. In the latter two cases, the process wil return the
//      corresponding stdlib exit codes.
// CAN (unsurprisingly) FATAL.
void jf_exit(int sig);
/////////////////////////////////////////


////////// GENERIC JELLYFIN ITEM REPRESENTATION //////////
// Information about persistency is used to make part of the menu interface
// tree not get deallocated when navigating upwards
typedef enum __attribute__((__packed__)) jf_item_type {
    // Atoms
    JF_ITEM_TYPE_NONE = 0,
    JF_ITEM_TYPE_AUDIO = 1,
    JF_ITEM_TYPE_AUDIOBOOK = 2,
    JF_ITEM_TYPE_EPISODE = 3,
    JF_ITEM_TYPE_MOVIE = 4,
    JF_ITEM_TYPE_MUSIC_VIDEO = 5,
    JF_ITEM_TYPE_VIDEO_SOURCE = 6,
    // Subs break the usual format:
    //  name: suffix URL for the stream. This is better computed at parse time
    //      and cached for later use instead of computed on the fly by
    //      as usual, since it requires more information (id, stream number,
    //      codec) than normal.
    //  id: given the above it would be redundant, so we use it for additional
    //      information in the format "xxxDisplayTitle": the first three
    //      characters mark an ISO language code (id[0] == '\0' if not
    //      available) while the remaining 29 characters contain as much of
    //      the JF DisplayTitle as possible.
    JF_ITEM_TYPE_VIDEO_SUB = 7,

    // Folders
    JF_ITEM_TYPE_COLLECTION = 20,
    JF_ITEM_TYPE_COLLECTION_MUSIC = 21,
    JF_ITEM_TYPE_COLLECTION_SERIES = 22,
    JF_ITEM_TYPE_COLLECTION_MOVIES = 23,
    JF_ITEM_TYPE_COLLECTION_MUSIC_VIDEOS = 24,
    JF_ITEM_TYPE_USER_VIEW = 25,
    JF_ITEM_TYPE_FOLDER = 26,
    JF_ITEM_TYPE_PLAYLIST = 27,
    JF_ITEM_TYPE_ARTIST = 28,
    JF_ITEM_TYPE_ALBUM = 29,
    JF_ITEM_TYPE_SEASON = 30,
    JF_ITEM_TYPE_SERIES = 31,

    // Special folder
    JF_ITEM_TYPE_SEARCH_RESULT = 100,

    // Persistent folders
    JF_ITEM_TYPE_MENU_ROOT = -1,
    JF_ITEM_TYPE_MENU_FAVORITES = -2,
    JF_ITEM_TYPE_MENU_CONTINUE = -3,
    JF_ITEM_TYPE_MENU_NEXT_UP = -4,
    JF_ITEM_TYPE_MENU_LATEST_ADDED = -5,
    JF_ITEM_TYPE_MENU_LIBRARIES = -6
} jf_item_type;

// Category macros. They're all expressions
// UPDATE THESE if you add item_type's or change the item_type representation!
#define JF_ITEM_TYPE_IS_PERSISTENT(t)           ((t) < 0)
#define JF_ITEM_TYPE_IS_FOLDER(t)               ((t) < 0 || (t) >= 20)
#define JF_ITEM_TYPE_HAS_DYNAMIC_CHILDREN(t)    ((t) < -1 || (t) >= 20)


const char *jf_item_type_get_name(const jf_item_type type);


typedef struct jf_menu_item {
    jf_item_type type;
    struct jf_menu_item **children;
    size_t children_count;
    char id[JF_ID_LENGTH +1];
    char *name;
    char *path;
    long long playback_ticks;
    long long runtime_ticks;
} jf_menu_item;


// Allocates a jf_menu_item struct in dynamic memory.
//
// Parameters:
//  - type: the jf_item_type of the menu item being represented.
//  - children: an array of pointers to jf_menu_item's that
//      descend from the current one in the UI/library hierarchy.
//      IT IS NOT COPIED BUT ASSIGNED (MOVE).
//  - children_count: the lenght of the `children` array
//  - id: the string marking the id of the item. It will be copied to an
//      internal buffer and must have JF_ID_LENGTH size but does not need to be
//      \0-terminated. May be NULL for persistent menu items, in which case the
//      internal buffer will contain a \0-terminated empty string.
//  - name: the string marking the display name of the item. It must be
//      \0-terminated. It will be copied by means of strdup. May be NULL, in
//      which case the corresponding field of the jf_menu_item will be NULL.
//  - runtime_ticks: length of underlying media item measured in JF ticks.
//  - playback_ticks: progress marker for partially viewed items measured in JF ticks.
//
// Returns:
//  A pointer to the newly allocated struct.
// CAN FATAL.
jf_menu_item *jf_menu_item_new(jf_item_type type,
        jf_menu_item **children,
        const size_t children_count,
        const char *id,
        const char *name,
        const char *path,
        const long long runtime_ticks,
        const long long playback_ticks);

// Deallocates a jf_menu_item and all its descendants recursively, unless they
// are marked as persistent (as per JF_ITEM_TYPE_IS_PERSISTENT).
//
// Parameters:
//  - menu_item: a pointer to the struct to deallocate. It may be NULL, in which
//      case the function will no-op.
void jf_menu_item_free(jf_menu_item *menu_item);


#ifdef JF_DEBUG
void jf_menu_item_print(const jf_menu_item *item);
#endif
//////////////////////////////////////////////////////////


////////// GROWING BUFFER //////////
typedef struct _jf_growing_buffer {
    char *buf;
    size_t size;
    size_t used;
} *jf_growing_buffer;


jf_growing_buffer jf_growing_buffer_new(const size_t size);
void jf_growing_buffer_append(jf_growing_buffer buffer,
        const void *data,
        const size_t length);
void jf_growing_buffer_sprintf(jf_growing_buffer buffer,
        size_t offset,
        const char *format, ...);
void jf_growing_buffer_empty(jf_growing_buffer buffer);
void jf_growing_buffer_free(jf_growing_buffer buffer);
////////////////////////////////////


////////// THREAD_BUFFER //////////
typedef enum jf_thread_buffer_state {
    JF_THREAD_BUFFER_STATE_CLEAR = 0,
    JF_THREAD_BUFFER_STATE_AWAITING_DATA = 1,
    JF_THREAD_BUFFER_STATE_PENDING_DATA = 2,
    JF_THREAD_BUFFER_STATE_PARSER_ERROR = 3,
    JF_THREAD_BUFFER_STATE_PARSER_DEAD = 4
} jf_thread_buffer_state;


typedef struct jf_thread_buffer {
    char data[JF_THREAD_BUFFER_DATA_SIZE];
    size_t used;
    bool promiscuous_context;
    jf_thread_buffer_state state;
    size_t item_count;
    pthread_mutex_t mut;
    pthread_cond_t cv_no_data;
    pthread_cond_t cv_has_data;
} jf_thread_buffer;


void jf_thread_buffer_init(jf_thread_buffer *tb);
///////////////////////////////////


////////// GLOBAL APPLICATION STATE //////////
typedef enum jf_jftui_state {
    JF_STATE_STARTING = 0,
    JF_STATE_STARTING_FULL_CONFIG = 1,
    JF_STATE_STARTING_LOGIN = 2,
    JF_STATE_MENU_UI = 3,
    JF_STATE_PLAYBACK = 4,
    JF_STATE_PLAYBACK_INIT = 5,
    JF_STATE_PLAYBACK_START_MARK = 6,
    JF_STATE_PLAYLIST_SEEKING = 7,
    JF_STATE_PLAYBACK_STOPPING = 8,

    JF_STATE_USER_QUIT = -1,
    JF_STATE_FAIL = -2
} jf_jftui_state;

#define JF_STATE_IS_EXITING(_s) ((_s) < 0)


typedef enum jf_loop_state {
    JF_LOOP_STATE_IN_SYNC = 0,
    JF_LOOP_STATE_RESYNCING = 1,
    JF_LOOP_STATE_OUT_OF_SYNC = 2
} jf_loop_state;


typedef struct jf_global_state {
    char *config_dir;
    char *session_id;
    char *server_name;
    uint64_t server_version;
    jf_jftui_state state;
    jf_menu_item *now_playing;
    // 1-indexed
    size_t playlist_position;
    // counter for playlist loops to do
    // -1 for infinite loops
    int64_t playlist_loops;
    jf_loop_state loop_state;
#if MPV_CLIENT_API_VERSION >= MPV_MAKE_VERSION(2,1)
    char *mpv_cache_dir;
#endif
} jf_global_state;
//////////////////////////////////////////////


////////// SYNCED QUEUE //////////
typedef struct jf_synced_queue {
    const void **slots;
    size_t slot_count;
    size_t current;
    size_t next;
    pthread_mutex_t mut;
    pthread_cond_t cv_is_empty;
    pthread_cond_t cv_is_full;
} jf_synced_queue;

jf_synced_queue *jf_synced_queue_new(const size_t slot_count);

// Deallocates the queue but NOT its contents.
//
// Parameters:
//  - q: pointer to the jf_synced_queue to deallocate (if NULL, no-op).
// CAN'T FAIL.
void jf_synced_queue_free(jf_synced_queue *q); 

void jf_synced_queue_enqueue(jf_synced_queue *q, const void *payload);

void *jf_synced_queue_dequeue(jf_synced_queue *q);
//////////////////////////////////


////////// MISCELLANEOUS GARBAGE //////////
typedef enum jf_strong_bool {
    JF_STRONG_BOOL_NO = 0,
    JF_STRONG_BOOL_YES = 1,
    JF_STRONG_BOOL_FORCE = 2
} jf_strong_bool;


// Parses a string into a jf_strong_bool.
// The mappings are case insensitive, as follows:
//  - "no" to JF_STRONG_BOOL_NO;
//  - "yes" to JF_STRONG_BOOL_YES;
//  - "force" to JF_STRONG_BOOL_FORCE.
//
// Returns:
//  - true on successful parsing;
//  - false on NULL or unrecognized input string.
bool jf_strong_bool_parse(const char *str,
        const size_t len,
        jf_strong_bool *out);

// Returns the obvious string representing a jf_strong_bool.
// The mapping is as per the jf_strong_bool_parse function.
const char *jf_strong_bool_to_str(jf_strong_bool strong_bool);


// Concatenates any amount of NULL-terminated strings. The result will be
// dynamically allocated and will need to be free'd.
//
// Parameters:
//  - n: the number of following arguments, i.e. strings to concatenate.
//  - varargs: a variadic sequence of (const char *) pointing to NULL-terminated
//      strings to be concatenated.
//
// Returns:
//  char * pointing to the malloc'd result.
// CAN FATAL.
char *jf_concat(const size_t n, ...);


// Prints an unsigned, base-10 number to stdout. The function is NEITHER
// reentrant NOR thread-safe.
// IT WILL CAUSE UNDEFINED BEHAVIOUR if the base-10 representation of the
// argument is longer than 20 digits, which means the binary representation of
// the number takes more than 64 bits.
//
// Parameters:
//  - n: The number to print. It is always treated as unsigned and base-10.
//      Regardless of the system's implementation of size_t, it must fit into
//      64 bits for the internal buffer not to overflow.
void jf_print_zu(size_t n);


// Generates a malloc'd string of arbitrary length of random digits.
//
// Parameters:
//  - len: length of the random string, excluding the terminating null btye. If
//      0, a default of 10 will be applied.
//
// Returns:
//  Pointer to the string. It will need be free'd.
// CAN FATAL.
char *jf_generate_random_id(size_t length);


char *jf_make_timestamp(const long long ticks);
size_t jf_clamp_zu(const size_t zu, const size_t min, const size_t max);
void jf_clear_stdin(void);


// Tries to replace the entire bottom line of the terminal buffer with empty
// space, by priting a line of whitespace.
//
// Parameters:
//  - stream: the actual stream that will be printed to.
//      Can be NULL to default to stdout.
//
// CAN'T FAIL.
void jf_term_clear_bottom(FILE *stream);


// Computes length of string, including terminating '\0' byte.
//
// Parameters:
//   - str: the string whose length to compute.
//
// Returns:
//   - 0 if str == NULL;
//   - strlen(str) + 1 if str != NULL.
//
// CAN'T FAIL.
size_t jf_strlen(const char *str);


// Returns a YYYY-mm-dd representation of today's date, shifted to exactly one
// year ago.
//
// Returns:
//  - A pointer to a statically allocated buffer containing the null-terminated
//      string with the date.
//
// CAN'T FAIL.
char *jf_make_date_one_year_ago(void);
///////////////////////////////////////////
#endif
