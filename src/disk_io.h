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
			JF_STATIC_PRINT("FATAL: could not open file s" #suffix ": ");			\
			write(2, strerror(fopen_errno), strlen(strerror(fopen_errno)));			\
			JF_STATIC_PRINT("\n");													\
			free(tmp);																\
			return false;															\
		}																			\
		free(tmp);																	\
	} while (false);


bool jf_disk_refresh(void);
void jf_disk_clear(void);

bool jf_disk_item_store(const jf_menu_item *item);
jf_menu_item *jf_disk_item_load(size_t n);

#endif
