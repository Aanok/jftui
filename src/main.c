///////////////////////////////////
#define _POSIX_C_SOURCE 200809L  //
///////////////////////////////////


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <mpv/client.h>

#include <unistd.h> // sleep for debug

#include "shared.h"
#include "network.h"
#include "json_parser.h"

//TODO add check that API version >= 1.23 (because of set_property)


static inline void mpv_check_error(int status)
{
	if (status < 0) {
		fprintf(stderr, "mpv API error: %s\n", mpv_error_string(status));
		exit(EXIT_FAILURE);
	}
}



int main(int argc, char *argv[])
{
	/*
	mpv_handle *mpv_ctx = mpv_create();
	if (!mpv_ctx) {
		fprintf(stderr, "FATAL: failed to create mpv context.\n");
		exit(EXIT_FAILURE);
	}

	{
		int flas_yes = 1;
		mpv_check_error(mpv_set_property(mpv_ctx, "config", MPV_FORMAT_FLAG, &flas_yes));
		mpv_check_error(mpv_set_property(mpv_ctx, "osc", MPV_FORMAT_FLAG, &flas_yes));
		mpv_check_error(mpv_set_property(mpv_ctx, "input-default-bindings", MPV_FORMAT_FLAG, &flas_yes));
		mpv_check_error(mpv_set_property(mpv_ctx, "input-vo-keyboard", MPV_FORMAT_FLAG, &flas_yes));
		mpv_check_error(mpv_set_property(mpv_ctx, "input-terminal", MPV_FORMAT_FLAG, &flas_yes));
		mpv_check_error(mpv_set_property(mpv_ctx, "terminal", MPV_FORMAT_FLAG, &flas_yes));
	}

	mpv_check_error(mpv_initialize(mpv_ctx));

	mpv_check_error(mpv_command_string(mpv_ctx, "loadfile /home/fabrizio/Music/daisy.opus"));

	while (1) {
		mpv_event *event = mpv_wait_event(mpv_ctx, -1);
		printf("event: %s\n", mpv_event_name(event->event_id));
		if (event->event_id == MPV_EVENT_SHUTDOWN) {
			break;
		}
	}

	mpv_terminate_destroy(mpv_ctx);
	*/

	jf_reply *reply;
	jf_options options = {
		.server_url = argv[1],
		.server_url_len = strlen(argv[1]),
		.token = argv[2],
		.userid = argv[3],
		.ssl_verifyhost = 1,
		.client = "jftui",
		.device = "pc",
		.deviceid = "desktop-linux",
		.version = "prealpha"
	};
	

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

	jf_network_init(&options);
	printf("NETWORK INIT DONE\n");
	/*
	reply = jf_request("/users/public", 0, NULL);
	printf("GOT REPLY:\n%s", reply->payload);
	jf_reply_free(reply);
	*/

	// musica
	reply = jf_request("/users/b8664437c69e4eb2802fc0a0eda8f852/items?ParentId=1b8414a45d245177d1c134bb724b1d92&SortBy=IsFolder,SortName&SortOrder=Ascending", JF_REQUEST_SAX_PROMISCUOUS, NULL);
	jf_reply_free(reply);
	// radiodrammi
	reply = jf_request("/users/b8664437c69e4eb2802fc0a0eda8f852/items?ParentId=283bb226c01f9dc23b97447df77f04f2&SortBy=IsFolder,SortName&SortOrder=Ascending", JF_REQUEST_SAX_PROMISCUOUS, NULL);
	jf_reply_free(reply);
	int n;
	scanf("%d", &n);
	jf_menu_item item = jf_thread_buffer_get_parsed_item(n);
	printf("type: %d\tid: %.*s\n", item.type, item.type == JF_ITEM_TYPE_NONE ? 0 : JF_ID_LENGTH, item.id);

	jf_network_cleanup();

	return 0;
}
