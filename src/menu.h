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


////////// CODE MACROS //////////
#define JF_CLEAR_STDIN()									\
	do {													\
		fcntl(0, F_SETFL, fcntl(0, F_GETFL)|O_NONBLOCK);	\
		while (getchar() != EOF) ;							\
		fcntl(0, F_SETFL, fcntl(0, F_GETFL)& ~O_NONBLOCK);	\
	} while (false)

#define JF_MENU_UI_GET_REQUEST_URL_FATAL()												\
	do {																				\
		if ((request_url = jf_menu_item_get_request_url(s_context)) == NULL) {			\
			fprintf(stderr, "FATAL: could not get request url for menu s_context.\n");	\
			jf_menu_item_free(s_context);												\
			s_context = NULL;															\
			return JF_MENU_UI_STATUS_ERROR;												\
		}																				\
		printf("DEBUG: request_url = %s\n", request_url);								\
	} while (false)

#define JF_MENU_UI_DO_REQUEST_FATAL(request_type)								\
	do {																		\
		if ((reply = jf_request(request_url, request_type, NULL)) == NULL) {	\
			fprintf(stderr, "FATAL: could not allocate jf_reply.\n");			\
			jf_menu_item_free(s_context);										\
			s_context = NULL;													\
			free(request_url);													\
			return JF_MENU_UI_STATUS_ERROR;										\
		}																		\
	} while (false)

#define JF_MENU_UI_FOLDER_CHECK_REPLY_FATAL()									\
	do {																		\
		if (JF_REPLY_PTR_HAS_ERROR(reply)) {									\
			jf_menu_item_free(s_context);										\
			if (JF_REPLY_PTR_ERROR_IS(reply, JF_REPLY_ERROR_PARSER_DEAD)) {		\
				fprintf(stderr, "FATAL: %s\n", jf_reply_error_string(reply));	\
				jf_reply_free(reply);											\
				return JF_MENU_UI_STATUS_ERROR;									\
			} else {															\
				fprintf(stderr, "ERROR: %s.\n", jf_reply_error_string(reply));	\
				jf_reply_free(reply);											\
				jf_thread_buffer_clear_error();									\
				return JF_MENU_UI_STATUS_GO_ON;									\
			}																	\
		}																		\
	} while (false)

#define JF_MENU_UI_PRINT_LEADER(tag, index)	\
	do {									\
		JF_STATIC_PRINT(tag " ");			\
		jf_print_zu(index);					\
		JF_STATIC_PRINT(". ");				\
	} while (false)

#define JF_MENU_UI_PRINT_FOLDER_TITLE()						\
	do {													\
		JF_STATIC_PRINT("\n===== ");						\
		write(1, s_context->name, strlen(s_context->name));	\
		JF_STATIC_PRINT(" =====\n");						\
	} while (false)

#define JF_MENU_UI_PRINT_FOLDER(request_type)			\
	do {												\
			JF_MENU_UI_GET_REQUEST_URL_FATAL();			\
			JF_MENU_UI_DO_REQUEST_FATAL(request_type);	\
			free(request_url);							\
			JF_MENU_UI_FOLDER_CHECK_REPLY_FATAL();		\
			jf_reply_free(reply);						\
			jf_menu_stack_push(s_context);				\
	} while (false)
/////////////////////////////////
		

////////// JF_MENU_UI_STATUS //////////
typedef unsigned char jf_menu_ui_status;

#define JF_MENU_UI_STATUS_GO_ON		0
#define JF_MENU_UI_STATUS_PLAYBACK	1
#define JF_MENU_UI_STATUS_ERROR		2
#define JF_MENU_UI_STATUS_QUIT		3
///////////////////////////////////////


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


void jf_menu_child_dispatch(const size_t n);


size_t jf_menu_child_count(void);


void jf_menu_dotdot(void);


void jf_menu_quit(void);


// Function: jf_menu_ui
//
// Runs the user interface loop until switching context to mpv or exiting.
//
// Returns:
// 	- JF_MENU_UI_STATUS_GO_ON if the UI loop should iterate again;
// 	- JF_MENU_UI_STATUS_PLAYBACK if the context should switch to mpv playback;
// 	- JF_MENU_UI_STATUS_ERROR on an error state;
// 	- JF_MENU_UI_STATUS_QUIT if the user requested the application to exit.
jf_menu_ui_status jf_menu_ui(void);
/////////////////////////////////////////


#endif
