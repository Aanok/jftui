#ifndef _JF_DISK_IO
#define _JF_DISK_IO

#include <stdio.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>

#include "shared.h"

#define JF_DISK_OPEN_FILE_FATAL(suffix)												\
	do {																			\
		tmp = jf_concat(4, g_state.runtime_dir, "/", g_state.session_id, #suffix);	\
		errno = 0;																	\
		if ((s ## suffix = fopen(tmp, "w+")) == NULL) {								\
			int fopen_errno = errno;												\
			JF_STATIC_PRINT_ERROR("FATAL: could not open file ");				 	\
			write(2, tmp, strlen(tmp));												\
			JF_STATIC_PRINT_ERROR(": ");											\
			write(2, strerror(fopen_errno), strlen(strerror(fopen_errno)));			\
			JF_STATIC_PRINT_ERROR(".\n");											\
			free(tmp);																\
			return false;															\
		}																			\
		free(tmp);																	\
	} while (false);

#define JF_DISK_CLOSE_DELETE_FILE(suffix)											\
	do {																			\
		fclose(s ## suffix);														\
		tmp = jf_concat(4, g_state.runtime_dir, "/", g_state.session_id, #suffix);	\
		errno = 0;																	\
		if (unlink(tmp) != 0) {														\
			int unlink_errno = errno;												\
			JF_STATIC_PRINT_ERROR("WARNING: could not delete file ");				\
			write(2, tmp, strlen(tmp));												\
			JF_STATIC_PRINT_ERROR(": ");											\
			write(2, strerror(unlink_errno), strlen(strerror(unlink_errno)));		\
			JF_STATIC_PRINT_ERROR(".\n");											\
		}																			\
		free(tmp);																	\
	} while (false)

bool jf_disk_refresh(void);
void jf_disk_clear(void);

bool jf_disk_item_store(const jf_menu_item *item);
jf_menu_item *jf_disk_item_load(size_t n);

#endif
