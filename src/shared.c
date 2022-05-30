#include "shared.h"
#include "config.h"


#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <stdarg.h>

////////// GLOBALS //////////
extern jf_global_state g_state;
extern jf_options g_options;
extern mpv_handle *g_mpv_ctx;
/////////////////////////////


////////// STATIC FUNCTIONS //////////
#ifdef JF_DEBUG
static void jf_menu_item_print_indented(const jf_menu_item *item, const size_t level);
#endif
inline static void jf_growing_buffer_make_space(jf_growing_buffer *buffer,
        size_t to_add);
//////////////////////////////////////


////////// JF_MENU_ITEM //////////
const char *jf_item_type_get_name(const jf_item_type type)
{
    switch (type) {
        case JF_ITEM_TYPE_NONE:
            return "None";
        case JF_ITEM_TYPE_AUDIO:
            return "Audio";
        case JF_ITEM_TYPE_AUDIOBOOK:
            return "Audiobook";
        case JF_ITEM_TYPE_EPISODE:
            return "Episode";
        case JF_ITEM_TYPE_MOVIE:
            return "Movie";
        case JF_ITEM_TYPE_VIDEO_SOURCE:
            return "Video_Source";
        case JF_ITEM_TYPE_VIDEO_SUB:
            return "Video_Sub";
        case JF_ITEM_TYPE_COLLECTION:
            return "Collection_Generic";
        case JF_ITEM_TYPE_COLLECTION_MUSIC:
            return "Collection_Music";
        case JF_ITEM_TYPE_COLLECTION_SERIES:
            return "Collection_Series";
        case JF_ITEM_TYPE_COLLECTION_MOVIES:
            return "Collection_Movies";
        case JF_ITEM_TYPE_COLLECTION_MUSIC_VIDEOS:
            return "Collection_Music_Videos";
        case JF_ITEM_TYPE_USER_VIEW:
            return "User_View";
        case JF_ITEM_TYPE_FOLDER:
            return "Folder";
        case JF_ITEM_TYPE_PLAYLIST:
            return "Playlist";
        case JF_ITEM_TYPE_ARTIST:
            return "Artist";
        case JF_ITEM_TYPE_ALBUM:
            return "Album";
        case JF_ITEM_TYPE_SEASON:
            return "Season";
        case JF_ITEM_TYPE_SERIES:
            return "Series";
        case JF_ITEM_TYPE_SEARCH_RESULT:
            return "Search_Result";
        case JF_ITEM_TYPE_MENU_ROOT:
        case JF_ITEM_TYPE_MENU_FAVORITES:
        case JF_ITEM_TYPE_MENU_CONTINUE:
        case JF_ITEM_TYPE_MENU_NEXT_UP:
        case JF_ITEM_TYPE_MENU_LATEST_ADDED:
        case JF_ITEM_TYPE_MENU_LIBRARIES:
            return "Persistent_Folder";
        default:
            return "Unrecognized";
    }
}


jf_menu_item *jf_menu_item_new(jf_item_type type,
        jf_menu_item **children,
        const char *id,
        const char *name,
        const char *path,
        const long long runtime_ticks,
        const long long playback_ticks)
{
    jf_menu_item *menu_item;
    size_t name_length = jf_strlen(name);
    size_t path_length = jf_strlen(path);

    assert((menu_item = malloc(sizeof(jf_menu_item)
                    + name_length
                    + path_length)) != NULL);

    menu_item->type = type;

    menu_item->children = children;
    menu_item->children_count = 0;
    if (children != NULL) {
        while (*(menu_item->children) != NULL) {
            menu_item->children_count++;
            menu_item->children++;
        }
        menu_item->children = children;
    }

    if (id == NULL) {
        menu_item->id[0] = '\0';
    } else {
        memcpy(menu_item->id, id, JF_ID_LENGTH);
        menu_item->id[JF_ID_LENGTH] = '\0';
    }

    if (name == NULL) {
        menu_item->name = NULL;
    } else {
        menu_item->name = (char *)menu_item + sizeof(jf_menu_item);
        memcpy(menu_item->name, name, name_length);
    }

    if (path == NULL) {
        menu_item->path = NULL;
    } else {
        menu_item->path = (char *)menu_item + sizeof(jf_menu_item) + name_length;
        memcpy(menu_item->path, path, path_length);
    }
    menu_item->runtime_ticks = runtime_ticks;
    menu_item->playback_ticks = playback_ticks;
    
    return menu_item;
}


void jf_menu_item_free(jf_menu_item *menu_item)
{
    size_t i;

    if (menu_item == NULL) {
        return;
    }

    if (! (JF_ITEM_TYPE_IS_PERSISTENT(menu_item->type))) {
        for (i = 0; i < menu_item->children_count; i++) {
            jf_menu_item_free(menu_item->children[i]);
        }
        free(menu_item->children);
        free(menu_item);
    }
}


#ifdef JF_DEBUG
static void jf_menu_item_print_indented(const jf_menu_item *item, const size_t level)
{
    size_t i;

    if (item == NULL) return;

    JF_PRINTF_INDENT("Name: %s\n", item->name);
    JF_PRINTF_INDENT("Path: %s\n", item->path);
    JF_PRINTF_INDENT("Type: %s\n", jf_item_type_get_name(item->type));
    JF_PRINTF_INDENT("Id: %s\n", item->id);
    JF_PRINTF_INDENT("PB ticks: %lld, RT ticks: %lld\n", item->playback_ticks, item->runtime_ticks);
    if (item->children_count > 0) {
        JF_PRINTF_INDENT("Children:\n");
        for (i = 0; i < item->children_count; i++) {
            jf_menu_item_print_indented(item->children[i], level + 1);
        }
    }
}


void jf_menu_item_print(const jf_menu_item *item)
{
    jf_menu_item_print_indented(item, 0);
}
#endif
//////////////////////////////////


////////// THREAD BUFFER //////////
void jf_thread_buffer_init(jf_thread_buffer *tb)
{
    tb->used = 0;
    tb->promiscuous_context = false;
    tb->state = JF_THREAD_BUFFER_STATE_CLEAR;
    tb->item_count = 0;
    assert(pthread_mutex_init(&tb->mut, NULL) == 0);
    assert(pthread_cond_init(&tb->cv_no_data, NULL) == 0);
    assert(pthread_cond_init(&tb->cv_has_data, NULL) == 0);
}
///////////////////////////////////


////////// GROWING BUFFER //////////
void jf_growing_buffer_init(jf_growing_buffer *buffer, const size_t size)
{
    assert((buffer->buf = malloc(size > 0 ? size : 1024)) != NULL);
    buffer->size = size > 0 ? size : 1024;
    buffer->used = 0;
}


jf_growing_buffer *jf_growing_buffer_new(const size_t size)
{
    jf_growing_buffer *buffer;
    assert((buffer = malloc(sizeof(jf_growing_buffer))) != NULL);
    jf_growing_buffer_init(buffer, size);
    return buffer;
}


inline static void jf_growing_buffer_make_space(jf_growing_buffer *buffer,
        const size_t required)
{
    size_t estimate;

    if (buffer == NULL) return;

    if (buffer->used + required > buffer->size) {
        estimate = (buffer->used + required ) / 2 * 3;
        buffer->size = estimate >= buffer->size * 2 ? estimate : buffer->size * 2;
        assert((buffer->buf = realloc(buffer->buf, buffer->size)) != NULL);
    }
}


void jf_growing_buffer_append(jf_growing_buffer *buffer, const void *data,
        size_t length)
{
    if (buffer == NULL) return;

    if (length == 0) {
        length = strlen(data);
    }

    jf_growing_buffer_make_space(buffer, length);
    memcpy(buffer->buf + buffer->used, data, length);
    buffer->used += length;
}


void jf_growing_buffer_sprintf(jf_growing_buffer *buffer,
        size_t offset,
        const char *format,
        ...)
{
    int sprintf_len;
    va_list ap;

    if (buffer == NULL) return;

    if (offset == 0) {
        offset = buffer->used;
    }

    va_start(ap, format);
    // count terminating NULL too or output loses last character
    assert((sprintf_len = vsnprintf(NULL, 0, format, ap) + 1) != -1);
    va_end(ap);

    jf_growing_buffer_make_space(buffer, offset + (size_t)sprintf_len - buffer->used);

    va_start(ap, format);
    // so this DOES write the terminating NULL as well
    assert(vsnprintf(buffer->buf + offset, (size_t)sprintf_len, format, ap)
            == sprintf_len - 1);
    va_end(ap);

    // but we ignore that it's there
    buffer->used += (size_t)sprintf_len - 1;
}


void jf_growing_buffer_empty(jf_growing_buffer *buffer)
{
    if (buffer == NULL) return;
    
    buffer->used = 0;
}


void jf_growing_buffer_free(jf_growing_buffer *buffer)
{
    if (buffer == NULL) return;

    free(buffer->buf);
    free(buffer);
}
////////////////////////////////////


////////// SYNCED QUEUE //////////
jf_synced_queue *jf_synced_queue_new(const size_t slots)
{
    jf_synced_queue *q;

    assert((q = malloc(sizeof(jf_synced_queue))) != NULL);
    assert((q->slots = calloc(slots, sizeof(void *))) != NULL);
    q->slot_count = slots;
    q->current = 0;
    q->next = 0;
    assert(pthread_mutex_init(&q->mut, NULL) == 0);
    assert(pthread_cond_init(&q->cv_is_empty, NULL) == 0);
    assert(pthread_cond_init(&q->cv_is_full, NULL) == 0);
    return q;
}


void jf_synced_queue_free(jf_synced_queue *q)
{
    free(q);
}


void jf_synced_queue_enqueue(jf_synced_queue *q, const void *payload)
{
    if (payload == NULL) return;

    pthread_mutex_lock(&q->mut);
    while (q->slots[q->next] != NULL) {
        pthread_cond_wait(&q->cv_is_full, &q->mut);
    }
    q->slots[q->next] = payload;
    q->next = (q->next + 1) % q->slot_count;
    pthread_mutex_unlock(&q->mut);
    pthread_cond_signal(&q->cv_is_empty);
}


void *jf_synced_queue_dequeue(jf_synced_queue *q)
{
    void *payload;

    pthread_mutex_lock(&q->mut);
    while (q->slots[q->current] == NULL) {
        pthread_cond_wait(&q->cv_is_empty, &q->mut);
    }
    payload = (void *)q->slots[q->current];
    q->slots[q->current] = NULL;
    q->current = (q->current + 1) % q->slot_count;
    pthread_mutex_unlock(&q->mut);
    pthread_cond_signal(&q->cv_is_full);

    return payload;
}
//////////////////////////////////

#define STRNCASECMP_LITERAL(_str, _lit, _len) strncasecmp(_str, _lit, _len > JF_STATIC_STRLEN(_lit) ? JF_STATIC_STRLEN(_lit) : _len)

////////// MISCELLANEOUS GARBAGE //////////
bool jf_strong_bool_parse(const char *str,
        const size_t len,
        jf_strong_bool *out)
{
    size_t l;

    if (str == NULL) return false;

    l = len > 0 ? len : strlen(str);

    if (STRNCASECMP_LITERAL(str, "no", l) == 0) {
        *out = JF_STRONG_BOOL_NO;
        return true;
    }
    if (STRNCASECMP_LITERAL(str, "yes", l) == 0) {
        *out = JF_STRONG_BOOL_YES;
        return true;
    }
    if (STRNCASECMP_LITERAL(str, "force", l) == 0) {
        *out = JF_STRONG_BOOL_FORCE;
        return true;
    }

    return false;
}


char *jf_concat(size_t n, ...)
{
    char *buf;
    size_t *argv_len;
    size_t len = 0;
    size_t i;
    va_list ap;

    assert((argv_len = malloc(sizeof(size_t) * n)) != NULL);
    va_start(ap, n);
    for (i = 0; i < n; i++) {
        argv_len[i] = strlen(va_arg(ap, const char*));
        len += argv_len[i];
    }
    va_end(ap);

    assert((buf = malloc(len + 1)) != NULL);
    len = 0;
    va_start(ap, n);
    for (i = 0; i < n; i++) {
        memcpy(buf + len, va_arg(ap, const char*), argv_len[i]);
        len += argv_len[i];
    }
    buf[len] = '\0';
    va_end(ap);
    
    free(argv_len);
    return buf;
}


void jf_print_zu(size_t n)                                               
{                                                                       
    static char str[20];                                              
    unsigned char i = 0;
    do {
        str[i++] = n % 10 + '0';
    } while ((n /= 10) > 0);
    while (i-- != 0) {
        fwrite(str + i, 1, 1, stdout);
    }
}


char *jf_generate_random_id(size_t len)
{
    char *rand_id;

    // default length
    len = len > 0 ? len : 10;

    assert((rand_id = malloc(len + 1)) != NULL);
    rand_id[len] = '\0';
    for (; len > 0; len--) {
        rand_id[len - 1] = '0' + random() % 10;
    }
    return rand_id;
}


char *jf_make_timestamp(const long long ticks)
{
    char *str;
    unsigned char seconds, minutes, hours;
    seconds = (unsigned char)((ticks / 10000000) % 60);
    minutes = (unsigned char)((ticks / 10000000 / 60) % 60);
    hours = (unsigned char)(ticks / 10000000 / 60 / 60);

    // allocate with overestimate. we shan't cry
    assert((str = malloc(sizeof("xxx:xx:xx"))) != NULL);
    snprintf(str, sizeof("xxx:xx:xx"), "%02u:%02u:%02u", hours, minutes, seconds);
    return str;
}


inline size_t jf_clamp_zu(const size_t zu, const size_t min,
        const size_t max)
{
    return zu < min ? min : zu > max ? max : zu;
}


inline void jf_clear_stdin()
{
    fcntl(0, F_SETFL, fcntl(0, F_GETFL)|O_NONBLOCK);
    while (getchar() != EOF) ;
    fcntl(0, F_SETFL, fcntl(0, F_GETFL)& ~O_NONBLOCK);
}


void jf_term_clear_bottom(FILE *stream)
{
    struct winsize ws;
    size_t i;

    if (stream == NULL) {
        stream = stdout;
    }

    if (ioctl(fileno(stream), TIOCGWINSZ, &ws) < 0 || ws.ws_col == 0) return;

    putc('\r', stream);
    for (i = 0; i < ws.ws_col; i++) {
        putc(' ', stream);
    }
    putc('\r', stream);
}


size_t jf_strlen(const char *str)
{
    return str == NULL ? 0 : strlen(str) + 1;
}
///////////////////////////////////////////
