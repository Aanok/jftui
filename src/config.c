#include "config.h"


jf_options *jf_options_new(void)
{
	jf_options *opts;

	if ((opts = malloc(sizeof(jf_options))) == NULL) {
		return NULL;
	}

	// initialize to empty, will NULL pointers
	*opts = (jf_options){ 0 }; 

	// initialize fields where 0 is a valid value
	opts->ssl_verifyhost = JF_CONFIG_SSL_VERIFYHOST_DEFAULT;

	return opts;
}


// Will provide defaults for fields: client, device, deviceid, version
void jf_options_fill_defaults(jf_options *opts)
{
	if (opts != NULL) {
		opts->client = opts->client != NULL ? opts-> client : JF_CONFIG_CLIENT_DEFAULT;
		opts->device = opts->device != NULL ? opts->device : JF_CONFIG_DEVICE_DEFAULT;
		if (opts->deviceid[0] == '\0') {
			if (gethostname(opts->deviceid, JF_CONFIG_DEVICEID_MAX_LEN - 1) == 0) {
				opts->deviceid[JF_CONFIG_DEVICEID_MAX_LEN - 1] = '\0';
			} else {
				strncpy(opts->deviceid, JF_CONFIG_DEVICEID_DEFAULT, JF_STATIC_STRLEN(JF_CONFIG_DEVICEID_DEFAULT));
			}
		}
		opts->version = opts->version != NULL ? opts->version : JF_CONFIG_VERSION_DEFAULT;
	}
}


void jf_options_free(jf_options *opts)
{
	if (opts != NULL) {
		free(opts->server);
		free(opts->token);
		free(opts->userid);
		free(opts->client);
		free(opts->device);
		free(opts->version);
		free(opts->error);
		free(opts);
	}
}


// NB return value will need to be free'd
// returns NULL if $HOME not set
char *jf_config_get_path(void)
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


// TODO: allow whitespace
// NB this function is meant to work on an existing config file.
// First time config should be handled separately.
jf_options *jf_config_read(const char *config_path)
{
	FILE *config_file;
	char *line;
	size_t line_size = 1024;
	char *value;
	size_t value_len;
	jf_options *opts;

	if (config_path == NULL) {
		return NULL;
	}
	
	if ((opts = jf_options_new()) == NULL) {
		return NULL;
	}

	if ((line = malloc(line_size)) == NULL) {
		free(opts);
		return NULL;
	}

	errno = 0;
	if ((config_file = fopen(config_path, "r")) == NULL) {
		opts->error = jf_concat(4, "FATAL: fopen for config file at location ", config_path, ": ", strerror(errno));
		free(line);
		return opts;
	}

	// read from file
	while (getline(&line, &line_size, config_file) != -1) {
		// allow comments
		if (line[0] == '#') continue;
		if ((value = strchr(line, '=')) == NULL) {
			// the line is malformed; issue a warning but go no further
			fprintf(stderr, "WARNING: malformed config file line: %s\n", line);
			continue;
		}
		value += 1; // digest '='
		// figure out which option key it is
		// NB options that start with a prefix of other options must go after those!
		if JF_CONFIG_KEY_IS("server") {
			JF_CONFIG_FILL_VALUE(server);
			opts->server_len = value_len;
		} else if JF_CONFIG_KEY_IS("token") {
			JF_CONFIG_FILL_VALUE(token);
		} else if JF_CONFIG_KEY_IS("userid") {
			JF_CONFIG_FILL_VALUE(userid);
		} else if JF_CONFIG_KEY_IS("ssl_verifyhost") {
			if (strncmp(value, "false", JF_STATIC_STRLEN("false")) == 0) opts->ssl_verifyhost = false;
		} else if JF_CONFIG_KEY_IS("client") {
			JF_CONFIG_FILL_VALUE(client);
		} else if JF_CONFIG_KEY_IS("deviceid") {
			value_len = strlen(value);
			if (value[value_len - 1] == '\n') value_len--;
			if (value_len > JF_CONFIG_DEVICEID_MAX_LEN - 1) value_len = JF_CONFIG_DEVICEID_MAX_LEN - 1;
			strncpy(opts->deviceid, value, value_len);
			opts->deviceid[value_len] = '\0';
		} else if JF_CONFIG_KEY_IS("device") {
			JF_CONFIG_FILL_VALUE(device);
		} else if JF_CONFIG_KEY_IS("version") {
			JF_CONFIG_FILL_VALUE(version);
		} else {
			// option key was not recognized; print a warning but do no more
			fprintf(stderr, "WARNING: unrecognized option key in config file line: %s\n", line);
		}
	}

	// apply defaults for missing values
	jf_options_fill_defaults(opts);

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
		JF_CONFIG_WRITE_VALUE(userid);
		fprintf(config_file, "ssl_verifyhost=%s\n", opts->ssl_verifyhost ? "true" : "false" );
		JF_CONFIG_WRITE_VALUE(client);
		JF_CONFIG_WRITE_VALUE(device);
		JF_CONFIG_WRITE_VALUE(deviceid);
		JF_CONFIG_WRITE_VALUE(version);

		fclose(config_file);
	}
}


// TODO: this is a stub
jf_options *jf_user_config(jf_options *opts)
{
	printf("FUNCTION STUB: jf_user_config\n");
	exit(EXIT_SUCCESS);
}
