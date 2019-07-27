#include "config.h"


////////// GLOBALS //////////
extern jf_options g_options;
/////////////////////////////


////////// STATIC FUNCTIONS //////////
// Procedure: jf_options_complete_with_defaults
//
// Will fill in fields client, device, deviceid and version of the global options struct, unless they're already filled in.
static void jf_options_complete_with_defaults(void);
//////////////////////////////////////


////////// JF_OPTIONS //////////
void jf_options_init()
{
	// initialize to empty, will NULL pointers
	g_options = (jf_options){ 0 }; 

	// initialize fields where 0 is a valid value
	g_options.ssl_verifyhost = JF_CONFIG_SSL_VERIFYHOST_DEFAULT;
}


// Will provide defaults for fields: client, device, deviceid, version
static void jf_options_complete_with_defaults()
{
	g_options.client = g_options.client != NULL ? g_options.client : JF_CONFIG_CLIENT_DEFAULT;
	g_options.device = g_options.device != NULL ? g_options.device : JF_CONFIG_DEVICE_DEFAULT;
	if (g_options.deviceid[0] == '\0') {
		if (gethostname(g_options.deviceid, JF_CONFIG_DEVICEID_MAX_LEN - 1) == 0) {
			g_options.deviceid[JF_CONFIG_DEVICEID_MAX_LEN - 1] = '\0';
		} else {
			strncpy(g_options.deviceid, JF_CONFIG_DEVICEID_DEFAULT, JF_STATIC_STRLEN(JF_CONFIG_DEVICEID_DEFAULT));
		}
	}
	g_options.version = g_options.version != NULL ? g_options.version : JF_CONFIG_VERSION_DEFAULT;
}


void jf_options_clear()
{
	free(g_options.server);
	free(g_options.token);
	free(g_options.userid);
	free(g_options.client);
	free(g_options.device);
	free(g_options.version);
	free(g_options.error);
}
////////////////////////////////


////////// CONFIGURATION FILE //////////
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
bool jf_config_read(const char *config_path)
{
	FILE *config_file;
	char *line;
	size_t line_size = 1024;
	char *value;
	size_t value_len;

	if (config_path == NULL) {
		return false;
	}

	if ((line = malloc(line_size)) == NULL) {
		return false;
	}

	errno = 0;
	if ((config_file = fopen(config_path, "r")) == NULL) {
		g_options.error = jf_concat(4, "FATAL: fopen for config file at location ", config_path, ": ", strerror(errno));
		free(line);
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
			g_options.server_len = value_len;
		} else if JF_CONFIG_KEY_IS("token") {
			JF_CONFIG_FILL_VALUE(token);
		} else if JF_CONFIG_KEY_IS("userid") {
			JF_CONFIG_FILL_VALUE(userid);
		} else if JF_CONFIG_KEY_IS("ssl_verifyhost") {
			if (strncmp(value, "false", JF_STATIC_STRLEN("false")) == 0) g_options.ssl_verifyhost = false;
		} else if JF_CONFIG_KEY_IS("client") {
			JF_CONFIG_FILL_VALUE(client);
		} else if JF_CONFIG_KEY_IS("deviceid") {
			value_len = strlen(value);
			if (value[value_len - 1] == '\n') value_len--;
			if (value_len > JF_CONFIG_DEVICEID_MAX_LEN - 1) value_len = JF_CONFIG_DEVICEID_MAX_LEN - 1;
			strncpy(g_options.deviceid, value, value_len);
			g_options.deviceid[value_len] = '\0';
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
	jf_options_complete_with_defaults();

	free(line);
	fclose(config_file);

	return true;
}


// TODO: error handling
bool jf_config_write(const char *config_path)
{
	FILE *config_file;

	if ((config_file = fopen(config_path, "w")) != NULL) {
		// bit inefficient but w/e
		JF_CONFIG_WRITE_VALUE(server);
		JF_CONFIG_WRITE_VALUE(token);
		JF_CONFIG_WRITE_VALUE(userid);
		fprintf(config_file, "ssl_verifyhost=%s\n", g_options.ssl_verifyhost ? "true" : "false" );
		JF_CONFIG_WRITE_VALUE(client);
		JF_CONFIG_WRITE_VALUE(device);
		JF_CONFIG_WRITE_VALUE(deviceid);
		JF_CONFIG_WRITE_VALUE(version);

		fclose(config_file);
		return true;
	} else {
		return false;
	}
}
////////////////////////////////////////


// TODO: this is a stub
bool jf_user_config()
{
	printf("FUNCTION STUB: jf_user_config\n");
	exit(EXIT_SUCCESS);
}
