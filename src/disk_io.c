#include "disk_io.h"

////////// GLOBALS //////////
extern jf_global_state g_state;
/////////////////////////////


////////// STATIC VARIABLES //////////
static char s_buffer[JF_DISK_BUFFER_SIZE];
static size_t s_payload_count;
static FILE *s_payload_header;
static FILE *s_payload_body;
static size_t s_playlist_count;
static FILE *s_playlist_header;
static FILE *s_playlist_body;
//////////////////////////////////////


bool jf_disk_refresh()
{
	char *tmp;
	if (access(g_state.runtime_dir, F_OK) != 0) {
		errno = 0;
		if (mkdir(g_state.runtime_dir, S_IRWXU) == -1) {
			int mkdir_errno = errno;
			JF_STATIC_PRINT("FATAL: could not create runtime directory ");
			write(2, g_state.runtime_dir, strlen(g_state.runtime_dir));
			JF_STATIC_PRINT(": ");
			write(2, strerror(mkdir_errno), strlen(strerror(mkdir_errno)));
			JF_STATIC_PRINT("\n");
			return false;
		}
	}

	JF_DISK_OPEN_FILE_FATAL(_payload_header);
	JF_DISK_OPEN_FILE_FATAL(_payload_body);
	s_payload_count = 0;
	JF_DISK_OPEN_FILE_FATAL(_playlist_header);
	JF_DISK_OPEN_FILE_FATAL(_playlist_body);
	s_playlist_count = 0;

	return true;
}


void jf_disk_clear()
{
	char *tmp;

	JF_DISK_CLOSE_DELETE_FILE(_payload_header);
	JF_DISK_CLOSE_DELETE_FILE(_payload_body);
	JF_DISK_CLOSE_DELETE_FILE(_playlist_header);
	JF_DISK_CLOSE_DELETE_FILE(_playlist_body);
}


bool jf_playlist_add(jf_menu_item *item)
{
	int starting_body_offset;

	if (item == NULL) {
		return false;
	}

	if (JF_ITEM_TYPE_IS_FOLDER(item->type)) {
		return false;
	}

	// TODO error check!
	fseek(s_playlist_header, 0, SEEK_END);
	starting_body_offset = fseek(s_playlist_body, 0, SEEK_END);
	fwrite(&starting_body_offset, sizeof(int), 1, s_playlist_header);

	fwrite(&(item->type), sizeof(jf_item_type), 1, s_playlist_body);
	fwrite(item->id, 1, sizeof(item->id), s_playlist_body);
	fwrite(item->name, 1, item->name == NULL ? 0 : strlen(item->name), s_playlist_body);
	fwrite(&("\0"), 1, 1, s_playlist_body);

	s_playlist_count++;

	return true;
}


// 1-indexed
jf_menu_item *jf_disk_playlist_get(size_t n)
{
	size_t i = 0;
	long body_offset;
	jf_menu_item *item;

	if (n == 0 || n > s_playlist_count) {
		return NULL;
	}

	if ((item = jf_menu_item_new(JF_ITEM_TYPE_NONE, NULL, NULL, NULL)) == NULL) {
		return NULL;
	}

	// TODO error check!
	n--; // 0-indexed
	fseek(s_playlist_header, (long)(n * sizeof(int)), SEEK_SET);
	fread(&body_offset, sizeof(int), 1, s_playlist_header);
	fseek(s_playlist_body, body_offset, SEEK_SET);

	fread(&(item->type), sizeof(jf_item_type), 1, s_playlist_body);
	fread(item->id, 1, sizeof(item->id), s_playlist_body);

	while (true) {
		if (i >= JF_DISK_BUFFER_SIZE) {
			jf_menu_item_free(item);
			return NULL;
		}
		fread(s_buffer + i, 1, 1, s_playlist_body);
		if (s_buffer[i] == '\0') {
			item->name = strdup(s_buffer);
			break;
		}
		i++;
	}

	return item;
}
