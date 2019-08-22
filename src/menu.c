#include "menu.h"

////////// COMMAND PARSER //////////
#include "command_parser.c"
////////////////////////////////////


////////// GLOBAL VARIABLES //////////
extern jf_options g_options;
extern mpv_handle *g_mpv_ctx;
//////////////////////////////////////


////////// STATIC VARIABLES //////////
static jf_menu_stack s_menu_stack;
static jf_menu_item *s_context = NULL;
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

static jf_menu_item *jf_menu_child_get(size_t n);
static jf_item_type jf_menu_child_get_type(size_t n);
static bool jf_menu_read_commands(void);
static char *jf_menu_item_get_request_url(const jf_menu_item *item);
//////////////////////////////////////


////////// JF_MENU_ITEM //////////
jf_menu_item *jf_menu_item_new(jf_item_type type, const char *id, jf_menu_item **children)
{
	jf_menu_item *menu_item;

	if ((menu_item = malloc(sizeof(jf_menu_item))) == NULL) {
		return NULL;
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
	jf_menu_item **child;

	if (menu_item == NULL) {
		return true;
	}

	if (! (check_persistent && JF_ITEM_TYPE_IS_PERSISTENT(menu_item->type))) {
		if ((child = menu_item->children) != NULL) {
			while (*child != NULL) {
				jf_menu_item_conditional_free(*child, check_persistent);
				child++;
			}
			free(menu_item->children);
		}
		free(menu_item);
		return true;
	}

	return false;
}


static jf_menu_item *jf_menu_make_ui()
{
	jf_menu_item *root = NULL,
				 **root_children = NULL;
	int i;

	if ((root_children = calloc(6, sizeof(jf_menu_item *))) == NULL) {
		return NULL;
	}

	if ((root_children[0] = jf_menu_item_new(JF_ITEM_TYPE_MENU_FAVORITES, NULL, NULL)) == NULL
		|| (root_children[1] = jf_menu_item_new(JF_ITEM_TYPE_MENU_CONTINUE, NULL, NULL)) == NULL
		|| (root_children[2] = jf_menu_item_new(JF_ITEM_TYPE_MENU_NEXT_UP, NULL, NULL)) == NULL
		|| (root_children[3] = jf_menu_item_new(JF_ITEM_TYPE_MENU_LATEST, NULL, NULL)) == NULL
		|| (root_children[4] = jf_menu_item_new(JF_ITEM_TYPE_MENU_LIBRARIES, NULL, NULL)) == NULL
		|| (root = jf_menu_item_new(JF_ITEM_TYPE_MENU_ROOT, NULL, root_children)) == NULL) {
		for (i = 0; i < 5; i++) {
			jf_menu_item_conditional_free(root_children[i], false);
		}
		free(root_children);
		free(root);
		return NULL;
	}

	return root;
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
	// clear non persistent items up to the root
	while (s_menu_stack.used > 1) {
		jf_menu_item_conditional_free(jf_menu_stack_pop(), true);
	}
	// persistent item deletion must cascade from the root because persistent
	// children popped from the stack would get free'd twice otherwise
	jf_menu_item_conditional_free(jf_menu_stack_pop(), false);
}
///////////////////////////////////


////////// USER INTERFACE LOOP //////////
static bool jf_menu_read_commands()
{
	yycontext yy;
	while (true) {
		memset(&yy, 0, sizeof(yycontext));
		printf("> ");
		yyparse(&yy);
		yyrelease(&yy);
		switch (yy_zu_stack_get_state(&yy)) {
			case JF_ZU_STACK_SUCCESS:
				return true;
			case JF_ZU_STACK_FAIL_FOLDER:
				fprintf(stderr, "ERROR: cannot target many folders or both folders and items with non-recursive command.\n");
				JF_CLEAR_STDIN();
				break;
			case JF_ZU_STACK_FAIL_MATCH:
				fprintf(stderr, "ERROR: malformed command.\n");
				JF_CLEAR_STDIN();
				break;
			default:
				fprintf(stderr, "ERROR: command parser ended in unexpected state. This is a bug.\n");
				return false;
		}
	}
}


// TODO: EXTREMELY incomplete, stub
static char *jf_menu_item_get_request_url(const jf_menu_item *item)
{
	if (item != NULL) {
		switch (item->type) {
			// Atoms
			case JF_ITEM_TYPE_AUDIO:
			case JF_ITEM_TYPE_AUDIOBOOK:
				return jf_concat(4, g_options.server, "/items/", item->id, "/file");
			case JF_ITEM_TYPE_EPISODE:
			case JF_ITEM_TYPE_MOVIE:
				return jf_concat(4, "/users/", g_options.userid, "/items/", item->id);
			// Folders
			case JF_ITEM_TYPE_COLLECTION:
			case JF_ITEM_TYPE_USER_VIEW:
			case JF_ITEM_TYPE_FOLDER:
			case JF_ITEM_TYPE_PLAYLIST:
			case JF_ITEM_TYPE_ALBUM:
			case JF_ITEM_TYPE_SEASON:
			case JF_ITEM_TYPE_SERIES:
				return jf_concat(5, "/users/", g_options.userid, "/items?parentid=",
						item->id, "&sortby=isfolder,sortname");
			case JF_ITEM_TYPE_ARTIST:
				return jf_concat(5, "/users/", g_options.userid, "/items?albumartistids=",
						item->id, "&recursive=true&includeitemtypes=musicalbum");
			// Persistent folders
			case JF_ITEM_TYPE_MENU_FAVORITES:
				return jf_concat(3, "/users/", g_options.userid, "/items?filters=isfavorite&recursive=true&sortby=sortname");
			case JF_ITEM_TYPE_MENU_CONTINUE:
				return jf_concat(3, "/users/", g_options.userid, "/items/resume?recursive=true");
			case JF_ITEM_TYPE_MENU_NEXT_UP:
				return jf_concat(3, "/shows/nextup?userid=", g_options.userid, "&limit=15");
			case JF_ITEM_TYPE_MENU_LATEST:
				return jf_concat(3, "/users/", g_options.userid, "/items/latest?isfolder=true&limit=50");
			case JF_ITEM_TYPE_MENU_LIBRARIES:
				return jf_concat(3, "/users/", g_options.userid, "/views");
			default:
				fprintf(stderr, "ERROR: get_request_url was called on an unsupported item_type (%d).\n", item->type);
				return NULL;
		}
	}
	return NULL;
}


static jf_menu_item *jf_menu_child_get(size_t n)
{
	jf_menu_item **child;

	if (s_context == NULL) {
		return NULL;
	}
	if (JF_ITEM_TYPE_HAS_DYNAMIC_CHILDREN(s_context->type)) {
		return jf_thread_buffer_get_parsed_item(n);
	} else {
		child = s_context->children;
		while (--n > 0) {
			if (*(++child) == NULL) {
				return NULL;
			}
		}
		return *child;
	}
}


static jf_item_type jf_menu_child_get_type(size_t n)
{
	jf_menu_item **child;

	if (s_context == NULL) {
		return JF_ITEM_TYPE_NONE;
	}
	if (JF_ITEM_TYPE_HAS_DYNAMIC_CHILDREN(s_context->type)) {
		return jf_thread_buffer_get_parsed_item_type(n);
	} else {
		child = s_context->children;
		while (--n > 0) {
			if (*(++child) == NULL) {
				return JF_ITEM_TYPE_NONE;
			}
		}
		return (*child)->type;
	}
}


bool jf_menu_child_is_folder(const size_t n)
{
	if (s_context == NULL) {
		return false;
	}
	return JF_ITEM_TYPE_IS_FOLDER(jf_menu_child_get_type(n));
}


void jf_menu_child_push(const size_t n)
{
	jf_menu_item *child = jf_menu_child_get(n);
	jf_menu_stack_push(child);
}


size_t jf_menu_child_count()
{
	jf_menu_item **child;
	size_t n = 0;

	if (s_context == NULL) {
		return 0;
	}
	if (JF_ITEM_TYPE_HAS_DYNAMIC_CHILDREN(s_context->type)) {
		return jf_thread_buffer_item_count();
	} else {
		child = s_context->children;
		while (*(child++) != NULL) {
			n++;
		}
		return n;
	}
}


void jf_menu_dotdot()
{
	jf_menu_item *menu_item;
	if ((menu_item = jf_menu_stack_pop()) != NULL) {
		if (menu_item->type == JF_ITEM_TYPE_MENU_ROOT) {
			// root entry should be pushed back to not cause memory leaks due to its children
			jf_menu_stack_push(menu_item);
		} else {
			jf_menu_item_free(menu_item);
		}
	}
}


void jf_menu_quit()
{
	jf_menu_stack_push(jf_menu_item_new(JF_ITEM_TYPE_COMMAND_QUIT, NULL, NULL));
}


jf_menu_ui_status jf_menu_ui()
{
	jf_menu_item **child;
	jf_reply *reply;
	char *request_url;
	size_t i;

	// ACQUIRE ITEM CONTEXT
	// if menu_stack is empty, assume first time run
	// in case of error it's not an unreasonable fallback (though it might cause memory leaks depending on what happened)
	if ((s_context = jf_menu_stack_pop()) == NULL) {
			if ((s_context = jf_menu_make_ui()) == NULL) {
			fprintf(stderr, "FATAL: jf_menu_make_ui() returned NULL.\n");
			return JF_MENU_UI_STATUS_ERROR;
		}
		jf_menu_stack_push(s_context);
		return JF_MENU_UI_STATUS_GO_ON;
	}

	do {
		// dispatch; download more info if necessary
		switch (s_context->type) {
			// AUDIO ATOMS
			case JF_ITEM_TYPE_AUDIO:
			case JF_ITEM_TYPE_AUDIOBOOK:
				JF_MENU_UI_GET_REQUEST_URL_FATAL();
				const char *mpv_command_args[3] = { "loadfile", request_url, NULL };
				mpv_command(g_mpv_ctx, mpv_command_args);
				free(request_url);
				jf_menu_item_free(s_context);
				return JF_MENU_UI_STATUS_PLAYBACK;
			// VIDEO ATOMS
			case JF_ITEM_TYPE_EPISODE:
			case JF_ITEM_TYPE_MOVIE:
				JF_MENU_UI_GET_REQUEST_URL_FATAL();
				JF_MENU_UI_DO_REQUEST_FATAL(JF_REQUEST_IN_MEMORY);
				free(request_url);
				jf_menu_item_free(s_context);
				break;
			// DYNAMIC FOLDERS: fetch children, parser prints entries
			case JF_ITEM_TYPE_COLLECTION:
			case JF_ITEM_TYPE_USER_VIEW:
			case JF_ITEM_TYPE_FOLDER:
			case JF_ITEM_TYPE_PLAYLIST:
			case JF_ITEM_TYPE_MENU_FAVORITES:
			case JF_ITEM_TYPE_MENU_CONTINUE:
			case JF_ITEM_TYPE_MENU_NEXT_UP:
			case JF_ITEM_TYPE_MENU_LATEST:
			case JF_ITEM_TYPE_MENU_LIBRARIES:
				// TODO print title
				printf("\n===== DYNAMIC PROMISCUOUS FOLDER =====\n");
				JF_MENU_UI_PRINT_FOLDER(JF_REQUEST_SAX_PROMISCUOUS);
				break;
			case JF_ITEM_TYPE_ARTIST:
			case JF_ITEM_TYPE_ALBUM:
			case JF_ITEM_TYPE_SEASON:
			case JF_ITEM_TYPE_SERIES:
				// TODO print title
				printf("\n===== DYNAMIC FOLDER =====\n");
				JF_MENU_UI_PRINT_FOLDER(JF_REQUEST_SAX);
				break;
			// PERSISTENT FOLDERS
			case JF_ITEM_TYPE_MENU_ROOT:
				printf("\n===== Server Root =====\n");
				child = s_context->children;
				i = 1;
				while (*child) {
					switch ((*child)->type) {
						case JF_ITEM_TYPE_MENU_FAVORITES:
							printf("D %zu. Favorites\n", i);
							break;
						case JF_ITEM_TYPE_MENU_CONTINUE:
							printf("D %zu. Continue Watching\n", i);
							break;
						case JF_ITEM_TYPE_MENU_NEXT_UP:
							printf("D %zu. Next Up\n", i);
							break;
						case JF_ITEM_TYPE_MENU_LATEST:
							printf("D %zu. Latest Added (!!unsupported!!)\n", i);
							break;
						case JF_ITEM_TYPE_MENU_LIBRARIES:
							printf("D %zu. User Views\n", i);
							break;
						default:
							printf("[!!!] %zu. WARNING: unrecognized menu item. Undefined behaviour if chosen. This is a bug.\n", i);
							break;
					}
					child++;
					i++;
				}
				// push back on stack to allow backtracking
				jf_menu_stack_push(s_context);
				break;
			case JF_ITEM_TYPE_COMMAND_QUIT:
				jf_menu_item_free(s_context);
				return JF_MENU_UI_STATUS_QUIT;
			default:
				fprintf(stderr, "ERROR: jf_menu_ui unsupported menu item type. This is a bug.\n");
				jf_menu_item_free(s_context);
				break;
		}
	} while (jf_menu_read_commands() == false);

	return JF_MENU_UI_STATUS_GO_ON;
}
/////////////////////////////////////////
