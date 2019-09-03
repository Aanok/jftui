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
#include "network.h"
#include "json_parser.h"
#include "config.h"


#define JF_MPV_ERROR_FATAL(status)													\
do {																				\
	if (status < 0) {																\
		fprintf(stderr, "FATAL: mpv API error: %s\n", mpv_error_string(status));	\
		jf_network_cleanup();														\
		jf_options_clear();															\
		mpv_terminate_destroy(g_mpv_ctx);														\
		exit(EXIT_FAILURE);															\
	}																				\
} while (false);


////////// GLOBALS //////////
jf_options g_options;
jf_global_state g_state = (jf_global_state){ 0 };
mpv_handle *g_mpv_ctx = NULL;
/////////////////////////////


static mpv_handle *jf_mpv_context_new(void);


mpv_handle *jf_mpv_context_new()
{
	mpv_handle *ctx;
	int mpv_flag_yes = 1, mpv_flag_no = 0;
	char *x_emby_token;

	if ((ctx = mpv_create()) == NULL) {
		fprintf(stderr, "FATAL: failed to create mpv context.\n");
		return NULL;
	}
	JF_MPV_ERROR_FATAL(mpv_set_option(ctx, "config", MPV_FORMAT_FLAG, &mpv_flag_yes));
	JF_MPV_ERROR_FATAL(mpv_set_option(ctx, "osc", MPV_FORMAT_FLAG, &mpv_flag_yes));
	JF_MPV_ERROR_FATAL(mpv_set_option(ctx, "input-default-bindings", MPV_FORMAT_FLAG, &mpv_flag_yes));
	JF_MPV_ERROR_FATAL(mpv_set_option(ctx, "input-vo-keyboard", MPV_FORMAT_FLAG, &mpv_flag_yes));
	JF_MPV_ERROR_FATAL(mpv_set_option(ctx, "input-terminal", MPV_FORMAT_FLAG, &mpv_flag_yes));
	JF_MPV_ERROR_FATAL(mpv_set_option(ctx, "terminal", MPV_FORMAT_FLAG, &mpv_flag_yes));
	if ((x_emby_token = jf_concat(2, "x-emby-token: ", g_options.token)) == NULL) {
		fprintf(stderr, "FATAL: jf_concat for x-emby-token header field for mpv requests returned NULL.\n");
	}
	JF_MPV_ERROR_FATAL(mpv_set_option_string(ctx, "http-header-fields", x_emby_token));
	free(x_emby_token);

	JF_MPV_ERROR_FATAL(mpv_initialize(ctx));

	return ctx;
}


int main(int argc, char *argv[])
{
	// VARIABLES
	char *config_path;
	mpv_event *event;
	bool run_ok = true;
	jf_menu_ui_status ui_status;
	int mpv_flag_yes = 1, mpv_flag_no = 0;

	// LIBMPV VERSION CHECK
	// required for "osc" option
	if (mpv_client_api_version() < MPV_MAKE_VERSION(1,23)) {
		fprintf(stderr, "FATAL: found libmpv version %lu.%lu, but 1.23 or greater is required.\n", mpv_client_api_version() >> 16, mpv_client_api_version() & 0xFFFF);
		exit(EXIT_FAILURE);
	}
	///////////////////////


	// VARIABLES INIT
	jf_options_init();
	jf_menu_stack_init();
	/////////////////
	
	
	// TODO command line arguments
	

	// SETUP NETWORK
	// NB the network unit keeps a reference to options, not a copy. Keep it live till cleanup!
	if (! jf_network_pre_init()) {
		fprintf(stderr, "FATAL: could not initialize network context.\n");
		exit(EXIT_FAILURE);
	}
	////////////////
	

	// READ AND PARSE CONFIGURATION FILE
	// apply config directory location default unless there was user override
	if (g_state.config_dir == NULL) {
		if ((g_state.config_dir = jf_config_get_default_dir()) == NULL) {
			JF_STATIC_PRINT_ERROR("FATAL: could not acquire configuration directory location. $HOME could not be read and --config-dir was not passed.\n");
			exit(EXIT_FAILURE);
		}
	}

	// get location of config file
	if ((config_path = jf_concat(2, g_state.config_dir, "/options")) == NULL) {
		JF_STATIC_PRINT_ERROR("FATAL: could not compute config_path.\n");
		exit(EXIT_FAILURE);
	}

	// check config file exists
	errno = 0;
	if (access(config_path, F_OK) == 0) {
		// it's there: read it
		if (! jf_config_read(config_path)) {
			free(config_path);
			exit(EXIT_FAILURE);
		}
		// if server, userid or token are missing (may happen if the config file was edited badly)
		if (g_options.server == NULL || g_options.userid == NULL || g_options.token == NULL) {
			jf_user_config();
		}
	} else if (errno == ENOENT || errno == ENOTDIR) {
		// it's not there
		jf_user_config();
	} else {
		int access_errno = errno;
		JF_STATIC_PRINT_ERROR("FATAL access for config file at location ");
		write(2, config_path, strlen(config_path));
		JF_STATIC_PRINT_ERROR(": ");
		write(2, strerror(access_errno), strlen(strerror(access_errno)));
		JF_STATIC_PRINT_ERROR("\n");
		free(config_path);
		exit(EXIT_FAILURE);
	}
	////////////////////////////////////
	
	// DOUBLE CHECK AND FINALIZE NETWORK CONFIG
	if (! jf_network_refresh_config()) {
		free(config_path);
		exit(EXIT_FAILURE);
	}
	// TODO ping server
	// TODO check token still valid, prompt relogin otherwise
	
	// COMMIT CONFIG TO DISK
	jf_config_write(config_path);
	free(config_path);
	//////////////////////////////////////////


	// SETUP MPV
	if (setlocale(LC_NUMERIC, "C") == NULL) {
		fprintf(stderr, "WARNING: could not set numeric locale to sane standard. mpv might refuse to work.\n");
	}

	if ((g_mpv_ctx = jf_mpv_context_new()) == NULL) {
		jf_network_cleanup();
		jf_options_clear();
		exit(EXIT_FAILURE);
	}
	////////////

//  	mpv_command_string(g_mpv_ctx, "loadfile /home/fabrizio/Music/future_people.opus append");
	// NB there is now way to prebuild a playlist and pass it to mpv once
	// you need to "loadfile file1" then "loadfile file2 append"

	// MAIN LOOP
	while (run_ok) {
		event = mpv_wait_event(g_mpv_ctx, -1);
		printf("event: %s\n", mpv_event_name(event->event_id)); //debug
		switch (event->event_id) {
			case MPV_EVENT_END_FILE:
				// reason MPV_END_FILE_REASON_STOP (2) is next/prev on playlist and we should ignore it
				// reason MPV_END_FILE_REASON_EOF (0) is triggered BOTH mid-playlist AND at the end of a playlist :/
				// reason MPV_END_FILE_REASON_QUIT (3) is when the user presses q mid-playback
				printf("\treason: %d\n", ((mpv_event_end_file *)event->data)->reason);
				break;
			case MPV_EVENT_IDLE:
				// go into UI mode
				JF_MPV_ERROR_FATAL(mpv_set_property(g_mpv_ctx, "terminal", MPV_FORMAT_FLAG, &mpv_flag_no));
				while ((ui_status = jf_menu_ui()) == JF_MENU_UI_STATUS_GO_ON) ;
				switch (ui_status) {
					case JF_MENU_UI_STATUS_ERROR:
					case JF_MENU_UI_STATUS_QUIT:
						run_ok = false;
						break;
					default:
						// GO_ON (which never happens) and PLAYBACK
						// TODO: double check that we don't have something to do for PLAYBACK
						break;
				}
				JF_MPV_ERROR_FATAL(mpv_set_property(g_mpv_ctx, "terminal", MPV_FORMAT_FLAG, &mpv_flag_yes));
				break;
			case MPV_EVENT_SHUTDOWN:
				// it is unfortunate, but the cleanest way to handle this case
				// (which is when mpv receives a "quit" command)
				// is to comply and create a new context
				mpv_terminate_destroy(g_mpv_ctx);
				if ((g_mpv_ctx = jf_mpv_context_new()) == NULL) {
					jf_menu_stack_clear();
					jf_network_cleanup();
					jf_options_clear();
					exit(EXIT_FAILURE);
				}
				break;
			default:
				// no-op on everything else
				break;
		}
	}
	////////////


	// CLEANUP FOR EXIT
	jf_menu_stack_clear();
	jf_network_cleanup();
	jf_options_clear();
	mpv_terminate_destroy(g_mpv_ctx);
	///////////////////
	

	exit(EXIT_SUCCESS);
}
