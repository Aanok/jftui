#include "menu.h"

////////// COMMAND PARSER //////////
#include "command_parser.c"
////////////////////////////////////


////////// GLOBAL VARIABLES //////////
extern jf_options g_options;
extern jf_global_state g_state;
extern mpv_handle *g_mpv_ctx;
//////////////////////////////////////


////////// STATIC VARIABLES //////////
static jf_menu_item *s_root_menu = &(jf_menu_item){
		JF_ITEM_TYPE_MENU_ROOT,
		(jf_menu_item *[]){
			&(jf_menu_item){
				JF_ITEM_TYPE_MENU_FAVORITES,
				NULL,
				"",
				"Favorites"
			},
			&(jf_menu_item){
				JF_ITEM_TYPE_MENU_CONTINUE,
				NULL,
				"",
				"Continue Watching"
			},
			&(jf_menu_item){
				JF_ITEM_TYPE_MENU_NEXT_UP,
				NULL,
				"",
				"Next Up"
			},
			&(jf_menu_item){
				JF_ITEM_TYPE_MENU_LATEST,
				NULL,
				"",
				"Latest Added"
			},
			&(jf_menu_item){
				JF_ITEM_TYPE_MENU_LIBRARIES,
				NULL,
				"",
				"User Views"
			},
			NULL
		},
		"",
		"Server Root"
	};
static jf_menu_stack s_menu_stack;
static jf_menu_item *s_context = NULL;
static size_t s_playlist_current = 0;
//////////////////////////////////////


////////// STATIC FUNCTIONS //////////

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

// Function: jf_menu_stack_peek
//
// Returns a const pointer to the item on top of the stack without popping it.
// REQUIRES: global menu stack struct initialized.
//
// Returns:
// 	A const pointer to the item on top of the stack or NULL if the stack is empty.
static const jf_menu_item *jf_menu_stack_peek(void);

static jf_menu_item *jf_menu_child_get(size_t n);
static char *jf_menu_item_get_request_url(const jf_menu_item *item);
static bool jf_menu_print_context(void);
static void jf_menu_play_item(const jf_menu_item *item);
static void jf_menu_try_play(void);
//////////////////////////////////////




////////// JF_MENU_STACK //////////
bool jf_menu_stack_init()
{
	linenoiseHistorySetMaxLen(10); // TODO move to better place
	
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


static const jf_menu_item *jf_menu_stack_peek()
{
	const jf_menu_item *retval;

	if (s_menu_stack.used == 0) {
		return NULL;
	}

	retval = s_menu_stack.items[s_menu_stack.used - 1];
	return retval;
}


void jf_menu_stack_clear()
{
	while (s_menu_stack.used > 0) {
		jf_menu_item_free(jf_menu_stack_pop());
	}
}
///////////////////////////////////


////////// USER INTERFACE LOOP //////////

// TODO: EXTREMELY incomplete, stub
static char *jf_menu_item_get_request_url(const jf_menu_item *item)
{
	const jf_menu_item *parent;
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
			case JF_ITEM_TYPE_FOLDER:
			case JF_ITEM_TYPE_ALBUM:
			case JF_ITEM_TYPE_SEASON:
			case JF_ITEM_TYPE_SERIES:
				if ((parent = jf_menu_stack_peek()) != NULL && parent->type == JF_ITEM_TYPE_MENU_LATEST) {
					return jf_concat(5, "/users/", g_options.userid, "/items/latest?parentid=",
							item->id, "&groupitems=false");
				} else {
					return jf_concat(5, "/users/", g_options.userid, "/items?parentid=",
							item->id, "&sortby=isfolder,sortname");
				}
			case JF_ITEM_TYPE_COLLECTION_MUSIC:
				if ((parent = jf_menu_stack_peek()) != NULL && parent->type == JF_ITEM_TYPE_FOLDER) {
					// we are inside a "by folders" view
					return jf_concat(5, "/users/", g_options.userid, "/items?parentid=",
							item->id, "&sortby=isfolder,sortname");
				} else {
					return jf_concat(4, "/artists?parentid=", item->id, "&userid=", g_options.userid);
				}
			case JF_ITEM_TYPE_COLLECTION_SERIES:
				return jf_concat(5, "/users/", g_options.userid, "/items?parentid=",
						item->id, "&includeitemtypes=series&recursive=true&sortby=isfolder,sortname");
			case JF_ITEM_TYPE_COLLECTION_MOVIES:
				return jf_concat(5, "/users/", g_options.userid, "/items?parentid=",
						item->id, "&includeitemtypes=Movie&recursive=true&sortby=isfolder,sortname");
			case JF_ITEM_TYPE_ARTIST:
				return jf_concat(5, "/users/", g_options.userid, "/items?albumartistids=",
						item->id, "&recursive=true&includeitemtypes=musicalbum&sortby=isfolder,sortname&sortorder=ascending");
			// Persistent folders
			case JF_ITEM_TYPE_MENU_FAVORITES:
				return jf_concat(3, "/users/", g_options.userid, "/items?filters=isfavorite&recursive=true&sortby=sortname");
			case JF_ITEM_TYPE_MENU_CONTINUE:
				return jf_concat(3, "/users/", g_options.userid, "/items/resume?recursive=true");
			case JF_ITEM_TYPE_MENU_NEXT_UP:
				return jf_concat(3, "/shows/nextup?userid=", g_options.userid, "&limit=15");
			case JF_ITEM_TYPE_MENU_LATEST:
				// TODO figure out what fresh insanity drives the limit amount in this case
				return jf_concat(3, "/users/", g_options.userid, "/items/latest?limit=115");
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


static bool jf_menu_print_context()
{
	jf_menu_item **child;
	size_t i;

	if (s_context == NULL) {
		fprintf(stderr, "ERROR: jf_menu_print_context found NULL menu context. This is a bug.\n");
		return false;
	}

	if (! JF_ITEM_TYPE_IS_FOLDER(s_context->type)) {
		fprintf(stderr, "ERROR: jf_menu_print_context found non-folder menu context. This is a bug.\n");
		return false;
	}

	switch (s_context->type) {
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
			JF_MENU_PRINT_TITLE(s_context->name);
			JF_MENU_PRINT_FOLDER_FATAL(s_context, JF_REQUEST_SAX_PROMISCUOUS);
			break;
		case JF_ITEM_TYPE_COLLECTION_MUSIC:
		case JF_ITEM_TYPE_COLLECTION_SERIES:
		case JF_ITEM_TYPE_COLLECTION_MOVIES:
		case JF_ITEM_TYPE_ARTIST:
		case JF_ITEM_TYPE_ALBUM:
		case JF_ITEM_TYPE_SEASON:
		case JF_ITEM_TYPE_SERIES:
			JF_MENU_PRINT_TITLE(s_context->name);
			JF_MENU_PRINT_FOLDER_FATAL(s_context, JF_REQUEST_SAX);
			break;
		// PERSISTENT FOLDERS
		case JF_ITEM_TYPE_MENU_ROOT:
			JF_MENU_PRINT_TITLE(s_context->name);
			child = s_context->children;
			i = 1;
			while (*child) {
				printf("D %zu. %s\n", i, (*child)->name);
				child++;
				i++;
			}
			// push on stack to allow backtracking
			jf_menu_stack_push(s_context);
			break;
		default:
			fprintf(stderr, "ERROR: jf_menu_dispatch_context unsupported menu item type. This is a bug.\n");
			jf_menu_item_free(s_context);
			return false;
	}

	return true;
}


static void jf_menu_play_item(const jf_menu_item *item)
{
	char *request_url;

	if (item == NULL) {
		return;
	}

	if (JF_ITEM_TYPE_IS_FOLDER(item->type)) {
		fprintf(stderr, "ERROR: jf_menu_play_item invoked on folder item type. This is a bug.\n");
		return;
	}

	switch (item->type) {
		case JF_ITEM_TYPE_AUDIO:
		case JF_ITEM_TYPE_AUDIOBOOK:
			if ((request_url = jf_menu_item_get_request_url(item)) == NULL) {
				fprintf(stderr, "ERROR: jf_menu_play_item could not get request url for item %s\n", item->name);
				return;
			}
			const char *mpv_args[] = { "loadfile", request_url, NULL };
			mpv_command(g_mpv_ctx, mpv_args); 
			free(request_url);
			break;
		case JF_ITEM_TYPE_EPISODE:
		case JF_ITEM_TYPE_MOVIE:
			printf("DEBUG: jf_menu_play_item video types not yet supported.\n");
			break;
		default:
			fprintf(stderr, "ERROR: jf_menu_play_item unsupported item type (%d). This is a bug.\n",
					item->type);
			break;
	}
}


static void jf_menu_try_play()
{
	jf_menu_item *item;

	if (jf_disk_playlist_count() > 0) {
		g_state.state = JF_STATE_PLAYBACK;
		s_playlist_current = 1;
		item = jf_disk_playlist_get(1);
		jf_menu_play_item(item);
		jf_menu_item_free(item);
	}
}


jf_item_type jf_menu_child_get_type(size_t n)
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


bool jf_menu_child_dispatch(size_t n)
{
	jf_menu_item *child = jf_menu_child_get(n);

	if (child == NULL) {
		return true;
	}

	switch (child->type) {
		// ATOMS: add to playlist
		case JF_ITEM_TYPE_AUDIO:
		case JF_ITEM_TYPE_AUDIOBOOK:
		case JF_ITEM_TYPE_EPISODE:
		case JF_ITEM_TYPE_MOVIE:
			jf_disk_playlist_add(child);
			jf_menu_item_free(child);
			break;
		// FOLDERS: push on stack
		case JF_ITEM_TYPE_COLLECTION:
		case JF_ITEM_TYPE_USER_VIEW:
		case JF_ITEM_TYPE_FOLDER:
		case JF_ITEM_TYPE_PLAYLIST:
		case JF_ITEM_TYPE_MENU_FAVORITES:
		case JF_ITEM_TYPE_MENU_CONTINUE:
		case JF_ITEM_TYPE_MENU_NEXT_UP:
		case JF_ITEM_TYPE_MENU_LATEST:
		case JF_ITEM_TYPE_MENU_LIBRARIES:
		case JF_ITEM_TYPE_COLLECTION_MUSIC:
		case JF_ITEM_TYPE_COLLECTION_SERIES:
		case JF_ITEM_TYPE_COLLECTION_MOVIES:
		case JF_ITEM_TYPE_ARTIST:
		case JF_ITEM_TYPE_ALBUM:
		case JF_ITEM_TYPE_SEASON:
		case JF_ITEM_TYPE_SERIES:
			jf_menu_stack_push(child);
			break;
		default:
			fprintf(stderr, "ERROR: jf_menu_child_dispatch unsupported menu item type. This is a bug.\n");
			jf_menu_item_free(child);
			break;
	}

	return true;
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
	g_state.state = JF_STATE_USER_QUIT;
}


bool jf_menu_playlist_forward()
{
	jf_menu_item *item;

	if (s_playlist_current < jf_disk_playlist_count()) {
		item = jf_disk_playlist_get(++s_playlist_current);
		jf_menu_play_item(item);
		jf_menu_item_free(item);
		return true;
	} else {
		return false;
	}
}


bool jf_menu_playlist_backward()
{
	jf_menu_item *item;

	if (s_playlist_current > 1) {
		item = jf_disk_playlist_get(--s_playlist_current);
		jf_menu_play_item(item);
		jf_menu_item_free(item);
		return true;
	} else {
		return false;
	}
}


void jf_menu_ui()
{
	yycontext yy;
	char *line = NULL;

	// ACQUIRE ITEM CONTEXT
	if ((s_context = jf_menu_stack_pop()) == NULL) {
		// expected on first run
		// in case of error it's a solid fallback
		s_context = s_root_menu;
	}

	while (true) {
		// PRINT MENU
		if (! jf_menu_print_context()) {
			return;
		}
		// READ AND PROCESS USER COMMAND
		memset(&yy, 0, sizeof(yycontext));
		while (true) {
			switch (yy_command_parser_get_state(&yy)) {
				case JF_CMD_VALIDATE_START:
					// read input and do first pass (validation)
					line = linenoise("> ");
					yy.input = line;
					yyparse(&yy);
					break;
				case JF_CMD_VALIDATE_OK:
					// reset parser but preserve state and input for second pass (dispatch)
					yyrelease(&yy);
					memset(&yy, 0, sizeof(yycontext));
					yy.state = JF_CMD_VALIDATE_OK;
					yy.input = line;
					yyparse(&yy);
					break;
				case JF_CMD_SUCCESS:
					linenoiseHistoryAdd(line);
					free(line);
					yyrelease(&yy);
					jf_menu_try_play();
					return;
				case JF_CMD_FAIL_FOLDER:
					fprintf(stderr, "ERROR: cannot open many folders or both folders and items with non-recursive command.\n");
					free(line);
					yyrelease(&yy);
					memset(&yy, 0, sizeof(yycontext));
					break;
				case JF_CMD_FAIL_SYNTAX:
					fprintf(stderr, "ERROR: malformed command.\n");
					free(line);
					yyrelease(&yy);
					memset(&yy, 0, sizeof(yycontext));
					break;
				case JF_CMD_FAIL_DISPATCH:
					// exit silently
					free(line);
					yyrelease(&yy);
					return;
				default:
					fprintf(stderr, "ERROR: command parser ended in unexpected state. This is a bug.\n");
					free(line);
					yyrelease(&yy);
					memset(&yy, 0, sizeof(yycontext));
					break;
			}
		}
	}
}
/////////////////////////////////////////
