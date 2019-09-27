#ifndef _JF_MENU
#define _JF_MENU

#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <errno.h>
#include <mpv/client.h>

#include "linenoise.h"

#include "shared.h"
#include "config.h"
#include "net.h"
#include "disk.h"
		

////////// JF_MENU_STACK //////////
typedef struct jf_menu_stack {
	jf_menu_item **items;
	size_t size;
	size_t used;
} jf_menu_stack;
///////////////////////////////////


////////// USER INTERFACE LOOP //////////
jf_item_type jf_menu_child_get_type(size_t n);
size_t jf_menu_child_count(void);
bool jf_menu_child_dispatch(const size_t n);

void jf_menu_dotdot(void);
void jf_menu_quit(void);
void jf_menu_search(const char *s);
bool jf_menu_mark_played(const jf_menu_item *item);

bool jf_menu_playlist_forward(void);
bool jf_menu_playlist_backward(void);

void jf_menu_ui(void);
/////////////////////////////////////////


////////// MISCELLANEOUS //////////
// Procedure: jf_menu_init
//
// Initializes linenoise history and the static menu stack struct.
//
// Returns:
// 	true on success, false on failure.
bool jf_menu_init(void);


// Procedure: jf_menu_clear
//
// Clears the contents of the static menu stack, forcibly deallocating all items
// regardless of their persistency bit.
void jf_menu_clear(void);

char *jf_menu_linenoise(const char *prompt);
bool jf_menu_user_ask_yn(const char *question);
///////////////////////////////////

#endif
