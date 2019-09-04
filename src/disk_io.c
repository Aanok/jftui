#include "disk_io.h"

////////// GLOBALS //////////
extern jf_global_state g_state;
/////////////////////////////


////////// STATIC VARIABLES //////////
static FILE *s_payload_header;
static FILE *s_payload_body;
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
	JF_DISK_OPEN_FILE_FATAL(_playlist_header);
	JF_DISK_OPEN_FILE_FATAL(_playlist_body);

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
