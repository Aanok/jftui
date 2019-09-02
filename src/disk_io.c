#include "disk_io.h"

////////// GLOBALS //////////
extern jf_global_state g_state;
/////////////////////////////


////////// STATIC VARIABLES //////////
static FILE *s_header;
static FILE *s_body;
static FILE *s_playlist;
//////////////////////////////////////


char *jf_disk_get_default_dir()
{
	char *str;
	if ((str = getenv("XDG_DATA_HOME")) == NULL) {
		if ((str = getenv("HOME")) != NULL) {
			str = jf_concat(2, getenv("HOME"), "/.local/share/jftui");
		}
	} else {
		str = jf_concat(2, str, "/jftui");
	}
	return str;

}


bool jf_disk_refresh()
{
	char *tmp;
	if (access(g_state.runtime_dir, F_OK) != 0) {
		errno = 0;
		if (mkdir(g_state.runtime_dir, S_IRUSR | S_IWUSR) == -1) {
			int mkdir_errno = errno;
			JF_STATIC_PRINT("FATAL: could not create runtime directory ");
			write(2, g_state.runtime_dir, strlen(g_state.runtime_dir));
			JF_STATIC_PRINT(": ");
			write(2, strerror(mkdir_errno), strlen(strerror(mkdir_errno)));
			JF_STATIC_PRINT("\n");
			return false;
		}
	}

	JF_DISK_OPEN_FILE_FATAL(_header);
	JF_DISK_OPEN_FILE_FATAL(_body);
	JF_DISK_OPEN_FILE_FATAL(_playlist);

	return true;
}
