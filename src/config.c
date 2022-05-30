#include "config.h"
#include "shared.h"
#include "net.h"
#include "menu.h"
#include "json.h"
#include "disk.h"

#include <errno.h>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>

////////// GLOBAL VARIABLES //////////
extern jf_options g_options;
extern jf_global_state g_state;
//////////////////////////////////////


////////// STATIC FUNCTIONS //////////
// Will fill in fields client, device, deviceid and version of the global
// options struct, unless they're already filled in.
static void jf_options_complete_with_defaults(void);
//////////////////////////////////////


////////// JF_OPTIONS //////////
static void jf_options_complete_with_defaults()
{
    if (g_options.client == NULL) {
        assert((g_options.client = strdup(JF_CONFIG_CLIENT_DEFAULT)) != NULL);
    }
    if (g_options.device[0] == '\0') {
        if (gethostname(g_options.device, JF_CONFIG_DEVICE_SIZE) != 0) {
            strcpy(g_options.device, JF_CONFIG_DEVICE_DEFAULT);
        }
        g_options.device[JF_CONFIG_DEVICE_SIZE - 1] = '\0';
    }
    if (g_options.deviceid[0] == '\0') {
        char *tmp = jf_generate_random_id(JF_CONFIG_DEVICEID_SIZE - 1);
        strcpy(g_options.deviceid, tmp);
        g_options.deviceid[JF_CONFIG_DEVICEID_SIZE - 1] = '\0';
        free(tmp);
    }
    if (g_options.version == NULL) {
        assert((g_options.version = strdup(JF_CONFIG_VERSION_DEFAULT)) != NULL);
    }
}


void jf_options_init(void)
{
    g_options = (jf_options){ 0 };
    // these two must not be overwritten when calling _defaults() again
    // during config file parsing
    g_options.ssl_verifyhost = JF_CONFIG_SSL_VERIFYHOST_DEFAULT;
    g_options.check_updates = JF_CONFIG_CHECK_UPDATES_DEFAULT;
    jf_options_complete_with_defaults();
}


void jf_options_clear()
{
    free(g_options.server);
    free(g_options.token);
    free(g_options.userid);
    free(g_options.client);
    free(g_options.version);
    free(g_options.mpv_profile);
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
void jf_config_read(const char *config_path)
{
    FILE *config_file;
    char *line;
    size_t line_size = 1024;
    char *value;
    size_t value_len;
    jf_strong_bool try_local_files_config;

    assert(config_path != NULL);

    assert((line = malloc(line_size)) != NULL);

    assert((config_file = fopen(config_path, "r")) != NULL);

    // read from file
    while (getline(&line, &line_size, config_file) != -1) {
        assert(line != NULL);
        if ((value = strchr(line, '=')) == NULL) {
            // the line is malformed; issue a warning and skip it
            fprintf(stderr,
                    "Warning: skipping malformed settings file line: %s",
                    line);
            continue;
        }
        value += 1; // digest '='
        // figure out which option key it is
        // NB options that start with a prefix of other options must go after those!
        if (JF_CONFIG_KEY_IS("server")) {
            JF_CONFIG_FILL_VALUE(server);
            g_options.server_len = value_len;
        } else if (JF_CONFIG_KEY_IS("token")) {
            JF_CONFIG_FILL_VALUE(token);
        } else if (JF_CONFIG_KEY_IS("userid")) {
            JF_CONFIG_FILL_VALUE(userid);
        } else if (JF_CONFIG_KEY_IS("ssl_verifyhost")) {
            JF_CONFIG_FILL_VALUE_BOOL(ssl_verifyhost);
        } else if (JF_CONFIG_KEY_IS("client")) {
            JF_CONFIG_FILL_VALUE(client);
        } else if (JF_CONFIG_KEY_IS("deviceid")) {
            JF_CONFIG_FILL_VALUE_ARRAY(deviceid, JF_CONFIG_DEVICEID_SIZE);
        } else if (JF_CONFIG_KEY_IS("device")) {
            JF_CONFIG_FILL_VALUE_ARRAY(device, JF_CONFIG_DEVICE_SIZE);
        } else if (JF_CONFIG_KEY_IS("version")) {
            JF_CONFIG_FILL_VALUE(version);
        } else if (JF_CONFIG_KEY_IS("mpv_profile")) {
            JF_CONFIG_FILL_VALUE(mpv_profile);
        } else if (JF_CONFIG_KEY_IS("check_updates")) {
            JF_CONFIG_FILL_VALUE_BOOL(check_updates);
        } else if (JF_CONFIG_KEY_IS("try_local_files")) {
            if (jf_strong_bool_parse(value, 0, &try_local_files_config) == false) {
                fprintf(stderr,
                        "Warning: unrecognized value for config option \"try_local_files\": %s",
                        value);
            }
        } else {
            // option key was not recognized; print a warning and go on
            fprintf(stderr,
                    "Warning: unrecognized option key in settings file line: %s",
                    line);
        }
    }

    // apply defaults for missing values
    jf_options_complete_with_defaults();

    free(line);
    fclose(config_file);

    // figure out if we should try local files
    switch (try_local_files_config) {
        case JF_STRONG_BOOL_NO:
            g_options.try_local_files = false;
            break;
        case JF_STRONG_BOOL_YES:
            g_options.try_local_files = jf_net_url_is_localhost(g_options.server);
            break;
        case JF_STRONG_BOOL_FORCE:
            g_options.try_local_files = true;
            break;
    }
}


bool jf_config_write(const char *config_path)
{
    FILE *tmp_file;
    char *tmp_path;

    if (jf_disk_is_file_accessible(g_state.config_dir) != 0) {
        assert(mkdir(g_state.config_dir, S_IRWXU) != -1);
    }

    tmp_path = jf_concat(2, g_state.config_dir, "/settings.tmp");

    if ((tmp_file = fopen(tmp_path, "w")) == NULL) {
        fprintf(stderr,
                "Warning: could not open temporary settings file (%s): %s.\nSettings could not be saved.\n",
                tmp_path,
                strerror(errno));
        goto bad_exit;
    }
    JF_CONFIG_WRITE_VALUE(server);
    JF_CONFIG_WRITE_VALUE(token);
    JF_CONFIG_WRITE_VALUE(userid);
    fprintf(tmp_file, "ssl_verifyhost=%s\n",
            g_options.ssl_verifyhost ? "true" : "false" );
    JF_CONFIG_WRITE_VALUE(client);
    JF_CONFIG_WRITE_VALUE(device);
    JF_CONFIG_WRITE_VALUE(deviceid);
    JF_CONFIG_WRITE_VALUE(version);
    if (g_options.mpv_profile != NULL) {
        JF_CONFIG_WRITE_VALUE(mpv_profile);
    }
    // NB don't write check_updates, we want it set manually

    if (fclose(tmp_file) != 0) {
        fprintf(stderr,
                "Warning: could not close temporary settings file (%s): %s.\nSettings could not be saved.\n",
                tmp_path,
                strerror(errno));
        goto bad_exit;
    }
    if (rename(tmp_path, config_path) != 0) {
        fprintf(stderr,
                "Warning: could not move temporary settings file to final location (%s): %s.\nSettings could not be saved.\n",
                config_path,
                strerror(errno));
        goto bad_exit;
    }

    free(tmp_path);
    return true;

bad_exit:
    free(tmp_path);
    return false;
}
////////////////////////////////////////


////////// INTERACTIVE USER CONFIG //////////
void jf_config_ask_user_login()
{
    struct termios old, new;
    char *username, *login_post;
    jf_growing_buffer *password;
    jf_reply *login_reply;
    int c;

    password = jf_growing_buffer_new(128);

    while (true) {
        printf("Please enter your username.\n");
        errno = 0;
        username = jf_menu_linenoise("> ");
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
        jf_growing_buffer_empty(password);
        login_reply = jf_net_request("/emby/Users/authenticatebyname",
                JF_REQUEST_IN_MEMORY,
                JF_HTTP_POST,
                login_post);
        free(login_post);
        if (! JF_REPLY_PTR_HAS_ERROR(login_reply)) break;
        if (login_reply->state == JF_REPLY_ERROR_HTTP_401) {
            jf_reply_free(login_reply);
            if (jf_menu_user_ask_yn("Error: invalid login credentials. "
                        " Would you like to try again?") == false) {
                jf_exit(EXIT_SUCCESS);
            }
        } else {
            fprintf(stderr,
                    "FATAL: could not login: %s.\n",
                    jf_reply_error_string(login_reply));
            jf_reply_free(login_reply);
            jf_exit(JF_EXIT_FAILURE);
        }
    }
    printf("Login successful.\n");
    jf_json_parse_login_response(login_reply->payload);
    jf_reply_free(login_reply);
    jf_growing_buffer_free(password);
}


void jf_config_ask_user()
{
    // login user input
    printf("Please enter the encoded URL of your Jellyfin server. Example: http://foo%%20bar.baz:8096/jf\n");
    printf("(note: unless specified, ports will be the protocol's defaults, i.e. 80 for HTTP and 443 for HTTPS)\n");
    while (true) {
        g_options.server = jf_menu_linenoise("> ");
        if (jf_net_url_is_valid(g_options.server)) {
            g_options.server_len = g_options.server == NULL ? 0 : strlen(g_options.server);
            break;
        } else {
            fprintf(stderr, "Error: malformed URL. Please try again.\n");
            free(g_options.server);
        }
    }

    // critical network stuff: must be configured before network init
    if (jf_menu_user_ask_yn("Do you need jftui to ignore hostname validation (required e.g. if you're using Jellyfin's built-in SSL certificate)?")) {
        g_options.ssl_verifyhost = false;
    }

    jf_config_ask_user_login();
}
/////////////////////////////////////////////
