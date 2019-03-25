#ifndef _JF_SHARED
#define _JF_SHARED

#define STATIC_STRLEN(str) (sizeof(str) - 1)

#define TB_DATA_SIZE 65536


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
	char data[TB_DATA_SIZE];
	size_t used;
	size_t promiscuous_context;
	pthread_mutex_t mut;
	pthread_cond_t cv_no_data;
	pthread_cond_t cv_has_data;
} jf_thread_buffer;


// returns a malloc'd string result of the concatenation of its (expected char *) arguments past the first
// the first argument is the number of following arguments
char *jf_concat(size_t n, ...);

#endif
