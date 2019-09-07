#ifndef _JF_MENU
#define _JF_MENU

#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <fcntl.h>
#include <mpv/client.h>

#include "linenoise.h"

#include "shared.h"
#include "config.h"
#include "network.h"
#include "disk_io.h"


////////// CODE MACROS //////////
#define JF_CLEAR_STDIN()									\
	do {													\
		fcntl(0, F_SETFL, fcntl(0, F_GETFL)|O_NONBLOCK);	\
		while (getchar() != EOF) ;							\
		fcntl(0, F_SETFL, fcntl(0, F_GETFL)& ~O_NONBLOCK);	\
	} while (false)

#define JF_MENU_GET_REQUEST_URL_FATAL(item, url)											\
	do {																					\
		if ((url = jf_menu_item_get_request_url(item)) == NULL) {							\
			fprintf(stderr, "FATAL: could not get request url for item %s.\n", item->name);	\
			jf_menu_item_free(item);														\
			item = NULL;																	\
			g_state.state = JF_STATE_FAIL;													\
			return false;																	\
		}																					\
		printf("DEBUG: request_url = %s\n", url);											\
	} while (false)

#define JF_MENU_DO_REQUEST_FATAL(item, url, request_type)						\
	do {																		\
		if ((reply = jf_request(url, request_type, NULL)) == NULL) {			\
			fprintf(stderr, "FATAL: could not allocate jf_reply.\n");			\
			jf_menu_item_free(item);											\
			item = NULL;														\
			free(request_url);													\
			g_state.state = JF_STATE_FAIL;										\
			return false;														\
		}																		\
	} while (false)

#define JF_MENU_FOLDER_CHECK_REPLY_FATAL(item, reply)							\
	do {																		\
		if (JF_REPLY_PTR_HAS_ERROR(reply)) {									\
			jf_menu_item_free(item);											\
			if (JF_REPLY_PTR_ERROR_IS(reply, JF_REPLY_ERROR_PARSER_DEAD)) {		\
				fprintf(stderr, "FATAL: %s\n", jf_reply_error_string(reply));	\
				jf_reply_free(reply);											\
				g_state.state = JF_STATE_FAIL;									\
				return false;													\
			} else {															\
				fprintf(stderr, "ERROR: %s.\n", jf_reply_error_string(reply));	\
				jf_reply_free(reply);											\
				jf_thread_buffer_clear_error();									\
				return false;													\
			}																	\
		}																		\
	} while (false)

#define JF_MENU_PRINT_TITLE(title)				\
	do {										\
		printf("\n===== %s =====\n", title);	\
	} while (false)

#define JF_MENU_PRINT_FOLDER_FATAL(item, request_type)				\
	do {															\
		char *request_url;											\
		jf_reply *reply;											\
		JF_MENU_GET_REQUEST_URL_FATAL(item, request_url);			\
		JF_MENU_DO_REQUEST_FATAL(item, request_url, request_type);	\
		free(request_url);											\
		JF_MENU_FOLDER_CHECK_REPLY_FATAL(item, reply);				\
		jf_reply_free(reply);										\
		jf_menu_stack_push(item);									\
	} while (false)
/////////////////////////////////
		

////////// JF_MENU_STACK //////////
typedef struct jf_menu_stack {
	jf_menu_item **items;
	size_t size;
	size_t used;
} jf_menu_stack;


// Procedure: jf_menu_stack_init
//
// Initializes the global menu stack struct.
//
// Returns:
// 	true on success, false on failure.
bool jf_menu_stack_init(void);


// Procedure: jf_menu_stack_clear
//
// Clears the contents of the global menu stack, forcibly deallocating all items
// regardless of their persistency bit.
void jf_menu_stack_clear(void);
///////////////////////////////////


////////// USER INTERFACE LOOP //////////

jf_item_type jf_menu_child_get_type(size_t n);


size_t jf_menu_child_count(void);


// false on error
bool jf_menu_child_dispatch(const size_t n);


void jf_menu_dotdot(void);


void jf_menu_quit(void);


bool jf_menu_playlist_forward(void);
bool jf_menu_playlist_backward(void);

// Function: jf_menu_ui
//
// Runs the user interface loop until switching context to mpv or exiting.
void jf_menu_ui(void);
/////////////////////////////////////////


#endif
