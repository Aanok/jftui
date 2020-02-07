#ifndef _JF_CONFIG
#define _JF_CONFIG

#include <stddef.h>
#include <stdbool.h>


////////// CODE MACROS //////////
#define JF_CONFIG_KEY_IS(key) strncmp(line, key, JF_STATIC_STRLEN(key)) == 0

#define JF_CONFIG_FILL_VALUE(_key)                                      \
do {                                                                    \
    value_len = strlen(value);                                          \
    if (value[value_len - 1] == '\n') value_len--;                      \
    free(g_options._key);                                               \
    assert((g_options._key = strndup(value, value_len)) != NULL);       \
} while (false)

#define JF_CONFIG_FILL_VALUE_ARRAY(_key, _bufsize)          \
do {                                                        \
    value_len = strlen(value);                              \
    if (value[value_len - 1] == '\n') value_len--;          \
    value_len = jf_clamp_zu(value_len, 0, _bufsize - 1);    \
    strncpy(g_options._key, value, value_len);              \
    g_options._key[value_len] = '\0';                       \
} while (false)

#define JF_CONFIG_FILL_VALUE_BOOL(_key)                             \
do {                                                                \
    if (strncmp(value, "false", JF_STATIC_STRLEN("false")) == 0) {  \
        g_options._key= false;                                      \
    }                                                               \
} while (false)

#define JF_CONFIG_WRITE_VALUE(key) fprintf(tmp_file, #key "=%s\n", g_options.key)
/////////////////////////////////


////////// JF_OPTIONS //////////
// defaults
#define JF_CONFIG_SSL_VERIFYHOST_DEFAULT    true
#define JF_CONFIG_CLIENT_DEFAULT            "jftui"
#define JF_CONFIG_DEVICE_DEFAULT            "Linux-PC"
#define JF_CONFIG_DEVICE_SIZE               32
#define JF_CONFIG_DEVICEID_SIZE             8
#define JF_CONFIG_VERSION_DEFAULT           JF_VERSION
#define JF_CONFIG_CHECK_UPDATES_DEFAULT     true


typedef struct jf_options {
    char *server;
    size_t server_len;
    char *token;
    char *userid;
    bool ssl_verifyhost;
    char *client;
    char device[JF_CONFIG_DEVICE_SIZE];
    char deviceid[JF_CONFIG_DEVICEID_SIZE];
    char *version;
    bool check_updates;
} jf_options;


void jf_options_init(void);
void jf_options_clear(void);
////////////////////////////////


////////// USER CONFIGURATION //////////
char *jf_config_get_default_dir(void);
void jf_config_read(const char *config_path);
bool jf_config_write(const char *config_path);
void jf_config_ask_user_login(void);
void jf_config_ask_user(void);
////////////////////////////////////////

#endif
