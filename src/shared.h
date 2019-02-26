#ifndef _JF_SHARED
#define _JF_SHARED

#define _POSIX_C_SOURCE 200809L

#define STATIC_STRLEN(str) (sizeof(str) - 1)

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

#endif
