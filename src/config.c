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
static void jf_options_complete_with_defaults()
{
	g_options.client = g_options.client != NULL ? g_options.client : strdup(JF_CONFIG_CLIENT_DEFAULT);
	g_options.device = g_options.device != NULL ? g_options.device : strdup(JF_CONFIG_DEVICE_DEFAULT);
	if (g_options.deviceid[0] == '\0') {
		if (gethostname(g_options.deviceid, JF_CONFIG_DEVICEID_MAX_LEN - 1) == 0) {
			g_options.deviceid[JF_CONFIG_DEVICEID_MAX_LEN - 1] = '\0';
		} else {
			strcpy(g_options.deviceid, JF_CONFIG_DEVICEID_DEFAULT);
		}
	}
	g_options.version = g_options.version != NULL ? g_options.version : strdup(JF_CONFIG_VERSION_DEFAULT);
}


void jf_options_clear()
{
	free(g_options.server);
	free(g_options.token);
	free(g_options.userid);
	free(g_options.client);
	free(g_options.device);
	free(g_options.version);
}
////////////////////////////////


////////// CONFIGURATION FILE //////////
// NB return value will need to be free'd
// returns NULL if $HOME not set
char *jf_config_get_default_dir(void)
{
	char *dir;
	if ((dir = getenv("XDG_CONFIG_HOME")) == NULL) {
		if ((dir = getenv("HOME")) != NULL) {
			dir = jf_concat(2, getenv("HOME"), "/.config/jftui");
		}
	} else {
		dir = jf_concat(2, dir, "/jftui");
	}
	return dir;
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
		fprintf(stderr, "FATAL: tried to open settings file with NULL path.\n");
		return false;
	}

	if ((line = malloc(line_size)) == NULL) {
		fprintf(stderr, "FATAL: couldn't allocate getline buffer.\n");
		return false;
	}

	errno = 0;
	if ((config_file = fopen(config_path, "r")) == NULL) {
		int fopen_errno = errno;
		fprintf(stderr, "FATAL: fopen for settings file at location %s: %s.\n",
				config_path, strerror(fopen_errno));
		return false;
	}

	// read from file
	while (getline(&line, &line_size, config_file) != -1) {
		if (line == NULL) {
			fprintf(stderr, "FATAL: couldn't resize getline buffer.\n");
			return false;
		}
		// allow comments
		if (line[0] == '#') continue;
		if ((value = strchr(line, '=')) == NULL) {
			// the line is malformed; issue a warning and skip it
			fprintf(stderr, "Warning: skipping malformed settings file line: %s.\n", line);
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
			// option key was not recognized; print a warning and go on
			fprintf(stderr, "WARNING: unrecognized option key in settings file line: %s.\n", line);
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


bool jf_config_ask_user_login()
{
	struct termios old, new;
	char *username, *login_post;
	jf_growing_buffer *password;
	jf_reply *login_reply;
	int c;

	if ((password = jf_growing_buffer_new(128)) == NULL) {
		fprintf(stderr, "FATAL: password jf_growing_buffer_new returned NULL.\n");
		return false;
	}

	printf("Please enter your username.\n");
	username = linenoise("> ");
	printf("Please enter your password.\n> ");
	tcgetattr(STDIN_FILENO, &old);
	new = old;
	new.c_lflag &= (unsigned int)~ECHO;
	tcsetattr(STDIN_FILENO, TCSANOW, &new);
	while ((c = getchar()) != '\n' && c != EOF) {
		jf_growing_buffer_append(password, &c, 1);
	}
	jf_growing_buffer_append(password, "", 1);
	tcsetattr(STDIN_FILENO, TCSANOW, &old);
	putchar('\n');
	
	login_post = jf_json_generate_login_request(username, password->buf);
	free(username);
	memset(password->buf, 0, password->used);
	jf_growing_buffer_free(password);
	login_reply = jf_net_login_request(login_post);
	free(login_post);
	if (login_reply == NULL) {
		fprintf(stderr, "FATAL: jf_net_login_request returned NULL.\n");
		return false;
	}
	if (JF_REPLY_PTR_HAS_ERROR(login_reply)) {
		fprintf(stderr, "FATAL: jf_net_login_request: %s.\n", jf_reply_error_string(login_reply));
		jf_reply_free(login_reply);
		return false;
	}
	if (! jf_json_parse_login_response(login_reply->payload)) {
		fprintf(stderr, "FATAL: could not parse login response.\n");
		jf_reply_free(login_reply);
		return false;
	}
	jf_reply_free(login_reply);

	return true;
}


// TODO: this is a stub
bool jf_config_ask_user()
{
	// setup
	jf_options_complete_with_defaults();

	// login user input
	printf("Please enter the URL of your Jellyfin server. Example: http://foo.bar:8096/jf\n(note: unless specified, ports will be the protocol's defaults, i.e. 80 for HTTP and 443 for HTTPS)\n");
	while (true) {
		g_options.server = linenoise("> ");
		if (jf_net_url_is_valid(g_options.server)) {
			g_options.server_len = strlen(g_options.server);
			break;
		} else {
			fprintf(stderr, "Error: malformed URL. Please try again.\n");
		}
	}

	if (! jf_config_ask_user_login()) {
		return false;
	}

	// misc config user input
	if (jf_menu_user_ask_yn("Do you need jftui to ignore hostname validation (required e.g. if you're using Jellyfin's built-in SSL certificate)?")) {
		g_options.ssl_verifyhost = false;
	}

	printf("Configuration and login successful.\n");
	return true;
}
