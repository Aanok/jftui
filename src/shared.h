#ifndef _JF_SHARED
#define _JF_SHARED

#include <pthread.h>
#include <curl/curl.h>
#include <stdbool.h>

// for hardcoded strings
#define JF_STATIC_STRLEN(str) (sizeof(str) - 1)

#define JF_VERSION "prealpha"

#define JF_THREAD_BUFFER_DATA_SIZE (CURL_MAX_WRITE_SIZE +1)

#define JF_ID_LENGTH 32

#define JF_CONFIG_DEVICEID_MAX_LEN 32


// GENERIC JELLYFIN ITEM REPRESENTATION (make sure < 256)
#define JF_ITEM_TYPE_NONE		0
#define JF_ITEM_TYPE_COLLECTION	1
#define JF_ITEM_TYPE_FOLDER		2
#define JF_ITEM_TYPE_PLAYLIST	3
#define JF_ITEM_TYPE_AUDIO		4
#define JF_ITEM_TYPE_ARTIST		5
#define JF_ITEM_TYPE_ALBUM		6
#define JF_ITEM_TYPE_EPISODE	7
#define JF_ITEM_TYPE_SEASON		8
#define JF_ITEM_TYPE_SERIES		9
#define JF_ITEM_TYPE_MOVIE		10
#define JF_ITEM_TYPE_AUDIOBOOK	11


////////// OPTIONS DEFAULTS //////////
#define JF_CONFIG_SSL_VERIFYHOST_DEFAULT	true
#define JF_CONFIG_CLIENT_DEFAULT			"jftui"
#define JF_CONFIG_DEVICE_DEFAULT			"PC"
#define JF_CONFIG_DEVICEID_DEFAULT			"Linux"
#define JF_CONFIG_VERSION_DEFAULT			JF_VERSION
//////////////////////////////////////


// TODO: consider refactoring into global state for application
typedef struct jf_options {
	char *server;
	size_t server_len;
	char *token;
	char *userid;
	bool ssl_verifyhost;
	char *client;
	char *device;
	char deviceid[JF_CONFIG_DEVICEID_MAX_LEN];
	char *version;
} jf_options;


typedef struct jf_menu_item {
	unsigned char type;
	unsigned char *id;
	struct jf_menu_item *children; // array, shall be NULL-terminated
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
jf_options *jf_options_new(void);
void jf_options_fill_defaults(jf_options *opts);
void jf_options_free(jf_options *opts);

// UNUSED FOR NOW
// jf_synced_queue *jf_synced_queue_new(const size_t slot_count);
// void jf_synced_queue_free(jf_synced_queue *q); // NB will NOT deallocate the contents of the queue! make sure it's empty beforehand to avoid leaks
// void jf_synced_queue_enqueue(jf_synced_queue *q, const void *payload);
// void *jf_synced_queue_dequeue(jf_synced_queue *q);
// size_t jf_synced_queue_is_empty(const jf_synced_queue *q);

#endif
