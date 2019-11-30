#include "disk.h"

////////// GLOBALS //////////
extern jf_global_state g_state;
/////////////////////////////


////////// STATIC VARIABLES //////////
static jf_growing_buffer *s_buffer = NULL;
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
    assert(fwrite(&(item->children_count), sizeof(size_t), 1, cache->body) == 1);
    for (i = 0; i < item->children_count; i++) {
        jf_disk_add_next(cache, item->children[i]);
    }
    assert(fwrite(item->id, 1, sizeof(item->id), cache->body) == sizeof(item->id));
    name_length = item->name == NULL ? 0 : strlen(item->name);
    assert(fwrite(item->name, 1, name_length, cache->body) == name_length);
    assert(fwrite(&"\0", 1, 1, cache->body) == 1);
    assert(fwrite(&(item->runtime_ticks), sizeof(long long), 1, cache->body) == 1);
    assert(fwrite(&(item->playback_ticks), sizeof(long long), 1, cache->body) == 1);
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


static jf_menu_item *jf_disk_get_next(jf_file_cache *cache)
{
    jf_menu_item *item;
    char tmp[1];
    size_t i;

    assert((item = malloc(sizeof(jf_menu_item))) != NULL);

    assert(fread(&(item->type), sizeof(jf_item_type), 1, cache->body) == 1);
    assert(fread(&(item->children_count), sizeof(size_t), 1, cache->body) == 1);
    if (item->children_count > 0) {
        assert((item->children = malloc(item->children_count * sizeof(jf_menu_item *))) != NULL);
        for (i = 0; i < item->children_count; i++) {
            item->children[i] = jf_disk_get_next(cache);
        }
    } else {
        item->children = NULL;
    }
    assert(fread(item->id, 1, sizeof(item->id), cache->body) == sizeof(item->id));
    jf_growing_buffer_empty(s_buffer);
    while (true) {
        assert(fread(tmp, 1, 1, cache->body) == 1);
        jf_growing_buffer_append(s_buffer, tmp, 1);
        if (*tmp == '\0') {
            item->name = strdup(s_buffer->buf);
            break;
        }
    }
    assert(fread(&(item->runtime_ticks), sizeof(long long), 1, cache->body) == 1);
    assert(fread(&(item->playback_ticks), sizeof(long long), 1, cache->body) == 1);

    return item;
}


static jf_menu_item *jf_disk_get_item(jf_file_cache *cache, const size_t n)
{
    if (n == 0 || n > cache->count) return NULL;

    jf_disk_align_to(cache, n);
    return jf_disk_get_next(cache);
}


char *jf_disk_get_default_runtime_dir()
{
    char *dir;
    if ((dir = getenv("XDG_DATA_HOME")) == NULL) {
        if ((dir = getenv("HOME")) != NULL) {
            dir = jf_concat(2, getenv("HOME"), "/.local/share/jftui");
        }
    } else {
        dir = jf_concat(2, dir, "/jftui");
    }

    if (dir == NULL) {
        fprintf(stderr, "FATAL: could not acquire runtime directory location. $HOME could not be read and --runtime-dir was not passed.\n");
        jf_exit(JF_EXIT_FAILURE);
    }
    return dir;
}


void jf_disk_init()
{
    if (access(g_state.runtime_dir, F_OK) != 0) {
        assert(mkdir(g_state.runtime_dir, S_IRWXU) != -1);
    }

    if (s_buffer == NULL) assert((s_buffer = jf_growing_buffer_new(0)) != NULL);

    assert((s_payload.header_path = jf_concat(2, g_state.runtime_dir, "/s_payload_header")) != NULL);
    assert((s_payload.body_path = jf_concat(2, g_state.runtime_dir, "/s_payload_body")) != NULL);
    assert((s_playlist.header_path = jf_concat(2, g_state.runtime_dir, "/s_playlist_header")) != NULL);
    assert((s_playlist.body_path = jf_concat(2, g_state.runtime_dir, "/s_playlist_body")) != NULL);

    if ((access(s_payload.header_path, F_OK)
                && access(s_payload.body_path, F_OK)
                && access(s_playlist.header_path, F_OK)
                && access(s_playlist.body_path, F_OK)) == 0) {
        fprintf(stderr, "Warning: there are files from another jftui session in %s.\n", g_state.runtime_dir);
        fprintf(stderr, "If you want to run multiple instances concurrently, make sure to specify a distinct --runtime-dir for each one after the first or they will interfere with each other.\n");
        fprintf(stderr, "(if jftui terminated abruptly on the last run using this same runtime-dir, you may ignore this warning)\n\n");
    }

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


void jf_disk_clear()
{
    if (s_payload.header_path != NULL) unlink(s_payload.header_path);
    if (s_payload.body_path != NULL) unlink(s_payload.body_path);
    if (s_playlist.header_path != NULL) unlink(s_playlist.header_path);
    if (s_playlist.body_path != NULL) unlink(s_playlist.body_path);
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
    assert((starting_body_offset = ftell(s_playlist.body)) != -1);
    assert(fwrite(&starting_body_offset, sizeof(long), 1, s_playlist.header) == 1);

    // add replacement to tail
    assert(fseek(s_playlist.body, 0, SEEK_END) == 0);
    jf_disk_add_next(&s_playlist, item);
}


size_t jf_disk_playlist_item_count()
{
    return s_playlist.count;
}
