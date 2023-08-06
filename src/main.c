#include "shared.h"
#include "net.h"
#include "json.h"
#include "config.h"
#include "disk.h"
#include "playback.h"
#include "menu.h"


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <locale.h>
#include <assert.h>
#include <time.h>
#include <mpv/client.h>
#include <yajl/yajl_version.h>


////////// GLOBAL VARIABLES //////////
jf_options g_options;
jf_global_state g_state;
mpv_handle *g_mpv_ctx = NULL;
//////////////////////////////////////


////////// STATIC FUNCTIONS //////////
static void jf_print_usage(void);
static inline void jf_missing_arg(const char *arg);
static inline void jf_mpv_event_dispatch(const mpv_event *event);
//////////////////////////////////////


////////// PROGRAM TERMINATION //////////
// Note: the signature and description of this function are in shared.h
void jf_exit(int sig)
{
    // some of this is not async-signal-safe
    // but what's the worst that can happen, a crash? :^)
    g_state.state = sig == JF_EXIT_SUCCESS ? JF_STATE_USER_QUIT : JF_STATE_FAIL;
    if (sig == SIGABRT) {
        perror("FATAL");
    }
    jf_net_clear();
    mpv_terminate_destroy(g_mpv_ctx);
    _exit(sig == JF_EXIT_SUCCESS ? EXIT_SUCCESS : EXIT_FAILURE);
}
/////////////////////////////////////////


////////// STARTUP STUFF //////////
static void jf_print_usage(void) {
    printf("Usage:\n");
    printf("\t--help\n");
    printf("\t--version\n");
    printf("\t--config-dir <directory> (default: $XDG_CONFIG_HOME/jftui)\n");
    printf("\t--login.\n");
    printf("\t--no-check-updates\n");
}


static inline void jf_missing_arg(const char *arg)
{
    fprintf(stderr, "FATAL: missing parameter for argument %s\n", arg);
    jf_print_usage();
}
///////////////////////////////////


////////// MISCELLANEOUS GARBAGE //////////
static inline void jf_mpv_event_dispatch(const mpv_event *event)
{
    int64_t playback_ticks;
    mpv_node *node;
    int mpv_flag_yes = 1, mpv_flag_no = 0;

    JF_DEBUG_PRINTF("event: %s\n", mpv_event_name(event->event_id));
    switch (event->event_id) {
        case MPV_EVENT_CLIENT_MESSAGE:
            // playlist controls
            if (((mpv_event_client_message *)event->data)->num_args > 0) {
                if (strcmp(((mpv_event_client_message *)event->data)->args[0],
                            "jftui-playlist-next") == 0) {
                    jf_playback_next();
                } else if (strcmp(((mpv_event_client_message *)event->data)->args[0],
                            "jftui-playlist-prev") == 0) {
                    jf_playback_previous();
                } else if (strcmp(((mpv_event_client_message *)event->data)->args[0],
                            "jftui-playlist-print") == 0) {
                    jf_playback_print_playlist(0);
                } else if (strcmp(((mpv_event_client_message *)event->data)->args[0],
                            "jftui-playlist-shuffle") == 0) {
                    jf_playback_shuffle_playlist();
                }
            }
            break;
        case MPV_EVENT_START_FILE:
            jf_playback_load_external_subtitles();
            // if we're issuing playlist_next/prev very quickly, mpv will not
            // go into idle mode at all
            // in those cases, we digest the INIT state here
            if (g_state.state == JF_STATE_PLAYBACK_INIT) {
                g_state.state = JF_STATE_PLAYBACK;
            }
            break;
        case MPV_EVENT_END_FILE:
            // tell server file playback stopped so it won't keep accruing progress
            playback_ticks =
                mpv_get_property(g_mpv_ctx, "time-pos", MPV_FORMAT_INT64, &playback_ticks) == 0 ?
                JF_SECS_TO_TICKS(playback_ticks) : g_state.now_playing->playback_ticks;
            jf_playback_update_stopped(playback_ticks);
            // move to next item in playlist, if any
            if (((mpv_event_end_file *)event->data)->reason == MPV_END_FILE_REASON_EOF
                    && jf_playback_next()) {
                g_state.state = JF_STATE_PLAYBACK_INIT;
            }
            break;
        case MPV_EVENT_SEEK:
            // syncing to user progress marker
            if (g_state.state == JF_STATE_PLAYBACK_START_MARK) {
                JF_MPV_ASSERT(mpv_set_property_string(g_mpv_ctx, "start", "none"));
                // ensure parent playback ticks refer to merged item
                playback_ticks =
                    mpv_get_property(g_mpv_ctx, "time-pos", MPV_FORMAT_INT64, &playback_ticks) == 0 ?
                    JF_SECS_TO_TICKS(playback_ticks) : 0;
                g_state.now_playing->playback_ticks = playback_ticks;
                g_state.state = JF_STATE_PLAYBACK;
                break;
            }
            // no need to update progress as a time-pos event gets fired
            // immediately after
            break;
        case MPV_EVENT_PROPERTY_CHANGE:
            if (((mpv_event_property *)event->data)->format == MPV_FORMAT_NONE) break;
            if (strcmp("time-pos", ((mpv_event_property *)event->data)->name) == 0) {
                // event valid, check if need to update the server
                playback_ticks = JF_SECS_TO_TICKS(*(int64_t *)((mpv_event_property *)event->data)->data);
                if (llabs(playback_ticks - g_state.now_playing->playback_ticks) < JF_SECS_TO_TICKS(10)) break;
                // good for update; note this will also start a playback session if none are there
                jf_playback_update_progress(playback_ticks);
            } else if (strcmp("sid", ((mpv_event_property *)event->data)->name) == 0) {
                // subtitle track change, go and see if we need to align for split-part
                jf_playback_align_subtitle(*(int64_t *)((mpv_event_property *)event->data)->data);
            } else if (strcmp("options/loop-playlist", ((mpv_event_property *)event->data)->name) == 0) {
                if (g_state.loop_state == JF_LOOP_STATE_RESYNCING) {
                    g_state.loop_state = JF_LOOP_STATE_IN_SYNC;
                    break;
                }
                if (g_state.loop_state == JF_LOOP_STATE_OUT_OF_SYNC) {
                    // we're digesting a decrement caused by an EOF
                    // mid-jftui playlist
                    JF_MPV_ASSERT(mpv_set_property(g_mpv_ctx,
                            "options/loop-playlist",
                            MPV_FORMAT_INT64,
                            &g_state.playlist_loops));
                    g_state.loop_state = JF_LOOP_STATE_RESYNCING;
                    break;
                }
                // the loop counter is in sync, this means the property change
                // is user-triggered and we should abide by it
                node = (((mpv_event_property *)event->data)->data);
                switch (node->format) {
                    case MPV_FORMAT_FLAG:
                        // "no"
                        g_state.playlist_loops = 0;
                        break;
                    case MPV_FORMAT_INT64:
                        // a (guaranteed positive) numeral
                        g_state.playlist_loops = node->u.int64;
                        break;
                    case MPV_FORMAT_STRING:
                        // "yes", "inf" or "force", which we treat the same
                        g_state.playlist_loops = -1;
                        break;
                    default:
                        ;
                }
                g_state.loop_state = JF_LOOP_STATE_IN_SYNC;
            }
            break;
        case MPV_EVENT_IDLE:
            switch (g_state.state) {
                case JF_STATE_PLAYBACK_START_MARK:
                    // going too quick: do nothing but wait for the SEEK
                    break;
                case JF_STATE_PLAYBACK_INIT:
                    // normal: digest it
                    g_state.state = JF_STATE_PLAYBACK;
                    break;
                case JF_STATE_PLAYBACK:
                    // nothing left to play: leave
                    jf_playback_end();
                    break;
                default:
                    fprintf(stderr,
                            "Warning: received MPV_EVENT_IDLE under global state %d. This is a bug.\n",
                            g_state.state);
            }
            break;
        case MPV_EVENT_SHUTDOWN:
            // tell jellyfin playback stopped
            // NB we can't call mpv_get_property because mpv core has aborted!
            if (g_state.now_playing != NULL) {
                jf_playback_update_stopped(g_state.now_playing->playback_ticks);
            }
            jf_playback_end();
            break;
        default:
            // no-op on everything else
            break;
    }
}
///////////////////////////////////////////


////////// MAIN LOOP //////////
int main(int argc, char *argv[])
{
    // VARIABLES
    int i;
    char *config_path;
    jf_reply *reply, *reply_alt;


    // SIGNAL HANDLERS
    {
        struct sigaction sa;
        sa.sa_handler = jf_exit;
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = 0;
        sa.sa_sigaction = NULL;
        assert(sigaction(SIGABRT, &sa, NULL) == 0);
        assert(sigaction(SIGINT, &sa, NULL) == 0);
        // for the sake of multithreaded libcurl
        sa.sa_handler = SIG_IGN;
        assert(sigaction(SIGPIPE, &sa, NULL) == 0);
    }
    //////////////////


    // LIBMPV VERSION CHECK
    // required for "osc" option
    {
        unsigned long mpv_version = mpv_client_api_version();
        if (mpv_version < MPV_MAKE_VERSION(1,24)) {
            fprintf(stderr,
                    "FATAL: found libmpv version %lu.%lu, but 1.24 or greater is required.\n",
                    mpv_version >> 16, mpv_version & 0xFFFF);
            jf_exit(JF_EXIT_FAILURE);
        }
        // future proofing
        if (mpv_version >= MPV_MAKE_VERSION(3,0)) {
            fprintf(stderr,
                    "Warning: found libmpv version %lu.%lu, but jftui expects 1.xx or 2.xx. mpv will probably not work.\n",
                    mpv_version >> 16, mpv_version & 0xFFFF);
        }
    }
    ///////////////////////


    // SETUP OPTIONS
    jf_options_init();
    ////////////////


    // SETUP GLOBAL STATE
    srandom((unsigned)time(NULL));
    g_state = (jf_global_state){ 0 };
    assert((g_state.session_id = jf_generate_random_id(0)) != NULL);
    /////////////////////


    // COMMAND LINE ARGUMENTS
    i = 0;
    while (++i < argc) {
        if (strcmp(argv[i], "--help") == 0) {
            jf_print_usage();
            jf_exit(JF_EXIT_SUCCESS);
        } else if (strcmp(argv[i], "--config-dir") == 0) {
            if (++i >= argc) {
                jf_missing_arg("--config-dir");
                jf_exit(JF_EXIT_FAILURE);
            }
            assert((g_state.config_dir = strdup(argv[i])) != NULL);
        } else if (strcmp(argv[i], "--login") == 0) {
            g_state.state = JF_STATE_STARTING_LOGIN;
        } else if (strcmp(argv[i], "--no-check-updates") == 0) {
            g_options.check_updates = false;
        } else if (strcmp(argv[i], "--version") == 0) {
            printf("jftui %s, libmpv %lu.%lu, libcurl %s %s, yajl %d\n",
                    g_options.version,
                    mpv_client_api_version() >> 16,
                    mpv_client_api_version() & 0xFFFF,
                    curl_version_info(CURLVERSION_NOW)->version,
                    curl_version_info(CURLVERSION_NOW)->ssl_version,
                    yajl_version());
            jf_exit(JF_EXIT_SUCCESS);
        } else {
            fprintf(stderr, "FATAL: unrecognized argument %s.\n", argv[i]);
            jf_print_usage();
            jf_exit(JF_EXIT_FAILURE);
        }
    }
    /////////////////////////
    

    // SETUP DISK
    jf_disk_init();
    /////////////


    // READ AND PARSE CONFIGURATION FILE
    // apply config directory location default unless there was user override
    if (g_state.config_dir == NULL
            && (g_state.config_dir = jf_config_get_default_dir()) == NULL) {
        fprintf(stderr, "FATAL: could not acquire configuration directory location. $HOME could not be read and --config-dir was not passed.\n");
        jf_exit(JF_EXIT_FAILURE);
    }
    // get expected location of config file
    config_path = jf_concat(2, g_state.config_dir, "/settings");

    // check config file exists
    if (jf_disk_is_file_accessible(config_path)) {
        // it's there: read it
        jf_config_read(config_path);
        if (strcmp(g_options.version, JF_VERSION) < 0) {
            printf("Attention: jftui was updated from the last time it was run. Check the changelog on Github.\n");
            free(g_options.version);
            assert((g_options.version = strdup(JF_VERSION)) != NULL);
        }
        // if fundamental fields are missing (file corrupted for some reason)
        if (g_options.server == NULL
                || g_options.userid == NULL
                || g_options.token == NULL) {
            if (! jf_menu_user_ask_yn("Error: settings file missing fundamental fields. Would you like to go through manual configuration?")) {
                jf_exit(JF_EXIT_SUCCESS);
            }
            free(g_options.server);
            free(g_options.userid);
            free(g_options.token);
            g_state.state = JF_STATE_STARTING_FULL_CONFIG;
        }
    } else if (errno == ENOENT) {
        // it's not there
        if (! jf_menu_user_ask_yn("Settings file not found. Would you like to configure jftui?")) {
            jf_exit(JF_EXIT_SUCCESS);
        }
        g_state.state = JF_STATE_STARTING_FULL_CONFIG;
    } else {
        fprintf(stderr, "FATAL: access for settings file at location %s: %s.\n",
            config_path, strerror(errno));
        jf_exit(JF_EXIT_FAILURE);
    }
    ////////////////////////////////////
    

    // UPDATE CHECK
    // it runs asynchronously while we do other stuff
    if (g_options.check_updates) {
        reply_alt = jf_net_request(NULL, JF_REQUEST_CHECK_UPDATE, JF_HTTP_GET, NULL);
    }
    ///////////////


    // INTERACTIVE CONFIG
    if (g_state.state == JF_STATE_STARTING_FULL_CONFIG) {
        jf_config_ask_user();
    } else if (g_state.state == JF_STATE_STARTING_LOGIN) {
        jf_config_ask_user_login();
    }

    // save to disk
    if (g_state.state == JF_STATE_STARTING_FULL_CONFIG
            || g_state.state == JF_STATE_STARTING_LOGIN) {
        if (jf_config_write(config_path)) {
            printf("Please restart to apply the new settings.\n");
            jf_exit(JF_EXIT_SUCCESS);
        } else {
            fprintf(stderr, "FATAL: Configuration failed.\n");
            jf_exit(JF_EXIT_FAILURE);
        }
    } else {
        // we don't consider a failure to save config fatal during normal startup
        jf_config_write(config_path);
        free(config_path);
    }
    /////////////////////
    

    // SERVER NAME AND VERSION
    // this doubles up as a check for connectivity and correct login parameters
    reply = jf_net_request("/system/info", JF_REQUEST_IN_MEMORY, JF_HTTP_GET, NULL);
    if (JF_REPLY_PTR_HAS_ERROR(reply)) {
        fprintf(stderr, "FATAL: could not reach server: %s.\n", jf_reply_error_string(reply));
        jf_exit(JF_EXIT_FAILURE);
    }
    jf_json_parse_system_info_response(reply->payload);
    jf_reply_free(reply);
    //////////////
    

    // SETUP MENU
    jf_menu_init();
    /////////////////


    // SETUP MPV
    if (setlocale(LC_NUMERIC, "C") == NULL) {
        fprintf(stderr, "Warning: could not set numeric locale to sane standard. mpv might refuse to work.\n");
    }
    ////////////


    // resolve update check
    if (g_options.check_updates) {
        jf_net_await(reply_alt);
        if (JF_REPLY_PTR_HAS_ERROR(reply_alt)) {
            fprintf(stderr, "Warning: could not fetch latest version info: %s.\n",
                    jf_reply_error_string(reply_alt));
        } else if (strcmp(JF_VERSION, reply_alt->payload) < 0) {
            printf("Attention: jftui v%s is available for update.\n",
                    reply_alt->payload);
        }
        jf_reply_free(reply_alt);
    }
    ///////////////////////


    ////////// MAIN LOOP //////////
    while (true) {
        switch (g_state.state) {
            case JF_STATE_STARTING:
            case JF_STATE_STARTING_FULL_CONFIG:
            case JF_STATE_STARTING_LOGIN:
                g_state.state = JF_STATE_MENU_UI;
                // no reason to break
            case JF_STATE_MENU_UI:
                jf_menu_ui();
                break;
            case JF_STATE_PLAYBACK:
            case JF_STATE_PLAYBACK_INIT:
            case JF_STATE_PLAYBACK_START_MARK:
                jf_mpv_event_dispatch(mpv_wait_event(g_mpv_ctx, -1));
                break;
            case JF_STATE_USER_QUIT:
                jf_exit(JF_EXIT_SUCCESS);
                break;
            case JF_STATE_FAIL:
                jf_exit(JF_EXIT_FAILURE);
                break;
        }
    }
    ///////////////////////////////


    // never reached
    jf_exit(JF_EXIT_SUCCESS);
}
///////////////////////////////
