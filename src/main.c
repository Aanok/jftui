///////////////////////////////////
#define _POSIX_C_SOURCE 200809L  //
///////////////////////////////////

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <locale.h>
#include <mpv/client.h>

#include "shared.h"
#include "net.h"
#include "json.h"
#include "config.h"
#include "disk.h"


////////// CODE MACROS //////////
#define JF_MPV_ASSERT(_s)													\
do {																		\
	int _status = _s;														\
	if (_status < 0) {														\
		fprintf(stderr, "%s:%d: " #_s " failed.\n", __FILE__, __LINE__);	\
		fprintf(stderr, "FATAL: mpv API error: %s.\n",						\
				mpv_error_string(_status));									\
		exit(EXIT_FAILURE);													\
	}																		\
} while (false)
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
static JF_FORCE_INLINE void jf_mpv_version_check(void);
static void jf_abort(int sig);
static void jf_print_usage(void);
static JF_FORCE_INLINE void jf_missing_arg(const char *arg);
static mpv_handle *jf_mpv_context_new(void);
static JF_FORCE_INLINE void jf_mpv_event_dispatch(const mpv_event *event);
//////////////////////////////////////


////////// MISCELLANEOUS GARBAGE //////////
static JF_FORCE_INLINE void jf_mpv_version_check(void)
{
	unsigned long mpv_version = mpv_client_api_version();
	if (mpv_version < MPV_MAKE_VERSION(1,24)) {
		fprintf(stderr, "FATAL: found libmpv version %lu.%lu, but 1.24 or greater is required.\n",
				mpv_version >> 16, mpv_version & 0xFFFF);
		exit(EXIT_FAILURE);
	}
	// future proofing
	if (mpv_version >= MPV_MAKE_VERSION(2,0)) {
		fprintf(stderr, "Warning: found libmpv version %lu.%lu, but jftui expects 1.xx. mpv will probably not work.\n",
				mpv_version >> 16, mpv_version & 0xFFFF);
	}
}


static void jf_abort(int sig)
{
	// some of this is not async-signal-safe
	// but what's the worst that can happen, a crash? :^)
	if (sig == SIGABRT) {
		perror("FATAL");
	}
	jf_disk_clear();
	jf_net_clear();
	_exit(EXIT_FAILURE);
}


static void jf_print_usage() {
	printf("Usage:\n");
	printf("\t--help\n");
	printf("\t--config-dir <directory> (default: $XDG_CONFIG_HOME/jftui)\n");
	printf("\t--runtime-dir <directory> (default: $XDG_DATA_HOME/jftui)\n");
	printf("\t--login.\n");
}


static JF_FORCE_INLINE void jf_missing_arg(const char *arg)
{
	fprintf(stderr, "FATAL: missing parameter for argument %s\n", arg);
	jf_print_usage();
}


static mpv_handle *jf_mpv_context_new()
{
	mpv_handle *ctx;
	int mpv_flag_yes = 1;
	char *x_emby_token;

	assert((ctx = mpv_create()) != NULL);
	JF_MPV_ASSERT(mpv_set_property(ctx, "config-dir", MPV_FORMAT_STRING, &g_state.config_dir));
	JF_MPV_ASSERT(mpv_set_property(ctx, "config", MPV_FORMAT_FLAG, &mpv_flag_yes));
	JF_MPV_ASSERT(mpv_set_property(ctx, "osc", MPV_FORMAT_FLAG, &mpv_flag_yes));
	JF_MPV_ASSERT(mpv_set_property(ctx, "input-default-bindings", MPV_FORMAT_FLAG, &mpv_flag_yes));
	JF_MPV_ASSERT(mpv_set_property(ctx, "input-vo-keyboard", MPV_FORMAT_FLAG, &mpv_flag_yes));
	JF_MPV_ASSERT(mpv_set_property(ctx, "input-terminal", MPV_FORMAT_FLAG, &mpv_flag_yes));
	JF_MPV_ASSERT(mpv_set_property(ctx, "terminal", MPV_FORMAT_FLAG, &mpv_flag_yes));
	assert((x_emby_token = jf_concat(2, "x-emby-token: ", g_options.token)) != NULL);
	JF_MPV_ASSERT(mpv_set_property_string(ctx, "http-header-fields", x_emby_token));
	free(x_emby_token);
	JF_MPV_ASSERT(mpv_observe_property(ctx, 0, "time-pos", MPV_FORMAT_INT64));

	JF_MPV_ASSERT(mpv_initialize(ctx));

	return ctx;
}


static JF_FORCE_INLINE void jf_mpv_event_dispatch(const mpv_event *event)
{
	char *progress_post;
	int64_t playback_ticks;
	int mpv_flag_yes = 1, mpv_flag_no = 0;

// 	printf("DEBUG: event: %s\n", mpv_event_name(event->event_id));
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
			progress_post = jf_json_generate_progress_post(g_state.now_playing.id, playback_ticks);
			jf_net_request("/sessions/playing/stopped", JF_REQUEST_ASYNC_DETACH, progress_post);
			free(progress_post);
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
			progress_post = jf_json_generate_progress_post(g_state.now_playing.id, playback_ticks);
			jf_net_request("/sessions/playing/progress", JF_REQUEST_ASYNC_DETACH, progress_post);
			free(progress_post);
			g_state.now_playing.playback_ticks = playback_ticks;
			break;
		case MPV_EVENT_IDLE:
			if (g_state.state == JF_STATE_PLAYBACK_NAVIGATING) {
				// digest idle event while we move to the next track
				g_state.state = JF_STATE_PLAYBACK;
			} else {
				// go into UI mode
				g_state.state = JF_STATE_MENU_UI;
				JF_MPV_ASSERT(mpv_set_property(g_mpv_ctx, "terminal", MPV_FORMAT_FLAG, &mpv_flag_no));
				while (g_state.state == JF_STATE_MENU_UI) jf_menu_ui();
				JF_MPV_ASSERT(mpv_set_property(g_mpv_ctx, "terminal", MPV_FORMAT_FLAG, &mpv_flag_yes));
			}
			break;
		case MPV_EVENT_SHUTDOWN:
			// tell jellyfin playback stopped
			progress_post = jf_json_generate_progress_post(g_state.now_playing.id, g_state.now_playing.playback_ticks);
			jf_net_request("/sessions/playing/stopped", JF_REQUEST_ASYNC_DETACH, progress_post);
			free(progress_post);
			// it is unfortunate, but the cleanest way to handle this case
			// (which is when mpv receives a "quit" command)
			// is to comply and create a new context
			mpv_terminate_destroy(g_mpv_ctx);
			g_mpv_ctx = jf_mpv_context_new();
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
	jf_reply *reply;


	// SIGNAL HANDLERS
	assert(signal(SIGABRT, jf_abort) != SIG_ERR);
	assert(signal(SIGINT, jf_abort) != SIG_ERR);
	//////////////////


	// LIBMPV VERSION CHECK
	// required for "osc" option
	jf_mpv_version_check();
	///////////////////////


	// SETUP OPTIONS
	g_options = (jf_options){ 0 }; 
	g_options.ssl_verifyhost = JF_CONFIG_SSL_VERIFYHOST_DEFAULT;
	atexit(jf_options_clear);
	////////////////


	// SETUP GLOBAL STATE
	g_state = (jf_global_state){ 0 };
	assert((g_state.session_id = jf_generate_random_id(0)) != NULL);
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
				jf_missing_arg("--config-dir");
				exit(EXIT_FAILURE);
			}
			assert((g_state.config_dir = strdup(argv[i])) != NULL);
		} else if (strcmp(argv[i], "--runtime-dir") == 0) {
			if (++i >= argc) {
				jf_missing_arg("--runtime-dir");
				exit(EXIT_FAILURE);
			}
			assert((g_state.runtime_dir = strdup(argv[i])) != NULL);
		} else if (strcmp(argv[i], "--login") == 0) {
			g_state.state = JF_STATE_STARTING_LOGIN;
		} else {
			fprintf(stderr, "FATAL: unrecognized argument %s.\n", argv[i]);
			jf_print_usage();
			exit(EXIT_FAILURE);
		}
	}
	/////////////////////////
	

	// SETUP DISK
	// apply runtime directory location default unless there was user override
	if (g_state.runtime_dir == NULL
			&& (g_state.runtime_dir = jf_disk_get_default_runtime_dir()) == NULL) {
		fprintf(stderr, "FATAL: could not acquire runtime directory location. $HOME could not be read and --runtime-dir was not passed.\n");
		exit(EXIT_FAILURE);
	}
	jf_disk_init();
	atexit(jf_disk_clear);
	/////////////


	// READ AND PARSE CONFIGURATION FILE
	// apply config directory location default unless there was user override
	if (g_state.config_dir == NULL
			&& (g_state.config_dir = jf_config_get_default_dir()) == NULL) {
		fprintf(stderr, "FATAL: could not acquire configuration directory location. $HOME could not be read and --config-dir was not passed.\n");
		exit(EXIT_FAILURE);
	}
	// get expected location of config file
	config_path = jf_concat(2, g_state.config_dir, "/settings");

	// check config file exists
	if (access(config_path, F_OK) == 0) {
		// it's there: read it
		jf_config_read(config_path);
		// if fundamental fields are missing (file corrupted for some reason)
		if (g_options.server == NULL
				|| g_options.userid == NULL
				|| g_options.token == NULL) {
			if (! jf_menu_user_ask_yn("Error: settings file missing fundamental fields. Would you like to go through manual configuration?")) {
				exit(EXIT_SUCCESS);
			}
			free(g_options.server);
			free(g_options.userid);
			free(g_options.token);
			g_state.state = JF_STATE_STARTING_FULL_CONFIG;
		}
	} else if (errno == ENOENT || errno == ENOTDIR) {
		// it's not there
		if (! jf_menu_user_ask_yn("Settings file not found. Would you like to configure jftui?")) {
			exit(EXIT_SUCCESS);
		}
		g_state.state = JF_STATE_STARTING_FULL_CONFIG;
	} else {
		fprintf(stderr, "FATAL: access for settings file at location %s: %s.\n",
			config_path, strerror(errno));
		exit(EXIT_FAILURE);
	}
	////////////////////////////////////


	// NETWORK SETUP
	jf_net_init();
 	atexit(jf_net_clear);
	////////////////
	

	// INTERACTIVE CONFIG
	if (g_state.state == JF_STATE_STARTING_FULL_CONFIG) {
		jf_config_ask_user();
	} else if (g_state.state == JF_STATE_STARTING_LOGIN) {
		jf_config_ask_user_login();
	}
	
	// save to disk
	jf_config_write(config_path);
	free(config_path);

	if (g_state.state != JF_STATE_STARTING) {
		printf("Please restart to apply the new settings.\n");
		exit(EXIT_SUCCESS);
	}
	/////////////////////
	

	// SERVER NAME
	// this doubles up as a check for connectivity and correct login parameters
	reply = jf_net_request("/system/info", JF_REQUEST_IN_MEMORY, NULL);
	if (JF_REPLY_PTR_HAS_ERROR(reply)) {
		fprintf(stderr, "FATAL: could not reach server: %s.\n", jf_reply_error_string(reply));
		exit(EXIT_FAILURE);
	}
	jf_json_parse_server_info_response(reply->payload);
	jf_reply_free(reply);
	//////////////
	

	// SETUP MENU
	jf_menu_init();
	/////////////////


	// SETUP MPV
	if (setlocale(LC_NUMERIC, "C") == NULL) {
		fprintf(stderr, "Warning: could not set numeric locale to sane standard. mpv might refuse to work.\n");
	}
	g_mpv_ctx = jf_mpv_context_new();
	atexit(jf_mpv_clear);
	////////////


	////////// MAIN LOOP //////////
	while (true) {
		switch (g_state.state) {
			case JF_STATE_USER_QUIT:
				exit(EXIT_SUCCESS);
				break;
			case JF_STATE_FAIL:
				exit(EXIT_FAILURE);
				break;
			default:
				jf_mpv_event_dispatch(mpv_wait_event(g_mpv_ctx, -1));
		}
	}
	///////////////////////////////


	// never reached
	exit(EXIT_SUCCESS);
}
///////////////////////////////
