#ifndef _JF_DISK_IO
#define _JF_DISK_IO

#include <stdio.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>

#include "shared.h"

////////// CODE MACROS //////////
#define JF_DISK_OPEN_FILE_FATAL(suffix)												\
	do {																			\
		tmp = jf_concat(4, g_state.runtime_dir, "/", g_state.session_id, #suffix);	\
		errno = 0;																	\
		if ((s ## suffix = fopen(tmp, "w+")) == NULL) {								\
			int fopen_errno = errno;												\
			fprintf(stderr, "FATAL: could not open file %s: %s.\n",					\
					tmp, strerror(fopen_errno));									\
			free(tmp);																\
			return false;															\
		}																			\
		free(tmp);																	\
	} while (false)

#define JF_DISK_CLOSE_DELETE_FILE(suffix)											\
	do {																			\
		fclose(s ## suffix);														\
		tmp = jf_concat(4, g_state.runtime_dir, "/", g_state.session_id, #suffix);	\
		errno = 0;																	\
		if (unlink(tmp) != 0) {														\
			int unlink_errno = errno;												\
			fprintf(stderr, "WARNING: could not delete file %s: %s.\n",				\
					tmp, strerror(unlink_errno));									\
		}																			\
		free(tmp);																	\
	} while (false)
/////////////////////////////////


////////// CONSTANTS //////////
#define JF_DISK_BUFFER_SIZE 1024
///////////////////////////////


////////// FUNCTION STUBS //////////
bool jf_disk_refresh(void);

void jf_disk_clear(void);


bool jf_disk_playlist_add(const jf_menu_item *item);

// 1-indexed
jf_menu_item *jf_disk_playlist_get(size_t n);

size_t jf_disk_playlist_count(void);
////////////////////////////////////
#endif
