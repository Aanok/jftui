///////////////////////////////////
#define _POSIX_C_SOURCE 200809L  //
///////////////////////////////////

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <locale.h>
#include <mpv/client.h>

#include "shared.h"
#include "net.h"
#include "json.h"
#include "config.h"
#include "disk.h"


////////// CODE MACROS //////////
#define JF_MISSING_ARG_FATAL(arg)												\
	do {																		\
		fprintf(stderr, "FATAL: missing parameter for argument " #arg "\n");	\
		jf_print_usage();														\
		exit(EXIT_FAILURE);														\
	} while (false)

#define JF_MPV_FATAL(status)															\
	do {																				\
		if (status < 0) {																\
			fprintf(stderr, "FATAL: mpv API error: %s\n", mpv_error_string(status));	\
			mpv_terminate_destroy(g_mpv_ctx);											\
			exit(EXIT_FAILURE);															\
		}																				\
	} while (false);
/////////////////////////////////


////////// GLOBAL VARIABLES //////////
jf_options g_options;
jf_global_state g_state;
mpv_handle *g_mpv_ctx = NULL;
//////////////////////////////////////


////////// STATIC VARIABLES //////////
static mpv_handle *jf_mpv_context_new(void);
//////////////////////////////////////


////////// STATIC FUNCTIONS //////////
static void jf_print_usage(void);
static mpv_handle *jf_mpv_context_new(void);
//////////////////////////////////////


static void jf_print_usage() {
	printf("Usage:\n");
	printf("\t--help\n");
	printf("\t--config-dir <directory> (default: $XDG_CONFIG_HOME/jftui)\n");
	printf("\t--runtime-dir <directory> (default: $XDG_DATA_HOME/jftui)\n");
	printf("\t--login.\n");
}


static mpv_handle *jf_mpv_context_new()
{
	mpv_handle *ctx;
	int mpv_flag_yes = 1;
	char *x_emby_token;

	if ((ctx = mpv_create()) == NULL) {
		fprintf(stderr, "FATAL: failed to create mpv context.\n");
		return NULL;
	}
	JF_MPV_FATAL(mpv_set_option(ctx, "config-dir", MPV_FORMAT_STRING, &g_state.config_dir));
	JF_MPV_FATAL(mpv_set_option(ctx, "config", MPV_FORMAT_FLAG, &mpv_flag_yes));
	JF_MPV_FATAL(mpv_set_option(ctx, "osc", MPV_FORMAT_FLAG, &mpv_flag_yes));
	JF_MPV_FATAL(mpv_set_option(ctx, "input-default-bindings", MPV_FORMAT_FLAG, &mpv_flag_yes));
	JF_MPV_FATAL(mpv_set_option(ctx, "input-vo-keyboard", MPV_FORMAT_FLAG, &mpv_flag_yes));
	JF_MPV_FATAL(mpv_set_option(ctx, "input-terminal", MPV_FORMAT_FLAG, &mpv_flag_yes));
	JF_MPV_FATAL(mpv_set_option(ctx, "terminal", MPV_FORMAT_FLAG, &mpv_flag_yes));
	if ((x_emby_token = jf_concat(2, "x-emby-token: ", g_options.token)) == NULL) {
		fprintf(stderr, "FATAL: jf_concat for x-emby-token header field for mpv requests returned NULL.\n");
		exit(EXIT_FAILURE);
	}
	JF_MPV_FATAL(mpv_set_option_string(ctx, "http-header-fields", x_emby_token));
	free(x_emby_token);
	JF_MPV_FATAL(mpv_observe_property(ctx, 0, "time-pos", MPV_FORMAT_INT64));

	JF_MPV_FATAL(mpv_initialize(ctx));

	return ctx;
}


int main(int argc, char *argv[])
{
	// VARIABLES
	int i;
	char *config_path, *progress_post;
	mpv_event *event;
	int mpv_flag_yes = 1, mpv_flag_no = 0;
	int64_t playback_ticks;
	jf_reply *reply;


	// LIBMPV VERSION CHECK
	// required for "osc" option
	if (MPV_CLIENT_API_VERSION < MPV_MAKE_VERSION(1,23)) {
		fprintf(stderr, "FATAL: found libmpv version %lu.%lu, but 1.23 or greater is required.\n",
				MPV_CLIENT_API_VERSION >> 16, MPV_CLIENT_API_VERSION & 0xFFFF);
		exit(EXIT_FAILURE);
	}
	// future proofing
	if (MPV_CLIENT_API_VERSION >= MPV_MAKE_VERSION(2,0)) {
		fprintf(stderr, "Warning: found libmpv version %lu.%lu, but jftui expects 1.xx. mpv will probably not work.\n",
				MPV_CLIENT_API_VERSION >> 16, MPV_CLIENT_API_VERSION & 0xFFFF);
	}
	///////////////////////


	// SETUP OPTIONS
	g_options = (jf_options){ 0 }; 
	g_options.ssl_verifyhost = JF_CONFIG_SSL_VERIFYHOST_DEFAULT;
	atexit(jf_options_clear);
	////////////////


	// SETUP GLOBAL STATE
	g_state = (jf_global_state){ 0 };
	if ((g_state.session_id = jf_generate_random_id(0)) == NULL) {
		fprintf(stderr, "FATAL: jf_generate_random_id returned NULL.\n");
		exit(EXIT_FAILURE);
	}
	atexit(jf_global_state_clear);
	/////////////////////


	// COMMAND LINE ARGUMENTS
	i = 0;
	while (++i < argc) {
		if (strcmp(argv[i], "--help") == 0) {
			jf_print_usage();
			exit(EXIT_SUCCESS);
		} else if (strcmp(argv[i], "--config-dir") == 0) {
			if (++i >= argc) {
				JF_MISSING_ARG_FATAL("--config-dir");
			}
			g_state.config_dir = strdup(argv[i]);
		} else if (strcmp(argv[i], "--runtime-dir") == 0) {
			if (++i >= argc) {
				JF_MISSING_ARG_FATAL("--runtime-dir");
			}
			g_state.runtime_dir = strdup(argv[i]);
		} else if (strcmp(argv[i], "--login") == 0) {
			g_state.state = JF_STATE_STARTING_LOGIN;
		} else {
			fprintf(stderr, "FATAL: unrecognized argument %s.\n", argv[i]);
			exit(EXIT_FAILURE);
		}
	}
	/////////////////////////
	

	// SETUP DISK
	// apply runtime directory location default unless there was user override
	if (g_state.runtime_dir == NULL) {
		if ((g_state.runtime_dir = jf_disk_get_default_runtime_dir()) == NULL) {
			fprintf(stderr, "FATAL: could not acquire runtime directory location. $HOME could not be read and --runtime-dir was not passed.\n");
			exit(EXIT_FAILURE);
		}
	}
	jf_disk_init();
	atexit(jf_disk_clear);
	/////////////


	// INITIAL NETWORK SETUP
	if (! jf_net_pre_init()) {
		fprintf(stderr, "FATAL: could not pre-initialize network context.\n");
		exit(EXIT_FAILURE);
	}
	atexit(jf_net_clear);
	////////////////
	

	// READ AND PARSE CONFIGURATION FILE
	// apply config directory location default unless there was user override
	if (g_state.config_dir == NULL) {
		if ((g_state.config_dir = jf_config_get_default_dir()) == NULL) {
			fprintf(stderr, "FATAL: could not acquire configuration directory location. $HOME could not be read and --config-dir was not passed.\n");
			exit(EXIT_FAILURE);
		}
	}
	// get expected location of config file
	if ((config_path = jf_concat(2, g_state.config_dir, "/settings")) == NULL) {
		fprintf(stderr, "FATAL: config path jf_concat returned NULL.\n");
		exit(EXIT_FAILURE);
	}

	// check config file exists
	if (access(config_path, F_OK) == 0) {
		// it's there: read it
		if (! jf_config_read(config_path)) {
			free(config_path);
			exit(EXIT_FAILURE);
		}
		// if fundamental fields are missing (file corrupted for some reason)
		if (JF_OPTIONS_IS_INCOMPLETE()) {
			if (! jf_menu_user_ask_yn("Error: settings file missing fundamental fields. Would you like to go through manual configuration?")) {
				free(config_path);
				exit(EXIT_SUCCESS);
			}
			g_state.state = JF_STATE_STARTING_FULL_CONFIG;
		}
	} else if (errno == ENOENT || errno == ENOTDIR) {
		// it's not there
		if (! jf_menu_user_ask_yn("Settings file not found. Would you like to configure jftui?")) {
			free(config_path);
			exit(EXIT_SUCCESS);
		}
		g_state.state = JF_STATE_STARTING_FULL_CONFIG;
	} else {
		int access_errno = errno;
		fprintf(stderr, "FATAL: access for settings file at location %s: %s.\n",
			config_path, strerror(access_errno));
		free(config_path);
		exit(EXIT_FAILURE);
	}

	// interactive config if required
	if (g_state.state == JF_STATE_STARTING_FULL_CONFIG) {
		if (! jf_config_ask_user()) {
			free(config_path);
			exit(EXIT_FAILURE);
		}
	} else if (g_state.state == JF_STATE_STARTING_LOGIN) {
		if (! jf_config_ask_user_login()) {
			free(config_path);
			exit(EXIT_FAILURE);
		}
	}
	
	// save to disk
	jf_config_write(config_path);
	free(config_path);
	////////////////////////////////////
	

	// FINALIZE NETWORK SETUP
	if (! jf_net_refresh()) {
		exit(EXIT_FAILURE);
	}
	// get server name and double check everything's fine
	reply = jf_net_request("/system/info", JF_REQUEST_IN_MEMORY, NULL);
	if (reply == NULL || JF_REPLY_PTR_HAS_ERROR(reply)) {
		fprintf(stderr, "FATAL: could not reach server: %s.\n", jf_reply_error_string(reply));
		exit(EXIT_FAILURE);
	}
	if (! jf_json_parse_server_info_response(reply->payload)) {
		exit(EXIT_FAILURE);
	}
	jf_reply_free(reply);
	////////////////////////
	

	// SETUP MENU
	jf_menu_init();
	atexit(jf_menu_clear);
	/////////////////


	// SETUP MPV
	if (setlocale(LC_NUMERIC, "C") == NULL) {
		fprintf(stderr, "Warning: could not set numeric locale to sane standard. mpv might refuse to work.\n");
	}

	if ((g_mpv_ctx = jf_mpv_context_new()) == NULL) {
		exit(EXIT_FAILURE);
	}
	atexit(jf_mpv_clear);
	////////////


	////////// MAIN LOOP //////////
	while (true) {
		switch (g_state.state) {
			// HANDLE SHUTDOWN
			case JF_STATE_USER_QUIT:
				exit(EXIT_SUCCESS);
				break;
			case JF_STATE_FAIL:
				exit(EXIT_FAILURE);
				break;
			// RUNTIME: READ AND PROCESS EVENTS
			default:
				event = mpv_wait_event(g_mpv_ctx, -1);
// 				printf("DEBUG: event: %s\n", mpv_event_name(event->event_id));
				switch (event->event_id) {
					case MPV_EVENT_CLIENT_MESSAGE:
						// playlist controls
						if (((mpv_event_client_message *)event->data)->num_args > 0) {
							if (strcmp(((mpv_event_client_message *)event->data)->args[0], "jftui-playlist-next") == 0) {
								jf_menu_playlist_forward();
							} else if (strcmp(((mpv_event_client_message *)event->data)->args[0], "jftui-playlist-prev") == 0) {
								jf_menu_playlist_backward();
							}
						}
						break;
					case MPV_EVENT_END_FILE:
						// tell server file playback stopped so it won't keep accruing progress
						playback_ticks = mpv_get_property(g_mpv_ctx, "time-pos", MPV_FORMAT_INT64, &playback_ticks) == 0 ?
							JF_SECS_TO_TICKS(playback_ticks) : g_state.now_playing.playback_ticks;
						if ((progress_post = jf_json_generate_progress_post(g_state.now_playing.id, playback_ticks)) == NULL) {
							fprintf(stderr, "Warning: session stop jf_json_generate_progress_post returned NULL.\n");
						} else {
							reply = jf_net_request("/sessions/playing/stopped", JF_REQUEST_IN_MEMORY, progress_post);
							free(progress_post);
							if (reply == NULL || JF_REPLY_PTR_HAS_ERROR(reply)) {
								fprintf(stderr, "Warning: session stop jf_net_request: %s.\n", jf_reply_error_string(reply));
							}
							jf_reply_free(reply);
						}
						// move to next item in playlist, if any
						if (((mpv_event_end_file *)event->data)->reason == MPV_END_FILE_REASON_EOF) {
							if (jf_menu_playlist_forward()) {
								g_state.state = JF_STATE_PLAYBACK_NAVIGATING;
							}
						}
						break;
					case MPV_EVENT_SEEK:
						if (g_state.state == JF_STATE_PLAYBACK_START_MARK) {
							mpv_set_property_string(g_mpv_ctx, "start", "none");
							g_state.state = JF_STATE_PLAYBACK;
						}
						break;
					case MPV_EVENT_PROPERTY_CHANGE:
						if (strcmp("time-pos", ((mpv_event_property *)event->data)->name) != 0) break;
						if (((mpv_event_property *)event->data)->format == MPV_FORMAT_NONE) break;
						// event valid, check if need to update the server
						playback_ticks = JF_SECS_TO_TICKS(*(int64_t *)((mpv_event_property *)event->data)->data);
						if (llabs(playback_ticks - g_state.now_playing.playback_ticks) < JF_SECS_TO_TICKS(10)) break;
						// good for update; note this will also start a playback session if none are there
						if ((progress_post = jf_json_generate_progress_post(g_state.now_playing.id, playback_ticks)) == NULL) {
							fprintf(stderr, "Warning: progress update jf_json_generate_progress_post returned NULL.\n");
							break;
						}
						reply = jf_net_request("/sessions/playing/progress", JF_REQUEST_IN_MEMORY, progress_post);
						free(progress_post);
						if (reply == NULL || JF_REPLY_PTR_HAS_ERROR(reply)) {
							fprintf(stderr, "Warning: progress update jf_net_request: %s.\n", jf_reply_error_string(reply));
						} else {
							g_state.now_playing.playback_ticks = playback_ticks;
						}
						jf_reply_free(reply);
						break;
					case MPV_EVENT_IDLE:
						if (g_state.state == JF_STATE_PLAYBACK_NAVIGATING) {
							// digest idle event while we move to the next track
							g_state.state = JF_STATE_PLAYBACK;
						} else {
							// go into UI mode
							g_state.state = JF_STATE_MENU_UI;
							JF_MPV_FATAL(mpv_set_property(g_mpv_ctx, "terminal", MPV_FORMAT_FLAG, &mpv_flag_no));
							while (g_state.state == JF_STATE_MENU_UI) jf_menu_ui();
							JF_MPV_FATAL(mpv_set_property(g_mpv_ctx, "terminal", MPV_FORMAT_FLAG, &mpv_flag_yes));
						}
						break;
					case MPV_EVENT_SHUTDOWN:
						// it is unfortunate, but the cleanest way to handle this case
						// (which is when mpv receives a "quit" command)
						// is to comply and create a new context
						mpv_terminate_destroy(g_mpv_ctx);
						if ((g_mpv_ctx = jf_mpv_context_new()) == NULL) {
							exit(EXIT_FAILURE);
						}
						break;
					default:
						// no-op on everything else
						break;
				}
		}
	}
	///////////////////////////////


	// never reached
	exit(EXIT_SUCCESS);
}
