#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "config.h"
#include "shared.h"


// NB return value will need to be free'd
// returns NULL if $HOME not set
const char *jf_config_get_path(void)
{
	char *str;
	if ((str = getenv("XDG_CONFIG_HOME")) == NULL) {
		if ((str = getenv("HOME")) != NULL) {
			str = jf_concat(2, getenv("HOME"), "/.config/jftui");
		}
	} else {
		str = jf_concat(2, str, "/jftui");
	}
	return str;
}


// TODO: better error handling
// TODO: allow whitespace
jf_options *jf_config_read(const char *config_path)
{
	FILE *config_file;
	char *line;
	size_t line_size = 1024;
	char *value;
	size_t value_len;
	jf_options *opts;

	// allocate options and assign defaults (static strings, safe to overwrite)
	if ((opts = malloc(sizeof(jf_options))) == NULL) {
		return NULL;
	}
	*opts = (jf_options){ 0 }; // correctly initialize to empty, will NULL pointers
	opts->ssl_verifyhost = JF_CONFIG_SSL_VERIFYHOST_DEFAULT;
	opts->client = JF_CONFIG_CLIENT_DEFAULT;
	opts->device = JF_CONFIG_DEVICE_DEFAULT;
	opts->deviceid = JF_CONFIG_DEVICEID_DEFAULT;
	opts->version = JF_CONFIG_VERSION_DEFAULT;


	if ((line = malloc(line_size)) == NULL) {
		free(opts);
		return NULL;
	}

	if ((config_file = fopen(config_path, "r")) == NULL) {
		free(opts);
		free(line);
		return NULL;
	}

	// read from file
	while (getline(&line, &line_size, config_file) != -1) {
		// allow comments
		if (line[0] == '#') continue;
		if ((value = strchr(line, '=')) == NULL) {
			// malformed line, consider fatal and return NULL
			JF_CONFIG_MALFORMED
		}
		value += 1; // digest '='
		// figure out which option key it is
		// NB options that start with a prefix of other options must go after those!
		if JF_CONFIG_KEY_IS("server") {
			JF_CONFIG_FILL_VALUE(server);
			opts->server_len = value_len;
		} else if JF_CONFIG_KEY_IS("token") {
			JF_CONFIG_FILL_VALUE(token);
		} else if JF_CONFIG_KEY_IS("user") {
			JF_CONFIG_FILL_VALUE(user);
		} else if JF_CONFIG_KEY_IS("ssl_verifyhost") {
			if (strncmp(value, "false", JF_STATIC_STRLEN("false")) == 0) opts->ssl_verifyhost = 0;
		} else if JF_CONFIG_KEY_IS("client") {
			JF_CONFIG_FILL_VALUE(client);
		} else if JF_CONFIG_KEY_IS("deviceid") {
			JF_CONFIG_FILL_VALUE(deviceid);
		} else if JF_CONFIG_KEY_IS("device") {
			JF_CONFIG_FILL_VALUE(device);
		} else if JF_CONFIG_KEY_IS("version") {
			JF_CONFIG_FILL_VALUE(version);
		} else {
			// unrecognized option key, consider fatal and return NULL
			JF_CONFIG_MALFORMED
		}
	}

	free(line);
	fclose(config_file);

	return opts;
}


// TODO: error handling
void jf_config_write(const jf_options *opts, const char *config_path)
{
	FILE *config_file;

	if ((config_file = fopen(config_path, "w")) != NULL) {
		// bit inefficient but w/e
		JF_CONFIG_WRITE_VALUE(server);
		JF_CONFIG_WRITE_VALUE(token);
		JF_CONFIG_WRITE_VALUE(user);
		fprintf(config_file, "ssl_verifyhost=%s\n", opts->ssl_verifyhost ? "true" : "false" );
		JF_CONFIG_WRITE_VALUE(client);
		JF_CONFIG_WRITE_VALUE(device);
		JF_CONFIG_WRITE_VALUE(deviceid);
		JF_CONFIG_WRITE_VALUE(version);

		fclose(config_file);
	}
}
