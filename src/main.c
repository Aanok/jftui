#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <mpv/client.h>

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
		int flag_yes = 1;
		mpv_check_error(mpv_set_property(mpv_ctx, "config", MPV_FORMAT_FLAG, &flag_yes));
		mpv_check_error(mpv_set_property(mpv_ctx, "osc", MPV_FORMAT_FLAG, &flag_yes));
		mpv_check_error(mpv_set_property(mpv_ctx, "input-default-bindings", MPV_FORMAT_FLAG, &flag_yes));
		mpv_check_error(mpv_set_property(mpv_ctx, "input-vo-keyboard", MPV_FORMAT_FLAG, &flag_yes));
		mpv_check_error(mpv_set_property(mpv_ctx, "input-terminal", MPV_FORMAT_FLAG, &flag_yes));
		mpv_check_error(mpv_set_property(mpv_ctx, "terminal", MPV_FORMAT_FLAG, &flag_yes));
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
		argv[1],
		strlen(argv[1]),
		argv[2],
		0
	};

	jf_network_init(&options);
	reply = jf_request("/users/public", 0);
	printf("%s\n", reply->size < 0 ? jf_reply_error_string(reply) : reply->payload);

	jf_reply_free(reply);
	jf_network_cleanup();

	return 0;
}
