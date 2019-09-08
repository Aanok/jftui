#ifndef _JF_DISK_IO
#define _JF_DISK_IO

#include <stdio.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>

#include "shared.h"

////////// CODE MACROS //////////
#define JF_DISK_OPEN_FILE_FATAL(cache, kind)												\
	do {																					\
		char *tmp;																			\
		tmp = jf_concat(4, g_state.runtime_dir, "/", g_state.session_id, "_" #cache "_" #kind);	\
		errno = 0;																			\
		if ((cache.kind = fopen(tmp, "w+")) == NULL) {										\
			int fopen_errno = errno;														\
			fprintf(stderr, "FATAL: could not open file %s: %s.\n",							\
					tmp, strerror(fopen_errno));											\
			free(tmp);																		\
			return false;																	\
		}																					\
		free(tmp);																			\
	} while (false)

#define JF_DISK_CLOSE_DELETE_FILE(cache, kind)												\
	do {																					\
		char *tmp;																			\
		fclose(cache.kind);																	\
		tmp = jf_concat(4, g_state.runtime_dir, "/", g_state.session_id, "_" #cache "_" #kind);	\
		errno = 0;																			\
		if (unlink(tmp) != 0) {																\
			int unlink_errno = errno;														\
			fprintf(stderr, "WARNING: could not delete file %s: %s.\n",						\
					tmp, strerror(unlink_errno));											\
		}																					\
		free(tmp);																			\
	} while (false)

// TODO error checking!
#define JF_DISK_ALIGN_TO(cache, nth)										\
	do {																	\
		long body_offset;													\
		fseek((cache)->header, (long)(((nth) - 1) * sizeof(long)), SEEK_SET);	\
		fread(&body_offset, sizeof(long), 1, (cache)->header);					\
		fseek((cache)->body, body_offset, SEEK_SET);							\
	} while (false)
		
/////////////////////////////////


////////// CONSTANTS //////////
#define JF_DISK_BUFFER_SIZE 1024
///////////////////////////////


////////// FILE CACHE //////////
typedef struct jf_file_cache {
	FILE *header;
	FILE *body;
	size_t count;
} jf_file_cache;
///////////////////////////////


////////// FUNCTION STUBS //////////
bool jf_disk_refresh(void);

void jf_disk_clear(void);


bool jf_disk_payload_add_item(const jf_menu_item *item);
jf_menu_item *jf_disk_payload_get_item(const size_t n);
jf_item_type jf_disk_payload_get_type(const size_t n);
size_t jf_disk_payload_item_count(void);


bool jf_disk_playlist_add_item(const jf_menu_item *item);
jf_menu_item *jf_disk_playlist_get_item(const size_t n);
size_t jf_disk_playlist_item_count(void);
////////////////////////////////////
#endif
