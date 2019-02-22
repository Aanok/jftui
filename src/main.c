#include <stdio.h>
#include <stdlib.h>

#include <curl/curl.h>
#include <yajl/yajl_tree.h>
#include <mpv/client.h>

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
	return 0;
}
