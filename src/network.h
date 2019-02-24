#ifndef _JF_NETWORK
#define _JF_NETWORK

#include "shared.h"

// error codes
#define JF_REPLY_ERROR_STUB -1                                                                                                                                            
#define JF_REPLY_ERROR_UNINITIALIZED -2
#define JF_REPLY_ERROR_NETWORK -3
#define JF_REPLY_ERROR_HTTP_401 -4
#define JF_REPLY_ERROR_HTTP_NOT_OK -5
#define JF_REPLY_ERROR_MALLOC -6


// size < 0 means an error occurred
typedef struct jf_reply {
	char *payload;
	int size;
} jf_reply;


// function prototypes
jf_reply *jf_reply_new(void);
void jf_reply_free(jf_reply *r);
char *jf_reply_error_string(jf_reply *r);
size_t jf_reply_callback(char *payload, size_t size, size_t nmemb, void *userdata);
size_t jf_network_init(jf_options *options);
jf_reply *jf_request(char *resource, size_t to_file);
void jf_network_cleanup(void);

#endif
