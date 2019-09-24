#ifndef _JF_DISK
#define _JF_DISK

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>
#include <assert.h>
#include <signal.h>

#include "shared.h"
#include "menu.h"


////////// CONSTANTS //////////
#define JF_DISK_BUFFER_SIZE 1024
///////////////////////////////


////////// FILE CACHE //////////
typedef struct jf_file_cache {
	FILE *header;
	char *header_path;
	FILE *body;
	char *body_path;
	size_t count;
} jf_file_cache;
///////////////////////////////


////////// FUNCTION STUBS //////////
char *jf_disk_get_default_runtime_dir(void);
void jf_disk_init(void);
void jf_disk_refresh(void);
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
