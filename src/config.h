#ifndef _JF_CONFIG
#define _JF_CONFIG

#include "shared.h"


#define JF_CONFIG_KEY_IS(key) (strncmp(line, key, JF_STATIC_STRLEN(key)) == 0)

#define JF_CONFIG_FILL_VALUE(key)					\
	value_len = strlen(value);						\
	if (value[value_len - 1] == '\n') value_len--;	\
	opts->key = strndup(value, value_len);

#define JF_CONFIG_MALFORMED												\
	fprintf(stderr, "FATAL: malformed config file line: %s\n", line);	\
	jf_options_free(opts);															\
	free(line);															\
	fclose(config_file);												\
	return NULL;

#define JF_CONFIG_WRITE_VALUE(key) fprintf(config_file, #key "=%s\n", opts->key);


const char *jf_config_get_path(void);
jf_options *jf_config_read(const char *config_path);
void jf_config_write(const jf_options *opts, const char *config_path);

#endif
