///////////////////////////////////
#define _POSIX_C_SOURCE 200809L  //
///////////////////////////////////


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <mpv/client.h>

#include "shared.h"
#include "network.h"
#include "json_parser.h"
#include "config.h"


static inline void mpv_check_error(int status)
{
	if (status < 0) {
		fprintf(stderr, "mpv API error: %s\n", mpv_error_string(status));
		exit(EXIT_FAILURE);
	}
}


int main(int argc, char *argv[])
{
	// VARIABLES
	char *config_path;
	jf_options *options;

	// LIBMPV VERSION CHECK
	// required due to the use of "set_property"
	if (mpv_client_api_version() < MPV_MAKE_VERSION(1,26)) {
		fprintf(stderr, "FATAL: libmpv version 1.26 or greater required.\n");
		exit(EXIT_FAILURE);
	}

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
	
	free(config_path);
	jf_options_free(options);
	exit(EXIT_SUCCESS);

// 	mpv_handle *mpv_ctx = mpv_create();
// 	if (!mpv_ctx) {
// 		fprintf(stderr, "FATAL: failed to create mpv context.\n");
// 		exit(EXIT_FAILURE);
// 	}
// 
// 	{
// 		int flas_yes = 1;
// 		mpv_check_error(mpv_set_property(mpv_ctx, "config", MPV_FORMAT_FLAG, &flas_yes));
// 		mpv_check_error(mpv_set_property(mpv_ctx, "osc", MPV_FORMAT_FLAG, &flas_yes));
// 		mpv_check_error(mpv_set_property(mpv_ctx, "input-default-bindings", MPV_FORMAT_FLAG, &flas_yes));
// 		mpv_check_error(mpv_set_property(mpv_ctx, "input-vo-keyboard", MPV_FORMAT_FLAG, &flas_yes));
// 		mpv_check_error(mpv_set_property(mpv_ctx, "input-terminal", MPV_FORMAT_FLAG, &flas_yes));
// 		mpv_check_error(mpv_set_property(mpv_ctx, "terminal", MPV_FORMAT_FLAG, &flas_yes));
// 	}
// 
// 	mpv_check_error(mpv_initialize(mpv_ctx));
// 
// 	mpv_check_error(mpv_command_string(mpv_ctx, "loadfile /home/fabrizio/Music/daisy.opus"));
// 
// 	while (1) {
// 		mpv_event *event = mpv_wait_event(mpv_ctx, -1);
// 		printf("event: %s\n", mpv_event_name(event->event_id));
// 		if (event->event_id == MPV_EVENT_SHUTDOWN) {
// 			break;
// 		}
// 	}
// 
// 	mpv_terminate_destroy(mpv_ctx);
// 	

// 	jf_reply *reply;
// 	const char *config_path = jf_config_get_path();
// 	jf_options *options = jf_config_read(config_path);
// 	if (options == NULL) return 1;
// 
// 	printf("server: \"%s\"\n", options->server);
// 	printf("token: \"%s\"\n", options->token);
// 	printf("userid: \"%s\"\n", options->userid);
// 	printf("client: \"%s\"\n", options->client);
// 	printf("device: \"%s\"\n", options->device);
// 	printf("deviceid: \"%s\"\n", options->deviceid);
// 
// 	jf_config_write(options, config_path);
// 	free((char *)config_path);

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

// 	jf_network_init(options);
// 	char *next_up = jf_concat(3, "/shows/nextup?userid=", options->userid, "&limit=15");
// 	printf("NEXTUP: %s\n", next_up);
// 	reply = jf_request(next_up, JF_REQUEST_SAX_PROMISCUOUS, NULL);
// 	free(next_up);
//	printf("GOT REPLY:\n%s", reply->payload);
// 	jf_reply_free(reply);

// 	// musica
// 	reply = jf_request("/users/b8664437c69e4eb2802fc0a0eda8f852/items?ParentId=1b8414a45d245177d1c134bb724b1d92&SortBy=IsFolder,SortName&SortOrder=Ascending", JF_REQUEST_SAX_PROMISCUOUS, NULL);
// 	jf_reply_free(reply);
// 	// radiodrammi
// 	reply = jf_request("/users/b8664437c69e4eb2802fc0a0eda8f852/items?ParentId=283bb226c01f9dc23b97447df77f04f2&SortBy=IsFolder,SortName&SortOrder=Ascending", JF_REQUEST_SAX_PROMISCUOUS, NULL);
// 	jf_reply_free(reply);
// 
// 	size_t n;
// 	scanf("%zu", &n);
// 	jf_menu_item item = jf_thread_buffer_get_parsed_item(n);
// 	printf("type: %d\tid: %.*s\n", item.type, item.type == JF_ITEM_TYPE_NONE ? 0 : JF_ID_LENGTH, item.id);
// 
// 	jf_options_free(options);
// 	jf_network_cleanup();

	return 0;
}
