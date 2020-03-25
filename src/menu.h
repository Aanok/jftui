#ifndef _JF_MENU
#define _JF_MENU


#include "shared.h"

#include <stddef.h>


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
void jf_menu_context_reset_filters(void);
void jf_menu_context_add_filter(const enum jf_filters filter);
void jf_menu_search(const char *s);
void jf_menu_mark_played(const jf_menu_item *item);
void jf_menu_mark_unplayed(const jf_menu_item *item);

void jf_menu_ui(void);
/////////////////////////////////////////


////////// MISCELLANEOUS //////////
char *jf_menu_item_get_request_url(const jf_menu_item *item);
void jf_menu_ask_resume(jf_menu_item *item);


// Initializes linenoise history and the static menu stack struct.
// CAN FATAL.
void jf_menu_init(void);


// Clears the contents of the static menu stack, forcibly deallocating all items
// regardless of their persistency bit.
// CAN'T FAIL.
void jf_menu_clear(void);


// Wrapper. Takes care of Ctrl-C (SIGINT) and other IO errors.
// CAN FATAL.
char *jf_menu_linenoise(const char *prompt);


// Prompts user for a question meant for a binary answer and reads reply from
// stdin.
//
// Returns:
//  - true if reply starts with 'y' or 'Y';
//  - false if reply starts with 'n' or 'N'.
// CAN'T FAIL.
bool jf_menu_user_ask_yn(const char *question);


size_t jf_menu_user_ask_selection(const size_t l, const size_t r);
///////////////////////////////////
#endif
