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
		mpv_destroy(mpv_ctx);														\
		exit(EXIT_FAILURE);															\
	}																				\
} while (false);


////////// GLOBALS //////////
jf_options g_options;
/////////////////////////////


int main(int argc, char *argv[])
{
	// VARIABLES
	char *config_path;
	mpv_handle *mpv_ctx;
	mpv_event *event;
	bool run_ok = true;

	// LIBMPV VERSION CHECK
	// required due to the use of "set_property"
	if (mpv_client_api_version() < MPV_MAKE_VERSION(1,26)) {
		fprintf(stderr, "FATAL: libmpv version 1.26 or greater required.\n");
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
	if ((config_path = jf_config_get_path()) == NULL) {
		fprintf(stderr, "FATAL: could not acquire configuration file location. $HOME could not be read and --config was not passed.\n");
		exit(EXIT_FAILURE);
	}

	// check config file exists
	errno = 0;
	if (access(config_path, F_OK) == 0) {
		// it's there
		if (! jf_config_read(config_path)) {
			fprintf(stderr, "FATAL: error during config parse.\n");
			exit(EXIT_FAILURE);
		}
		if (g_options.error != NULL) {
			fprintf(stderr, "%s\n", g_options.error);
			free(config_path);
			jf_options_clear();
			exit(EXIT_FAILURE);
		}

		// if server, userid or token are missing (may happen if the config file was edited badly)
		if (g_options.server == NULL || g_options.userid == NULL || g_options.token == NULL) {
			jf_user_config();
		}
	} else if (errno == ENOENT) {
		// it's not there
		jf_user_config();
	} else {
		fprintf(stderr, "FATAL: access for config file at location %s: %s\n", config_path, strerror(errno));
		free(config_path);
		exit(EXIT_FAILURE);
	}
	////////////////////////////////////
	

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

	if ((mpv_ctx = mpv_create()) == NULL) {
		fprintf(stderr, "FATAL: failed to create mpv context.\n");
		jf_options_clear();
		exit(EXIT_FAILURE);
	}
	{
		int flag_yes = 1;
		JF_MPV_ERROR_FATAL(mpv_set_property(mpv_ctx, "config", MPV_FORMAT_FLAG, &flag_yes));
		JF_MPV_ERROR_FATAL(mpv_set_property(mpv_ctx, "osc", MPV_FORMAT_FLAG, &flag_yes));
		JF_MPV_ERROR_FATAL(mpv_set_property(mpv_ctx, "input-default-bindings", MPV_FORMAT_FLAG, &flag_yes));
		JF_MPV_ERROR_FATAL(mpv_set_property(mpv_ctx, "input-vo-keyboard", MPV_FORMAT_FLAG, &flag_yes));
		JF_MPV_ERROR_FATAL(mpv_set_property(mpv_ctx, "input-terminal", MPV_FORMAT_FLAG, &flag_yes));
		JF_MPV_ERROR_FATAL(mpv_set_property(mpv_ctx, "terminal", MPV_FORMAT_FLAG, &flag_yes));
	}

	JF_MPV_ERROR_FATAL(mpv_initialize(mpv_ctx));
	////////////

//  	mpv_command_string(mpv_ctx, "loadfile /home/fabrizio/Music/future_people.opus append");
	// NB there is now way to prebuild a playlist and pass it to mpv once
	// you need to "loadfile file1" then "loadfile file2 append"

	// MAIN LOOP
	while (run_ok) {
		event = mpv_wait_event(mpv_ctx, -1);
		printf("event: %s\n", mpv_event_name(event->event_id)); //debug
		switch (event->event_id) {
			case MPV_EVENT_END_FILE:
				// reason MPV_END_FILE_REASON_STOP (2) is next/prev on playlist and we should ignore it
				// reason MPV_END_FILE_REASON_EOF (0) is triggered BOTH mid-playlist AND at the end of a playlist :/
				// reason MPV_END_FILE_REASON_QUIT (3) is when the user presses q mid-playback
				printf("\treason: %d\n", ((mpv_event_end_file *)event->data)->reason);
				break;
			case MPV_EVENT_IDLE:
				//TODO go into UI mode
				break;
			case MPV_EVENT_SHUTDOWN: //debug, we'll probably want to ignore these
				run_ok = false;
				break;
		}
	}
	////////////


	// CLEANUP FOR EXIT
	jf_menu_stack_clear();
	jf_network_cleanup();
	jf_options_clear();
	mpv_destroy(mpv_ctx);
	///////////////////
	

	exit(EXIT_SUCCESS);
}
