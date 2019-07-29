#ifndef _JF_SHARED
#define _JF_SHARED

#include <pthread.h>
#include <curl/curl.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <pthread.h>
#include <unistd.h>

// for hardcoded strings
#define JF_STATIC_STRLEN(str) (sizeof(str) - 1)

#define JF_VERSION "prealpha"

#define JF_THREAD_BUFFER_DATA_SIZE (CURL_MAX_WRITE_SIZE +1)

#define JF_ID_LENGTH 32

#define JF_CONFIG_DEVICEID_MAX_LEN 32


////////// GENERIC JELLYFIN ITEM REPRESENTATION //////////
// Information about persistency is used to make part of the menu interface
// tree not get deallocated when navigating upwards
typedef char jf_item_type;

#define JF_ITEM_TYPE_NONE			0
#define JF_ITEM_TYPE_COLLECTION		1
#define JF_ITEM_TYPE_FOLDER			2
#define JF_ITEM_TYPE_PLAYLIST		3
#define JF_ITEM_TYPE_AUDIO			4
#define JF_ITEM_TYPE_ARTIST			5
#define JF_ITEM_TYPE_ALBUM			6
#define JF_ITEM_TYPE_EPISODE		7
#define JF_ITEM_TYPE_SEASON			8
#define JF_ITEM_TYPE_SERIES			9
#define JF_ITEM_TYPE_MOVIE			10
#define JF_ITEM_TYPE_AUDIOBOOK		11
#define JF_ITEM_TYPE_MENU_ROOT		-1
#define JF_ITEM_TYPE_MENU_FAVORITES	-2
#define JF_ITEM_TYPE_MENU_ON_DECK	-3
#define JF_ITEM_TYPE_MENU_LATEST	-4
#define JF_ITEM_TYPE_MENU_LIBRARIES	-5

#define JF_MENU_ITEM_TYPE_IS_PERSISTENT(item_type) (item_type < 0)
//////////////////////////////////////////////////////////


typedef struct jf_menu_item {
	jf_item_type type;
	char *id;
	struct jf_menu_item *children; // NULL-terminated
} jf_menu_item;


typedef struct jf_thread_buffer {
	char data[JF_THREAD_BUFFER_DATA_SIZE];
	size_t used;
	bool promiscuous_context;
	unsigned char *parsed_ids;
	size_t parsed_ids_size;
	size_t item_count;
	pthread_mutex_t mut;
	pthread_cond_t cv_no_data;
	pthread_cond_t cv_has_data;
} jf_thread_buffer;


// size < 0 means an error occurred
typedef struct jf_reply {
	char *payload;
	int size;
} jf_reply;


// UNUSED FOR NOW
// typedef struct jf_synced_queue {
// 	const void **slots;
// 	size_t slot_count;
// 	size_t current;
// 	size_t next;
// 	pthread_mutex_t mut;
// 	pthread_cond_t cv_is_empty;
// 	pthread_cond_t cv_is_full;
// } jf_synced_queue;


// returns a NULL-terminated, malloc'd string result of the concatenation of its (expected char *) arguments past the first
// the first argument is the number of following arguments
char *jf_concat(const size_t n, ...);

bool jf_thread_buffer_init(jf_thread_buffer *tb);


// UNUSED FOR NOW
// jf_synced_queue *jf_synced_queue_new(const size_t slot_count);
// void jf_synced_queue_free(jf_synced_queue *q); // NB will NOT deallocate the contents of the queue! make sure it's empty beforehand to avoid leaks
// void jf_synced_queue_enqueue(jf_synced_queue *q, const void *payload);
// void *jf_synced_queue_dequeue(jf_synced_queue *q);
// size_t jf_synced_queue_is_empty(const jf_synced_queue *q);

#endif
