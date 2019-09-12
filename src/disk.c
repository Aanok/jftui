#include "disk.h"

////////// GLOBALS //////////
extern jf_global_state g_state;
/////////////////////////////


////////// STATIC VARIABLES //////////
static jf_growing_buffer *s_buffer = NULL;
static jf_file_cache s_payload;
static jf_file_cache s_playlist;
//////////////////////////////////////


////////// STATIC FUNCTIONS ///////////
static bool jf_disk_add_item(jf_file_cache *cache, const jf_menu_item *item);
static jf_menu_item *jf_disk_get_item(jf_file_cache *cache, size_t n);
///////////////////////////////////////


// item must not be NULL
static bool jf_disk_add_item(jf_file_cache *cache, const jf_menu_item *item)
{
	long starting_body_offset;

	// header and alignment
	JF_DISK_OP_SIMPLE_FATAL(fseek(cache->header, 0, SEEK_END), 0, false,
			"could not seek to end of cache header");
	JF_DISK_OP_SIMPLE_FATAL(fseek(cache->body, 0, SEEK_END), 0, false,
			"could not seek to end of cache body");
	if ((starting_body_offset = ftell(cache->body)) == -1) {
		int backup_errno = errno;
		fprintf(stderr, "FATAL: could not ftell cache body: %s.\n", strerror(backup_errno));
		return false;
	}
	JF_DISK_OP_SIMPLE_FATAL(fwrite(&starting_body_offset, sizeof(long), 1, cache->header),
			1, false, "could not write offset in cache header");

	// body
	JF_DISK_OP_SIMPLE_FATAL(fwrite(&(item->type), sizeof(jf_item_type), 1, cache->body),
			1, false, "could not write item type in cache body");
	JF_DISK_OP_SIMPLE_FATAL(fwrite(item->id, 1, sizeof(item->id), cache->body),
			sizeof(item->id), false, "could not write item id in cache body");
	if (item->name != NULL) {
		size_t len = strlen(item->name);
		JF_DISK_OP_SIMPLE_FATAL(fwrite(item->name, 1, len, cache->body), len,
			false, "could not write item name in cache body");
	}
	JF_DISK_OP_SIMPLE_FATAL(fwrite(&("\0"), 1, 1, cache->body), 1, false,
			"could not NULL-terminate item name in cache body");
	JF_DISK_OP_SIMPLE_FATAL(fwrite(&(item->runtime_ticks), sizeof(long long), 1, cache->body),
			1, false, "could not write item runtime_ticks in cache body");
	JF_DISK_OP_SIMPLE_FATAL(fwrite(&(item->playback_ticks), sizeof(long long), 1, cache->body),
			1, false, "could not write item playback_ticks in cache body");

	cache->count++;

	return true;
}


static jf_menu_item *jf_disk_get_item(jf_file_cache *cache, size_t n)
{
	jf_menu_item *item;
	char tmp[1];

	if (n == 0 || n > cache->count) {
		return NULL;
	}

	if ((item = malloc(sizeof(jf_menu_item))) == NULL) {
		return NULL;
	}

	JF_DISK_ALIGN_TO_FATAL(cache, n);
	JF_DISK_OP_FATAL(fread(&(item->type), sizeof(jf_item_type), 1, cache->body), 1,
			NULL, "could not read type for item %zu in a cache body", n);
	JF_DISK_OP_FATAL(fread(item->id, 1, sizeof(item->id), cache->body), sizeof(item->id),
			NULL, "could not read id for item %zu in a cache body", n);
	item->children = NULL;
	jf_growing_buffer_empty(s_buffer);
	while (true) {
		JF_DISK_OP_FATAL(fread(tmp, 1, 1, cache->body), 1,
				NULL, "could not read name for item %zu in a cache body", n);
		jf_growing_buffer_append(s_buffer, tmp, 1);
		if (tmp[0] == '\0') {
			item->name = strdup(s_buffer->buf);
			break;
		}
	}
	JF_DISK_OP_FATAL(fread(&(item->runtime_ticks), sizeof(long long), 1, cache->body), 1,
			NULL, "could not read runtime_ticks for item %zu in a cache body", n);
	JF_DISK_OP_FATAL(fread(&(item->playback_ticks), sizeof(long long), 1, cache->body), 1,
			NULL, "could not read playback_ticks for item %zu in a cache body", n);

	return item;
}


bool jf_disk_init()
{
	if (access(g_state.runtime_dir, F_OK) != 0) {
		errno = 0;
		if (mkdir(g_state.runtime_dir, S_IRWXU) == -1) {
			int mkdir_errno = errno;
			fprintf(stderr, "FATAL: could not create runtime directory %s: %s.\n",
					g_state.runtime_dir, strerror(mkdir_errno));
			return false;
		}
	}

	if (s_buffer == NULL) {
		if ((s_buffer = jf_growing_buffer_new(0)) == NULL) {
			fprintf(stderr, "FATAL: jf_disk_refresh could not allocate growing buffer.\n");
			return false;
		}
	}

	JF_DISK_OPEN_FILE_FATAL(s_payload, header);
	JF_DISK_OPEN_FILE_FATAL(s_payload, body);
	s_payload.count = 0;
	JF_DISK_OPEN_FILE_FATAL(s_playlist, header);
	JF_DISK_OPEN_FILE_FATAL(s_playlist, body);
	s_playlist.count = 0;

	return true;
}


bool jf_disk_refresh()
{
	JF_DISK_REOPEN_FILE_FATAL(s_payload, header);
	JF_DISK_REOPEN_FILE_FATAL(s_payload, body);
	s_payload.count = 0;
	JF_DISK_REOPEN_FILE_FATAL(s_playlist, header);
	JF_DISK_REOPEN_FILE_FATAL(s_playlist, body);
	s_playlist.count = 0;
	return true;
}


void jf_disk_clear()
{
	JF_DISK_CLOSE_DELETE_FILE(s_payload, header);
	JF_DISK_CLOSE_DELETE_FILE(s_payload, body);
	JF_DISK_CLOSE_DELETE_FILE(s_playlist, header);
	JF_DISK_CLOSE_DELETE_FILE(s_playlist, body);
}


bool jf_disk_payload_add_item(const jf_menu_item *item)
{
	if (item == NULL) {
		return false;
	}
	return jf_disk_add_item(&s_payload, item);
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

	JF_DISK_ALIGN_TO_FATAL(&s_payload, n);
	JF_DISK_OP_FATAL(fread(&(item_type), sizeof(jf_item_type), 1, s_payload.body),
			1, JF_ITEM_TYPE_NONE, "could not read type for item %zu in s_payload.body", n);

	return item_type;
}


size_t jf_disk_payload_item_count()
{
	return s_payload.count;
}


bool jf_disk_playlist_add_item(const jf_menu_item *item)
{
	if (item == NULL || JF_ITEM_TYPE_IS_FOLDER(item->type)) {
		return false;
	}
	return jf_disk_add_item(&s_playlist, item);
}


jf_menu_item *jf_disk_playlist_get_item(const size_t n)
{
	return jf_disk_get_item(&s_playlist, n);
}


size_t jf_disk_playlist_item_count()
{
	return s_playlist.count;
}
