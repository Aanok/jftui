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
		free(options);																\
		mpv_destroy(mpv_ctx);														\
	}																				\
} while (false);


int main(int argc, char *argv[])
{
	// VARIABLES
	char *config_path;
	jf_options *options;
	mpv_handle *mpv_ctx;
	mpv_event *event;

	// LIBMPV VERSION CHECK
	// required due to the use of "set_property"
	if (mpv_client_api_version() < MPV_MAKE_VERSION(1,26)) {
		fprintf(stderr, "FATAL: libmpv version 1.26 or greater required.\n");
		exit(EXIT_FAILURE);
	}
	///////////////////////


	// TODO command line arguments
	

	// READ AND PARSE CONFIGURATION FILE
	if ((config_path = jf_config_get_path()) == NULL) {
		fprintf(stderr, "FATAL: could not acquire configuration file location. $HOME could not be read and --config was not passed.\n");
		exit(EXIT_FAILURE);
	}

	// check config file exists
	errno = 0;
	if (access(config_path, F_OK) == 0) {
		// it's there
		if ((options = jf_config_read(config_path)) == NULL) {
			fprintf(stderr, "FATAL: malloc error allocating jf_options.\n");
			exit(EXIT_FAILURE);
		}
		if (options->error != NULL) {
			fprintf(stderr, "%s\n", options->error);
			free(config_path);
			jf_options_free(options);
			exit(EXIT_FAILURE);
		}

		// if server, userid or token are missing (may happen if the config file was edited badly)
		if (options->server == NULL || options->userid == NULL || options->token == NULL) {
			jf_user_config(options);
		}
	} else if (errno == ENOENT) {
		// it's not there
		options = jf_user_config(NULL);
	} else {
		fprintf(stderr, "FATAL: access for config file at location %s: %s\n", config_path, strerror(errno));
		free(config_path);
		exit(EXIT_FAILURE);
	}
	////////////////////////////////////
	

	// SETUP NETWORK SUBSYSTEM AND PING SERVER
	if (! jf_network_init(options)) {
		fprintf(stderr, "FATAL: could not initialize network context.\n"); free(config_path);
		free(options);
		exit(EXIT_FAILURE);
	}

	// TODO check token still valid, prompt relogin otherwise
	// TODO ping server
	
	jf_config_write(options, config_path);
	free(config_path);
	//////////////////////////////////////////

	// SETUP MPV
	if (setlocale(LC_NUMERIC, "C") == NULL) {
		fprintf(stderr, "WARNING: could not set numeric locale to sane standard. mpv might refuse to work.\n");
	}

	if ((mpv_ctx = mpv_create()) == NULL) {
		fprintf(stderr, "FATAL: failed to create mpv context.\n");
		free(options);
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
	

	// MAIN LOOP
	while (true) {
		event = mpv_wait_event(mpv_ctx, -1);
		printf("event: %s\n", mpv_event_name(event->event_id));
		if (event->event_id == MPV_EVENT_SHUTDOWN) {
			break;
		}
	}
	////////////


	// CLEANUP FOR EXIT
	jf_network_cleanup();
	jf_options_free(options);
	mpv_destroy(mpv_ctx);
	///////////////////
	

	exit(EXIT_SUCCESS);

	/*
	jf_network_init(&options);
	char *json = jf_generate_login_request(argv[2], argv[3]);
	printf("login request: %s\n", json);
	reply = jf_login_request(json);
	free(json);
	printf("reply: %s\n", reply->payload);
	if (reply->size < 0) {
		printf("%s\n", reply->payload);
	} else {
		if (! jf_parse_login_reply(reply->payload, &options)) printf("error in parse\n");
		printf("userid = \"%s\", token = \"%s\"\n", options.userid, options.token);
		jf_network_reload_token();
		free(options.userid);
		free(options.token);
	}
	jf_reply_free(reply);
	*/

// 	size_t n;
// 	scanf("%zu", &n);
// 	jf_menu_item item = jf_thread_buffer_get_parsed_item(n);
// 	printf("type: %d\tid: %.*s\n", item.type, item.type == JF_ITEM_TYPE_NONE ? 0 : JF_ID_LENGTH, item.id);
// 
}
