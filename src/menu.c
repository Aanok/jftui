#include "menu.h"


////////// GLOBAL VARIABLES //////////
extern jf_options g_options;
//////////////////////////////////////


////////// STATIC VARIABLES //////////
static jf_menu_stack s_menu_stack;
//////////////////////////////////////


////////// STATIC FUNCTIONS //////////

// Function: jf_menu_item_conditional_free
//
// Deallocates a menu item and all its descendants recursively, with a switch for checking or ignoring the persistence bit.
//
// Parameters:
// 	menu_item - Pointer to the struct to deallocate.
// 	check_persistence - Boolean for checking or not the persistence bit.
//
// Returns:
// 	true on success or menu_item == NULL, false otherwise
static bool jf_menu_item_conditional_free(jf_menu_item *menu_item, const bool check_persistence);

static jf_menu_item *jf_menu_make_ui(void);

// Function: jf_menu_stack_push
//
// Pushes a jf_menu_item on the global menu stack. No-op if NULL is passed.
// REQUIRES: global menu stack struct initialized.
//
// Parameters:
//
// 	menu_item - A pointer to the item to be pushed.
//
// Returns:
// 	true if the item was deallocated or NULL was passed, false otherwise.
static bool jf_menu_stack_push(jf_menu_item *menu_item);

// Function: jf_menu_stack_pop
//
// Pops the top item out of the global menu stack.
// The caller assumes ownership of the popped item (i.e. will have to free it).
// REQUIRES: global menu stack struct initialized.
//
// Returns:
// 	A pointer to the item popped or NULL if the stack is empty.
static jf_menu_item *jf_menu_stack_pop(void);

static bool jf_menu_read_commands(void);
static char *jf_menu_item_get_request_url(const jf_menu_item *item);
//////////////////////////////////////


////////// JF_MENU_ITEM //////////
jf_menu_item *jf_menu_item_new(jf_item_type type, const char *id, jf_menu_item *children)
{
	jf_menu_item *menu_item;

	if ((menu_item = malloc(sizeof(jf_menu_item))) == NULL) {
		return (jf_menu_item *)NULL;
	}

	menu_item->type = type;
	if (id == NULL) {
		menu_item->id[0] = '\0';
	} else {
		strncpy(menu_item->id, id, JF_ID_LENGTH);
		menu_item->id[JF_ID_LENGTH] = '\0';
	}
	menu_item->children = children;

	return menu_item;
}


bool jf_menu_item_free(jf_menu_item *menu_item)
{
	return jf_menu_item_conditional_free(menu_item, true);
}


static bool jf_menu_item_conditional_free(jf_menu_item *menu_item, const bool check_persistent)
{
	jf_menu_item *child;

	if (menu_item == NULL) {
		return true;
	}

	if (! (check_persistent && JF_MENU_ITEM_TYPE_IS_PERSISTENT(menu_item->type))) {
		child = menu_item->children;
		while (child) {
			jf_menu_item_conditional_free(child, check_persistent);
			child++;
		}
		free(menu_item);
		return true;
	}

	return false;
}


static jf_menu_item *jf_menu_make_ui()
{
	{
		jf_menu_item *libraries;
		if ((libraries = jf_menu_item_new(JF_ITEM_TYPE_MENU_LIBRARIES, NULL, NULL)) == NULL) {
			return NULL;
		}
		return libraries;
	}
}

//////////////////////////////////


////////// JF_MENU_STACK //////////
bool jf_menu_stack_init()
{
	if ((s_menu_stack.items = malloc(10 * sizeof(jf_menu_item *))) == NULL) {
		return false;
	}
	s_menu_stack.size = 10;
	s_menu_stack.used = 0;

	return true;
}


static bool jf_menu_stack_push(jf_menu_item *menu_item)
{
	if (menu_item == NULL) {
		return true;
	}

	if (s_menu_stack.size == s_menu_stack.used) {
		jf_menu_item **tmp;
		if ((tmp = realloc(s_menu_stack.items, s_menu_stack.size * 2 * sizeof(jf_menu_item *))) == NULL) {
			return false;
		}
		s_menu_stack.size *= 2;
		s_menu_stack.items = tmp;
	}

	s_menu_stack.items[s_menu_stack.used++] = menu_item;
	return true;
}


static jf_menu_item *jf_menu_stack_pop()
{
	jf_menu_item *retval;

	if (s_menu_stack.used == 0) {
		return NULL;
	}

	retval = s_menu_stack.items[--s_menu_stack.used];
	s_menu_stack.items[s_menu_stack.used] = NULL;
	return retval;
}


void jf_menu_stack_clear()
{
	while (s_menu_stack.used > 0) {
		jf_menu_item_conditional_free(jf_menu_stack_pop(), false);
	}
}
///////////////////////////////////


////////// USER INTERFACE LOOP //////////

// TODO innat
static bool jf_menu_read_commands()
{
	size_t i;
	fprintf(stderr, "DEBUG: this function is a stub! Enter a single number to select: ");
	if (scanf("%zu", &i) != 1) {
		while (getchar() != '\n') ; // clear stdin because scanf doesn't on error
		return false;
	}
	jf_menu_stack_push(jf_thread_buffer_get_parsed_item(i));
	return true;
}


// TODO: EXTREMELY incomplete, stub
static char *jf_menu_item_get_request_url(const jf_menu_item *item)
{
	if (item != NULL) {
		switch (item->type) {
			case JF_ITEM_TYPE_COLLECTION:
			case JF_ITEM_TYPE_USER_VIEW:
			case JF_ITEM_TYPE_FOLDER:
			case JF_ITEM_TYPE_PLAYLIST:
			case JF_ITEM_TYPE_ALBUM:
				return jf_concat(5, "/users/", g_options.userid, "/items?parentid=",
						item->id, "&sortby=isfolder,sortname");
			case JF_ITEM_TYPE_ARTIST:
				return jf_concat(5, "/users/", g_options.userid, "/items?albumartistids=",
						item->id, "&recursive=true&includeitemtypes=musicalbum");
			case JF_ITEM_TYPE_MENU_LIBRARIES:
				return jf_concat(3, "/users/", g_options.userid, "/views");
			default:
				fprintf(stderr, "ERROR: get_request_url is a stub and you requested an unsupported item_type (%d).\n", item->type);
				return NULL;
		}
	}
	return NULL;
}


bool jf_user_interface()
{
	jf_menu_item *context, *child;
	jf_reply *reply;
	char *request_url;
	size_t i;

	// ACQUIRE ITEM CONTEXT
	// if menu_stack is empty, assume first time run
	// in case of error it's not an unreasonable fallback
	// TODO: double check if that won't cause memory leaks
	if ((context = jf_menu_stack_pop()) == NULL) {
			if ((context = jf_menu_make_ui()) == NULL) {
			fprintf(stderr, "FATAL: jf_menu_make_ui() returned NULL.\n");
			return false;
		}
		jf_menu_stack_push(context);
		return true;
	}

	do {
		// TODO print title
		printf("\n\n===== oh how I wish I was a real title :( =====\n");

		// dispatch; download more info if necessary
		switch (context->type) {
			// dynamic directories: fetch children, parser prints entries
			case JF_ITEM_TYPE_COLLECTION:
			case JF_ITEM_TYPE_USER_VIEW:
			case JF_ITEM_TYPE_FOLDER:
			case JF_ITEM_TYPE_PLAYLIST:
			case JF_ITEM_TYPE_ARTIST:
			case JF_ITEM_TYPE_ALBUM:
			case JF_ITEM_TYPE_SEASON:
			case JF_ITEM_TYPE_SERIES:
			case JF_ITEM_TYPE_MENU_LIBRARIES:
				if ((request_url = jf_menu_item_get_request_url(context)) == NULL) {
					fprintf(stderr, "FATAL: could not get request url for menu context.\n");
					jf_menu_item_free(context);
					return false;
				}
				printf("request url: %s\n", request_url);
				if ((reply = jf_request(request_url, JF_REQUEST_SAX, NULL)) == NULL) {
					fprintf(stderr, "FATAL: could not allocate jf_reply.\n");
					jf_menu_item_free(context);
					free(request_url);
					return false;
				}
				free(request_url);
				if (JF_REPLY_PTR_HAS_ERROR(reply)) {
					jf_menu_item_free(context);
					if (JF_REPLY_PTR_ERROR_IS(reply, JF_REPLY_ERROR_PARSER_DEAD)) {
						fprintf(stderr, "FATAL: %s\n", jf_reply_error_string(reply));
						jf_reply_free(reply);
						return false;
					} else {
						fprintf(stderr, "ERROR: %s.\n", jf_reply_error_string(reply));
						jf_reply_free(reply);
						jf_thread_buffer_clear_error();
						return true;
					}
				}
				printf("reply content: %s\n", reply->payload);
				jf_reply_free(reply);
				// push back on stack to allow backtracking
				jf_menu_stack_push(context);
				break;
			// persistent menu directories: fetch nothing, print entries by hand
			case JF_ITEM_TYPE_MENU_ROOT:
			case JF_ITEM_TYPE_MENU_FAVORITES:
			case JF_ITEM_TYPE_MENU_ON_DECK:
			case JF_ITEM_TYPE_MENU_LATEST:
				child = context->children;
				i = 0;
				while (child) {
					switch (child->type) {
						case JF_ITEM_TYPE_MENU_FAVORITES:
							printf("D %zu. Favorites\n", i);
							break;
						case JF_ITEM_TYPE_MENU_ON_DECK:
							printf("D %zu. On Deck\n", i);
							break;
						default:
							printf("[!!!] %zu. WARNING: unrecognized menu item. Undefined behaviour if chosen. This is a bug.\n", i);
							break;
					}
					child++;
					i++;
				}
				// push back on stack to allow backtracking
				jf_menu_stack_push(context);
				break;
			default:
				// TODO: individual items should be handled by another function
				printf("Individual item; id: %s\n", context->id);
				jf_menu_item_free(context);
				break;
		}
	} while (jf_menu_read_commands() == false);

	return true;
}
/////////////////////////////////////////
