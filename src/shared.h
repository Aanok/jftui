#ifndef _JF_SHARED
#define _JF_SHARED

#define JF_STATIC_STRLEN(str) (sizeof(str) - 1)

#define JF_THREAD_BUFFER_DATA_SIZE 65536


#include <pthread.h>


typedef struct jf_options {
	char *server_url;
	size_t server_url_len;
	char *token;
	char *userid;
	size_t ssl_verifyhost;
	char *client;
	char *device;
	char *deviceid;
	char *version;
} jf_options;


// size < 0 means an error occurred
typedef struct jf_reply {
	char *payload;
	int size;
} jf_reply;


typedef struct jf_thread_buffer {
	char data[JF_THREAD_BUFFER_DATA_SIZE];
	size_t used;
	size_t promiscuous_context;
	pthread_mutex_t mut;
	pthread_cond_t cv_no_data;
	pthread_cond_t cv_has_data;
} jf_thread_buffer;


typedef struct jf_synced_queue {
	const void **slots;
	size_t slot_count;
	size_t current;
	size_t next;
	pthread_mutex_t mut;
	pthread_cond_t cv_is_empty;
	pthread_cond_t cv_is_full;
} jf_synced_queue;


// returns a malloc'd string result of the concatenation of its (expected char *) arguments past the first
// the first argument is the number of following arguments
char *jf_concat(const size_t n, ...);

void jf_thread_buffer_init(jf_thread_buffer *tb);

jf_synced_queue *jf_synced_queue_new(const size_t slot_count);
void jf_synced_queue_free(jf_synced_queue *q); // NB will NOT deallocate the contents of the queue! make sure it's empty beforehand to avoid leaks
void jf_synced_queue_enqueue(jf_synced_queue *q, const void *payload);
void *jf_synced_queue_dequeue(jf_synced_queue *q);
size_t jf_synced_queue_is_empty(const jf_synced_queue *q);

#endif
