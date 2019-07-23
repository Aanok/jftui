#ifndef _JF_CONFIG
#define _JF_CONFIG

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#include "shared.h"


#define JF_CONFIG_KEY_IS(key) (strncmp(line, key, JF_STATIC_STRLEN(key)) == 0)

#define JF_CONFIG_FILL_VALUE(key)					\
do {												\
	value_len = strlen(value);						\
	if (value[value_len - 1] == '\n') value_len--;	\
	opts->key = strndup(value, value_len);			\
} while (false)

#define JF_CONFIG_WRITE_VALUE(key) fprintf(config_file, #key "=%s\n", opts->key)


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
	char *error;
} jf_options;


jf_options *jf_options_new(void);
void jf_options_fill_defaults(jf_options *opts);
void jf_options_free(jf_options *opts);

char *jf_config_get_path(void);
jf_options *jf_config_read(const char *config_path);
void jf_config_write(const jf_options *opts, const char *config_path);
jf_options *jf_user_config(jf_options *opts);


#endif
