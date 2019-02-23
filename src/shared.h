#ifndef _JF_SHARED
#define _JF_SHARED

typedef struct jf_options {
	char *server_url;
	size_t server_url_len;
	char *token;
	size_t ssl_verifyhost;
} jf_options;

#endif
