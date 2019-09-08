#include "disk_io.h"

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

	// TODO error check!
	fseek(cache->header, 0, SEEK_END);
	fseek(cache->body, 0, SEEK_END);
	starting_body_offset = ftell(cache->body);
	fwrite(&starting_body_offset, sizeof(long), 1, cache->header);

	fwrite(&(item->type), sizeof(jf_item_type), 1, cache->body);
	fwrite(item->id, 1, sizeof(item->id), cache->body);
	fwrite(item->name, 1, item->name == NULL ? 0 : strlen(item->name), cache->body);
	fwrite(&("\0"), 1, 1, cache->body);

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

	if ((item = jf_menu_item_new(JF_ITEM_TYPE_NONE, NULL, NULL, NULL)) == NULL) {
		return NULL;
	}

	// TODO error check!
	JF_DISK_ALIGN_TO(cache, n);

	fread(&(item->type), sizeof(jf_item_type), 1, cache->body);
	fread(item->id, 1, sizeof(item->id), cache->body);
	jf_growing_buffer_empty(s_buffer);
	while (true) {
		fread(tmp, 1, 1, cache->body);
		jf_growing_buffer_append(s_buffer, tmp, 1);
		if (tmp[0] == '\0') {
			item->name = strdup(s_buffer->buf);
			break;
		}
	}
	printf("DEBUG: got item, type: %d, id: %s, name %s\n", item->type, item->id, item->name);

	return item;
}


bool jf_disk_refresh()
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
	// TODO error check!
	JF_DISK_ALIGN_TO(&s_payload, n);

	fread(&(item_type), sizeof(jf_item_type), 1, s_payload.body);

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


// 1-indexed
jf_menu_item *jf_disk_playlist_get_item(const size_t n)
{
	return jf_disk_get_item(&s_playlist, n);
}


size_t jf_disk_playlist_item_count()
{
	return s_playlist.count;
}
