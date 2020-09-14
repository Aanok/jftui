#include "disk.h"
#include "shared.h"
#include "menu.h"

#include <stdlib.h> // malloc, getenv
#include <stdio.h> // fwrite etc.
#include <unistd.h> // unlink
#include <sys/stat.h> //mkdir
#include <string.h>
#include <assert.h>


////////// GLOBALS //////////
extern jf_global_state g_state;
/////////////////////////////


////////// STATIC VARIABLES //////////
static jf_growing_buffer *s_buffer = NULL;
static char *s_file_prefix = NULL;
static jf_file_cache s_payload = (jf_file_cache){ 0 };
static jf_file_cache s_playlist = (jf_file_cache){ 0 };
//////////////////////////////////////


////////// STATIC FUNCTIONS ///////////
static inline void jf_disk_align_to(jf_file_cache *cache, const size_t n);
static inline void jf_disk_open(jf_file_cache *cache);
static void jf_disk_add_next(jf_file_cache *cache, const jf_menu_item *item);
static void jf_disk_add_item(jf_file_cache *cache, const jf_menu_item *item);
static jf_menu_item *jf_disk_get_next(jf_file_cache *cache);
static jf_menu_item *jf_disk_get_item(jf_file_cache *cache, const size_t n);
///////////////////////////////////////


static inline void jf_disk_open(jf_file_cache *cache)
{
    assert((cache->header = fopen(cache->header_path, "w+")) != NULL);
    assert((cache->body = fopen(cache->body_path, "w+")) != NULL);
    cache->count = 0;
    // no error checking on these two, nothing to do if they fail
    // at worst we pollute the temp dir, which is not the end of the world
    unlink(cache->header_path);
    unlink(cache->body_path);
}


static inline void
jf_disk_align_to(jf_file_cache *cache, const size_t n)
{
    long body_offset;
    assert(fseek(cache->header, (long)((n - 1) * sizeof(long)), SEEK_SET) == 0);
    assert(fread(&body_offset, sizeof(long), 1, cache->header) == 1);
    assert(fseek(cache->body, body_offset, SEEK_SET) == 0);
}


static void jf_disk_add_next(jf_file_cache *cache, const jf_menu_item *item)
{
    size_t name_length, i;

    assert(fwrite(&(item->type), sizeof(jf_item_type), 1, cache->body) == 1);
    assert(fwrite(item->id, 1, sizeof(item->id), cache->body) == sizeof(item->id));
    name_length = item->name == NULL ? 0 : strlen(item->name);
    assert(fwrite(item->name, 1, name_length, cache->body) == name_length);
    assert(fwrite(&"\0", 1, 1, cache->body) == 1);
    assert(fwrite(&(item->runtime_ticks), sizeof(long long), 1, cache->body) == 1);
    assert(fwrite(&(item->playback_ticks), sizeof(long long), 1, cache->body) == 1);
    assert(fwrite(&(item->children_count), sizeof(size_t), 1, cache->body) == 1);
    for (i = 0; i < item->children_count; i++) {
        jf_disk_add_next(cache, item->children[i]);
    }
}


static void jf_disk_add_item(jf_file_cache *cache, const jf_menu_item *item)
{
    long starting_body_offset;

    assert(item != NULL);

    assert(fseek(cache->header, 0, SEEK_END) == 0);
    assert(fseek(cache->body, 0, SEEK_END) == 0);
    assert((starting_body_offset = ftell(cache->body)) != -1);
    assert(fwrite(&starting_body_offset, sizeof(long), 1, cache->header) == 1);

    jf_disk_add_next(cache, item);
    cache->count++;
}


static void jf_disk_read_to_null_to_buffer(jf_file_cache *cache)
{
    char tmp;

    jf_growing_buffer_empty(s_buffer);
    while (true) {
        assert(fread(&tmp, 1, 1, cache->body) == 1);
        jf_growing_buffer_append(s_buffer, &tmp, 1);
        if (tmp == '\0') break;
    }
}


static jf_menu_item *jf_disk_get_next(jf_file_cache *cache)
{
    jf_menu_item *item;
    size_t i;

    assert((item = malloc(sizeof(jf_menu_item))) != NULL);

    assert(fread(&(item->type), sizeof(jf_item_type), 1, cache->body) == 1);
    assert(fread(item->id, 1, sizeof(item->id), cache->body) == sizeof(item->id));
    jf_disk_read_to_null_to_buffer(cache);
    item->name = strdup(s_buffer->buf);
    assert(fread(&(item->runtime_ticks), sizeof(long long), 1, cache->body) == 1);
    assert(fread(&(item->playback_ticks), sizeof(long long), 1, cache->body) == 1);
    assert(fread(&(item->children_count), sizeof(size_t), 1, cache->body) == 1);
    if (item->children_count > 0) {
        assert((item->children = malloc(item->children_count * sizeof(jf_menu_item *))) != NULL);
        for (i = 0; i < item->children_count; i++) {
            item->children[i] = jf_disk_get_next(cache);
        }
    } else {
        item->children = NULL;
    }

    return item;
}


static jf_menu_item *jf_disk_get_item(jf_file_cache *cache, const size_t n)
{
    if (n == 0 || n > cache->count) return NULL;

    jf_disk_align_to(cache, n);
    return jf_disk_get_next(cache);
}


void jf_disk_init()
{
    char *tmp_dir;
    if ((tmp_dir = getenv("TMPDIR")) == NULL) {
#ifdef P_tmpdir
        tmp_dir = P_tmpdir;
#else
        tmp_dir = "/tmp";
#endif
    }
    assert(access(tmp_dir, F_OK) == 0);
    assert((s_file_prefix = malloc((size_t)snprintf(NULL, 0,
            "%s/jftui_%d_XXXXXX",
            tmp_dir, getpid()) + 1)) != NULL);
    sprintf(s_file_prefix, "%s/jftui_%d_XXXXXX", tmp_dir, getpid());

    if (s_buffer == NULL) {
        assert((s_buffer = jf_growing_buffer_new(0)) != NULL);
    }

    assert((s_payload.header_path = jf_concat(2, s_file_prefix, "_s_payload_header")) != NULL);
    assert((s_payload.body_path = jf_concat(2, s_file_prefix, "_s_payload_body")) != NULL);
    assert((s_playlist.header_path = jf_concat(2, s_file_prefix, "_s_playlist_header")) != NULL);
    assert((s_playlist.body_path = jf_concat(2, s_file_prefix, "_s_playlist_body")) != NULL);

    jf_disk_open(&s_payload);
    jf_disk_open(&s_playlist);
}


void jf_disk_refresh()
{
    assert(fclose(s_payload.header) == 0);
    assert(fclose(s_payload.body) == 0);
    jf_disk_open(&s_payload);
    assert(fclose(s_playlist.header) == 0);
    assert(fclose(s_playlist.body) == 0);
    jf_disk_open(&s_playlist);
}


void jf_disk_payload_add_item(const jf_menu_item *item)
{
    if (item == NULL) return;
    jf_disk_add_item(&s_payload, item);
}


jf_menu_item *jf_disk_payload_get_item(const size_t n)
{
    return jf_disk_get_item(&s_payload, n);
}


const char *jf_disk_playlist_get_item_name(const size_t n)
{

    if (n == 0 || n > s_playlist.count) {
        return "Warning: requesting item out of bounds. This is a bug.";
    }

    jf_disk_align_to(&s_playlist, n);
    assert(fseek(s_playlist.body,
                // let him who hath understanding reckon the number of the beast!
                sizeof(jf_item_type) + sizeof(((jf_menu_item *)666)->id),
                SEEK_CUR) == 0);

    jf_disk_read_to_null_to_buffer(&s_playlist);

    return (const char *)s_buffer->buf;
}


jf_item_type jf_disk_payload_get_type(const size_t n)
{
    jf_item_type item_type;

    if (n == 0 || n > s_payload.count) {
        return JF_ITEM_TYPE_NONE;
    }

    jf_disk_align_to(&s_payload, n);
    if (fread(&(item_type), sizeof(jf_item_type), 1, s_payload.body) != 1) {
        fprintf(stderr, "Warning: jf_payload_get_type: could not read type for item %zu in s_payload.body.\n", n);
        return JF_ITEM_TYPE_NONE;
    }
    return item_type;
}


size_t jf_disk_payload_item_count()
{
    return s_payload.count;
}


void jf_disk_playlist_add_item(const jf_menu_item *item)
{
    if (item == NULL || JF_ITEM_TYPE_IS_FOLDER(item->type)) return;
    jf_disk_add_item(&s_playlist, item);
}


jf_menu_item *jf_disk_playlist_get_item(const size_t n)
{
    return jf_disk_get_item(&s_playlist, n);
}


void jf_disk_playlist_replace_item(const size_t n, const jf_menu_item *item)
{
    long starting_body_offset;

    assert(item != NULL);

    // overwrite old offset in header
    assert(fseek(s_playlist.header, (long)((n - 1) * sizeof(long)), SEEK_SET) == 0);
    assert(fseek(s_playlist.body, 0, SEEK_END) == 0);
    assert((starting_body_offset = ftell(s_playlist.body)) != -1);
    assert(fwrite(&starting_body_offset, sizeof(long), 1, s_playlist.header) == 1);

    // add replacement to tail
    jf_disk_add_next(&s_playlist, item);
}


size_t jf_disk_playlist_item_count()
{
    return s_playlist.count;
}
