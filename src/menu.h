#ifndef _JF_MENU
#define _JF_MENU


#include "shared.h"

#include <stddef.h>


////////// CODE MACROS //////////
#define JF_FILTER_URL_APPEND(_f, _s)                            \
    if (s_filters & (_f)) {                                     \
        s_filters_print_string[s_filters_print_len] = ' ';      \
        s_filters_print_len++;                                  \
        if (first_filter == false) {                            \
            s_filters_query_string[s_filters_len] = ',';        \
            s_filters_len++;                                    \
            s_filters_print_string[s_filters_print_len] = ',';  \
            s_filters_print_len++;                              \
        }                                                       \
        strncpy(s_filters_query_string + s_filters_len,         \
                (_s),                                           \
                JF_STATIC_STRLEN((_s)));                        \
        s_filters_len += JF_STATIC_STRLEN((_s));                \
        first_filter = false;                                   \
        strncpy(s_filters_print_string + s_filters_print_len,   \
                (_s),                                           \
                JF_STATIC_STRLEN((_s)));                        \
        s_filters_print_len += JF_STATIC_STRLEN((_s));          \
        first_filter = false;                                   \
    }
/////////////////////////////////


////////// QUERY FILTERS //////////
typedef uint8_t jf_filter_mask;

typedef enum jf_filter {
    JF_FILTER_NONE = 0,
    JF_FILTER_IS_PLAYED = 1 << 0,
    JF_FILTER_IS_UNPLAYED = 1 << 1,
    JF_FILTER_RESUMABLE = 1 << 2,
    JF_FILTER_FAVORITE = 1 << 3, // blasted american english
    JF_FILTER_LIKES = 1 << 4,
    JF_FILTER_DISLIKES = 1 << 5
} jf_filter;


void jf_menu_filters_clear(void);
bool jf_menu_filters_add(const enum jf_filter filter);
///////////////////////////////////


////////// PLAYED STATUS //////////
#define JF_PLAYED_STATUS_REQUESTS_LEN (JF_NET_ASYNC_THREADS * 4)

typedef enum jf_played_status {
    JF_PLAYED_STATUS_YES = 0,
    JF_PLAYED_STATUS_NO = 1
    // there should technically be a third entry for a partially played file
    // but we don't need it
} jf_played_status;


void jf_menu_child_mark_played(const size_t n, const jf_played_status status);
void jf_menu_item_mark_played_detach(const jf_menu_item *item, const jf_played_status status);
void jf_menu_item_mark_played_await_all(void);
///////////////////////////////////


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
