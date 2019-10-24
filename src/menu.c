#include "menu.h"

////////// COMMAND PARSER //////////
#include "cmd.c"
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
				0,
				"",
				"Favorites",
				0, 0
			},
			&(jf_menu_item){
				JF_ITEM_TYPE_MENU_CONTINUE,
				NULL,
				0,
				"",
				"Continue Watching",
				0, 0
			},
			&(jf_menu_item){
				JF_ITEM_TYPE_MENU_NEXT_UP,
				NULL,
				0,
				"",
				"Next Up",
				0, 0
			},
			&(jf_menu_item){
				JF_ITEM_TYPE_MENU_LATEST_UNPLAYED,
				NULL,
				0,
				"",
				"Latest Unplayed",
				0, 0
			},
			&(jf_menu_item){
				JF_ITEM_TYPE_MENU_LIBRARIES,
				NULL,
				0,
				"",
				"User Views",
				0, 0
			}
		},
		5,
		"",
		"",
		0, 0
	};
static jf_menu_stack s_menu_stack = (jf_menu_stack){ 0 };
static jf_menu_item *s_context = NULL;
static size_t s_playlist_current = 0;
//////////////////////////////////////


////////// STATIC FUNCTIONS //////////
// Pushes a jf_menu_item on the global menu stack. No-op if NULL is passed.
//
// Parameters:
// 	menu_item - A pointer to the item to be pushed (if NULL, no-op).
// CAN FATAL.
static JF_FORCE_INLINE void jf_menu_stack_push(jf_menu_item *menu_item);


// Pops the top item out of the global menu stack.
// The caller assumes ownership of the popped item (i.e. will have to free it).
//
// Returns:
// 	A pointer to the item popped or NULL if the stack is empty.
// CAN'T FAIL.
static JF_FORCE_INLINE jf_menu_item *jf_menu_stack_pop(void);


// Returns a const pointer to the item on top of the stack without popping it.
//
// Returns:
// 	A const pointer to the item on top of the stack or NULL if the stack is empty.
// CAN'T FAIL.
static JF_FORCE_INLINE const jf_menu_item *jf_menu_stack_peek(void);


static jf_menu_item *jf_menu_child_get(size_t n);
static char *jf_menu_item_get_request_url(const jf_menu_item *item);
static bool jf_menu_print_context(void);
static void jf_menu_play_video(const jf_menu_item *item);
static void jf_menu_ask_resume_yn(const jf_menu_item *item, const long long ticks);
static inline void jf_menu_populate_video_ticks(jf_menu_item *item);
static void jf_menu_ask_resume(jf_menu_item *item);
static void jf_menu_play_item(jf_menu_item *item);
static void jf_menu_try_play(void);
//////////////////////////////////////


////////// JF_MENU_STACK //////////
static JF_FORCE_INLINE void jf_menu_stack_push(jf_menu_item *menu_item)
{
	if (menu_item == NULL) {
		return;
	}

	assert(s_menu_stack.items != NULL);
	if (s_menu_stack.size == s_menu_stack.used) {
		s_menu_stack.size *= 2;
		assert((s_menu_stack.items = realloc(s_menu_stack.items,
						s_menu_stack.size * sizeof(jf_menu_item *))) != NULL);
	}
	s_menu_stack.items[s_menu_stack.used++] = menu_item;
}


static JF_FORCE_INLINE jf_menu_item *jf_menu_stack_pop()
{
	jf_menu_item *retval;

	if (s_menu_stack.used == 0) {
		return NULL;
	}

	retval = s_menu_stack.items[--s_menu_stack.used];
	s_menu_stack.items[s_menu_stack.used] = NULL;
	return retval;
}


static JF_FORCE_INLINE const jf_menu_item *jf_menu_stack_peek()
{
	return s_menu_stack.used == 0 ? NULL
		: s_menu_stack.items[s_menu_stack.used - 1];
}
///////////////////////////////////


////////// USER INTERFACE LOOP //////////

static char *jf_menu_item_get_request_url(const jf_menu_item *item)
{
	const jf_menu_item *parent;

	if (item == NULL) {
		return NULL;
	}

	switch (item->type) {
		// Atoms
		case JF_ITEM_TYPE_AUDIO:
		case JF_ITEM_TYPE_AUDIOBOOK:
		case JF_ITEM_TYPE_VIDEO_SOURCE:
			return jf_concat(4, g_options.server, "/items/", item->id, "/file");
		case JF_ITEM_TYPE_EPISODE:
		case JF_ITEM_TYPE_MOVIE:
			return jf_concat(4, "/users/", g_options.userid, "/items/", item->id);
		case JF_ITEM_TYPE_VIDEO_SUB:
			return strdup(item->name);
		// Folders
		case JF_ITEM_TYPE_COLLECTION:
		case JF_ITEM_TYPE_FOLDER:
		case JF_ITEM_TYPE_ALBUM:
		case JF_ITEM_TYPE_SEASON:
		case JF_ITEM_TYPE_SERIES:
			if ((parent = jf_menu_stack_peek()) != NULL && parent->type == JF_ITEM_TYPE_MENU_LATEST_UNPLAYED) {
				return jf_concat(4,
						"/users/",
						g_options.userid,
						"/items/latest?groupitems=false&parentid=",
						item->id);
			} else {
				return jf_concat(4,
						"/users/",
						g_options.userid,
						"/items?sortby=isfolder,parentindexnumber,indexnumber,productionyear,sortname&parentid=",
						item->id);
			}
		case JF_ITEM_TYPE_COLLECTION_MUSIC:
			if ((parent = jf_menu_stack_peek()) != NULL && parent->type == JF_ITEM_TYPE_FOLDER) {
				// we are inside a "by folders" view
				return jf_concat(4,
						"/users/",
						g_options.userid,
						"/items?sortby=isfolder,sortname&parentid=",
						item->id);
			} else {
				return jf_concat(4,
						"/artists?parentid=",
						item->id,
						"&userid=",
						g_options.userid);
			}
		case JF_ITEM_TYPE_COLLECTION_SERIES:
			return jf_concat(4,
					"/users/",
					g_options.userid,
					"/items?includeitemtypes=series&recursive=true&sortby=isfolder,sortname&parentid=",
					item->id);
		case JF_ITEM_TYPE_COLLECTION_MOVIES:
			return jf_concat(4,
					"/users/",
					g_options.userid,
					"/items?includeitemtypes=Movie&recursive=true&sortby=isfolder,sortname&parentid=",
					item->id);
		case JF_ITEM_TYPE_ARTIST:
			return jf_concat(4,
					"/users/",
					g_options.userid,
					"/items?recursive=true&includeitemtypes=musicalbum&sortby=isfolder,productionyear,sortname&sortorder=ascending&albumartistids=",
					item->id);
		case JF_ITEM_TYPE_SEARCH_RESULT:
			return jf_concat(4,
					"/users/",
					g_options.userid,
					"/items?recursive=true&searchterm=",
					item->name);
		// Persistent folders
		case JF_ITEM_TYPE_MENU_FAVORITES:
			return jf_concat(3,
					"/users/",
					g_options.userid,
					"/items?filters=isfavorite&recursive=true&sortby=sortname");
		case JF_ITEM_TYPE_MENU_CONTINUE:
			return jf_concat(3,
					"/users/",
					g_options.userid,
					"/items/resume?recursive=true");
		case JF_ITEM_TYPE_MENU_NEXT_UP:
			return jf_concat(3, "/shows/nextup?userid=", g_options.userid, "&limit=15");
		case JF_ITEM_TYPE_MENU_LATEST_UNPLAYED:
			// TODO figure out what fresh insanity drives the limit amount in this case
			return jf_concat(3, "/users/", g_options.userid, "/items/latest?limit=32");
		case JF_ITEM_TYPE_MENU_LIBRARIES:
			return jf_concat(3, "/users/", g_options.userid, "/views");
		default:
			fprintf(stderr,
					"Error: get_request_url was called on an unsupported item_type (%d). This is a bug.\n",
					item->type);
			return NULL;
	}
}


static jf_menu_item *jf_menu_child_get(size_t n)
{
	if (s_context == NULL) return NULL;

	if (JF_ITEM_TYPE_HAS_DYNAMIC_CHILDREN(s_context->type)) {
		return jf_disk_payload_get_item(n);
	} else {
		return n - 1 <= s_context->children_count ? s_context->children[n - 1]
			: NULL;
	}
}


static bool jf_menu_print_context()
{
	size_t i;
	jf_request_type request_type = JF_REQUEST_SAX;
	jf_reply *reply;
	char *request_url;

	if (s_context == NULL) {
		fprintf(stderr, "Error: jf_menu_print_context: s_context == NULL. This is a bug.\n");
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
		case JF_ITEM_TYPE_MENU_LATEST_UNPLAYED:
		case JF_ITEM_TYPE_MENU_LIBRARIES:
		case JF_ITEM_TYPE_SEARCH_RESULT:
			request_type = JF_REQUEST_SAX_PROMISCUOUS;
			// no break
		case JF_ITEM_TYPE_COLLECTION_MUSIC:
		case JF_ITEM_TYPE_COLLECTION_SERIES:
		case JF_ITEM_TYPE_COLLECTION_MOVIES:
		case JF_ITEM_TYPE_ARTIST:
		case JF_ITEM_TYPE_ALBUM:
		case JF_ITEM_TYPE_SEASON:
		case JF_ITEM_TYPE_SERIES:
			printf("\n===== %s =====\n", s_context->name);
			if ((request_url = jf_menu_item_get_request_url(s_context)) == NULL) {
				jf_menu_item_free(s_context);
				return false;
			}
#ifdef JF_DEBUG
			printf("DEBUG: request_url = %s\n", request_url);
#endif
			reply = jf_net_request(request_url, request_type, JF_HTTP_GET, NULL);
			free(request_url);
			if (JF_REPLY_PTR_HAS_ERROR(reply)) {
				jf_menu_item_free(s_context);
				fprintf(stderr, "Error: %s.\n", jf_reply_error_string(reply));
				jf_reply_free(reply);
				jf_thread_buffer_clear_error();
				return false;
			}
			jf_reply_free(reply);
			jf_menu_stack_push(s_context);
			break;
		// PERSISTENT FOLDERS
		case JF_ITEM_TYPE_MENU_ROOT:
			printf("\n===== %s =====\n", s_context->name);
			for (i = 0; i < s_context->children_count; i++) {
				printf("D %zu. %s\n", i + 1, s_context->children[i]->name);
			}
			// push on stack to allow backtracking
			jf_menu_stack_push(s_context);
			break;
		default:
			fprintf(stderr,
					"Error: jf_menu_dispatch_context unsupported menu item type (%d). This is a bug.\n",
					s_context->type);
			jf_menu_item_free(s_context);
			return false;
	}

	return true;
}


static void jf_menu_ask_resume_yn(const jf_menu_item *item, const long long ticks)
{
	char *timestamp, *question;

	if (ticks == 0) return;
	timestamp = jf_make_timestamp(ticks);
	question = jf_concat(5,
					"\nWould you like to resume ",
					item->name,
					" at the ",
					timestamp,
					" mark?");
	if (jf_menu_user_ask_yn(question)) {
		JF_MPV_ASSERT(mpv_set_property_string(g_mpv_ctx, "start", timestamp));
		g_state.state = JF_STATE_PLAYBACK_START_MARK;
	}
	free(timestamp);
	free(question);
}


static inline void jf_menu_populate_video_ticks(jf_menu_item *item)
{
	jf_reply **replies;
	char *tmp;
	size_t i;

	if (item == NULL) return;
	if (item->type != JF_ITEM_TYPE_EPISODE
			&& item->type != JF_ITEM_TYPE_MOVIE) return;

	// the Emby interface was designed by a drunk gibbon. to check for
	// a progress marker, we have to request the items corresponding to
	// the additionalparts and look at them individually
	// ...and each may have its own bookmark!

	// parent and first child refer the same ID, thus the same part
	item->children[0]->playback_ticks = item->playback_ticks;
	// but at this point it makes no sense for the parent item to have a PB
	// tick since there may be multiple markers
	item->playback_ticks = 0;

	// now go and get all markers for all parts
	assert((replies = malloc((item->children_count - 1) * sizeof(jf_menu_item *))) != NULL);
	for (i = 1; i < item->children_count; i++) {
		tmp = jf_concat(4,
				"/users/",
				g_options.userid,
				"/items/",
				item->children[i]->id);
		replies[i - 1] = jf_net_request(tmp,
				JF_REQUEST_ASYNC_IN_MEMORY,
				JF_HTTP_GET,
				NULL);
		free(tmp);
	}
	for (i = 1; i < item->children_count; i++) {
		jf_net_await(replies[i - 1]);
		jf_json_parse_playback_ticks(item->children[i], replies[i - 1]->payload);
		jf_reply_free(replies[i - 1]);
	}
	free(replies);
}


static void jf_menu_ask_resume(jf_menu_item *item)
{
	char **timestamps;
	long long ticks;
	size_t i, j, markers_count;

	switch (item->type) {
		case JF_ITEM_TYPE_AUDIO:
		case JF_ITEM_TYPE_AUDIOBOOK:
			if (item->playback_ticks != 0) {
				jf_menu_ask_resume_yn(item, item->playback_ticks);
			}
			break;
		case JF_ITEM_TYPE_EPISODE:
		case JF_ITEM_TYPE_MOVIE:
			markers_count = 0;
			for (i = 0; i < item->children_count; i++) {
				if (item->children[i]->playback_ticks != 0) {
					markers_count++;
				}
			}
			if (markers_count == 0) break;
			if (markers_count == 1) {
				i = 0;
				ticks = 0;
				while (item->children[i]->playback_ticks == 0
						&& i < item->children_count - 1) {
					ticks += item->children[i]->runtime_ticks;
					i++;
				}
				ticks += item->children[i]->playback_ticks;
				jf_menu_ask_resume_yn(item, ticks);
				break;
			}
			assert((timestamps = malloc(markers_count * sizeof(char *))) != NULL);
			ticks = 0;
			j = 2;
			printf("\n%s is a split-file on the server and there is progress marked on more than one part.\n",
					item->name);
			printf("Please choose at what time you'd like to resume watching:\n");
			printf("1. 00:00:00 (start)\n");
			for (i = 0; i < item->children_count; i++) {
				if (item->children[i]->playback_ticks != 0) {
					ticks += item->children[i]->playback_ticks;
					timestamps[j - 2] = jf_make_timestamp(ticks);
					printf("%zu. %s\n", j, timestamps[j - 2]);
					ticks += item->children[i]->runtime_ticks - item->children[i]->playback_ticks;
					j++;
				} else {
					ticks += item->children[i]->runtime_ticks;
				}
			}
			j = jf_menu_user_ask_selection(1, markers_count + 1);
			if (j != 1){
				JF_MPV_ASSERT(mpv_set_property_string(g_mpv_ctx, "start", timestamps[j - 2]));
			}
			g_state.state = JF_STATE_PLAYBACK_START_MARK;
			for (i = 0; i < markers_count; i++) {
				free(timestamps[i]);
			}
			free(timestamps);
			break;
		default:
			break;
	}
}


static void jf_menu_play_video(const jf_menu_item *item)
{
	jf_growing_buffer *filename;
	char *tmp;
	size_t i, j;
    int next_sub;

	jf_menu_item_print(item);
	// merge video files
	JF_MPV_ASSERT(mpv_set_property_string(g_mpv_ctx, "title", item->name));
	filename = jf_growing_buffer_new(128);
	jf_growing_buffer_append(filename, "edl://", JF_STATIC_STRLEN("edl://"));
	for (i = 0; i < item->children_count; i++) {
		if (item->children[i]->type != JF_ITEM_TYPE_VIDEO_SOURCE) {
			fprintf(stderr,
					"Warning: unrecognized item type (%s) for %s part %zu. This is a bug.\n",
					jf_item_type_get_name(item->children[i]->type), item->name, i);
			continue;
		}
		jf_growing_buffer_append(filename,
				jf_menu_item_get_request_url(item->children[i]),
				0);
		jf_growing_buffer_append(filename, ";", 1);
	}
	jf_growing_buffer_append(filename, "", 1);
	const char *loadfile[] = { "loadfile", filename->buf, NULL };
	JF_MPV_ASSERT(mpv_command(g_mpv_ctx, loadfile));
    printf("DEBUG: playing file %s\n", filename->buf);
	jf_growing_buffer_free(filename);

	// external subtitles
	// note: they unfortunately require loadfile to already have been issued
	for (i = 0; i < item->children_count; i++) {
		for (j = 0; j < item->children[i]->children_count; j++) {
			if (item->children[i]->children[j]->type != JF_ITEM_TYPE_VIDEO_SUB) {
				fprintf(stderr,
						"Warning: unrecognized item type (%s) for %s, part %zu, child %zu. This is a bug.\n",
						jf_item_type_get_name(item->children[i]->children[j]->type),
						item->name,
						i,
						j);
				continue;
			}
			tmp = jf_concat(2, g_options.server, item->children[i]->children[j]->name);
			printf("DEBUG: adding sub %s\n", tmp);
			const char *command[] = { "sub-add", tmp, "auto", NULL };
			JF_MPV_ASSERT(mpv_command(g_mpv_ctx, command));
		}
	}
}


static void jf_menu_play_item(jf_menu_item *item)
{
	char *request_url;
	jf_reply *replies[2];

	if (item == NULL) {
		return;
	}

	if (JF_ITEM_TYPE_IS_FOLDER(item->type)) {
		fprintf(stderr, "Error: jf_menu_play_item invoked on folder item type. This is a bug.\n");
		return;
	}

	switch (item->type) {
		case JF_ITEM_TYPE_AUDIO:
		case JF_ITEM_TYPE_AUDIOBOOK:
			if ((request_url = jf_menu_item_get_request_url(item)) == NULL) {
				return;
			}
			jf_menu_ask_resume(item);
			JF_MPV_ASSERT(mpv_set_property_string(g_mpv_ctx, "title", item->name));
			const char *loadfile[] = { "loadfile", request_url, NULL };
			mpv_command(g_mpv_ctx, loadfile); 
			jf_menu_item_free(g_state.now_playing);
			g_state.now_playing = item;
			free(request_url);
			break;
		case JF_ITEM_TYPE_EPISODE:
		case JF_ITEM_TYPE_MOVIE:
			// check if item was already evaded re: split file and versions
			if (item->children_count > 0) {
				jf_menu_ask_resume(item);
				jf_menu_play_video(item);
			} else {
				request_url = jf_menu_item_get_request_url(item);
				replies[0] = jf_net_request(request_url,
						JF_REQUEST_ASYNC_IN_MEMORY,
						JF_HTTP_GET,
						NULL);
				free(request_url);
				request_url = jf_concat(3, "/videos/", item->id, "/additionalparts");
				replies[1] = jf_net_request(request_url,
						JF_REQUEST_IN_MEMORY,
						JF_HTTP_GET,
						NULL);
				free(request_url);
				if (JF_REPLY_PTR_HAS_ERROR(replies[1])) {
					fprintf(stderr,
							"Error: network request for /additionalparts of item %s failed: %s.\n",
							item->name,
							jf_reply_error_string(replies[1]));
					jf_reply_free(replies[1]);
					jf_reply_free(jf_net_await(replies[0]));
					return;
				}
				if (JF_REPLY_PTR_HAS_ERROR(jf_net_await(replies[0]))) {
					fprintf(stderr,
							"Error: network request for item %s failed: %s.\n",
							item->name,
							jf_reply_error_string(replies[0]));
					jf_reply_free(replies[0]);
					jf_reply_free(replies[1]);
					return;
				}
				jf_json_parse_video(item, replies[0]->payload, replies[1]->payload);
				jf_reply_free(replies[0]);
				jf_reply_free(replies[1]);
				jf_menu_populate_video_ticks(item);
				jf_menu_ask_resume(item);
				jf_menu_play_video(item);
				jf_disk_playlist_replace_item(s_playlist_current, item);
				jf_menu_item_free(g_state.now_playing);
				g_state.now_playing = item;
			}
			break;
		default:
			fprintf(stderr,
					"Error: jf_menu_play_item unsupported type (%s). This is a bug.\n",
					jf_item_type_get_name(item->type));
			break;
	}
}


static void jf_menu_try_play()
{
	jf_menu_item *item;

	if (jf_disk_playlist_item_count() > 0) {
		g_state.state = JF_STATE_PLAYBACK;
		s_playlist_current = 1;
		item = jf_disk_playlist_get_item(1);
		jf_menu_play_item(item);
	}
}


jf_item_type jf_menu_child_get_type(size_t n)
{
	if (s_context == NULL) return JF_ITEM_TYPE_NONE;

	if (JF_ITEM_TYPE_HAS_DYNAMIC_CHILDREN(s_context->type)) {
		return jf_disk_payload_get_type(n);
	} else {
		return n - 1 <= s_context->children_count ?
			s_context->children[n - 1]->type : JF_ITEM_TYPE_NONE;
	}
}


bool jf_menu_child_dispatch(size_t n)
{
	jf_menu_item *child = jf_menu_child_get(n);

	if (child == NULL) return true;

	switch (child->type) {
		// ATOMS: add to playlist
		case JF_ITEM_TYPE_AUDIO:
		case JF_ITEM_TYPE_AUDIOBOOK:
		case JF_ITEM_TYPE_EPISODE:
		case JF_ITEM_TYPE_MOVIE:
			jf_disk_playlist_add_item(child);
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
		case JF_ITEM_TYPE_MENU_LATEST_UNPLAYED:
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
			fprintf(stderr,
					"Error: jf_menu_child_dispatch unsupported menu item type (%d) for item %zu. This is a bug.\n",
					child->type,
					n);
			jf_menu_item_free(child);
			return false;
	}

	return true;
}


size_t jf_menu_child_count()
{
	if (s_context == NULL) return 0;

	if (JF_ITEM_TYPE_HAS_DYNAMIC_CHILDREN(s_context->type)) {
		return jf_disk_payload_item_count();
	} else {
		return s_context->children_count;
	}
}


void jf_menu_dotdot()
{
	jf_menu_item *menu_item = jf_menu_stack_pop();

	if (menu_item == NULL) return;

	if (menu_item->type == JF_ITEM_TYPE_MENU_ROOT) {
		// root entry should be pushed back to not cause memory leaks due to its children
		jf_menu_stack_push(menu_item);
	} else {
		jf_menu_item_free(menu_item);
	}
}


void jf_menu_quit()
{
	g_state.state = JF_STATE_USER_QUIT;
}


void jf_menu_search(const char *s)
{
	jf_menu_item *menu_item;
	char *escaped;

	escaped = jf_net_urlencode(s);
	menu_item = jf_menu_item_new(JF_ITEM_TYPE_SEARCH_RESULT, NULL, NULL, escaped, 0, 0);
	free(escaped);
	jf_menu_stack_push(menu_item);
}


void jf_menu_mark_played(const jf_menu_item *item)
{
	char *url;
	url = jf_concat(4, "/users/", g_options.userid, "/playeditems/", item->id);
	jf_net_request(url, JF_REQUEST_ASYNC_DETACH, JF_HTTP_POST, NULL);
	free(url);
}


void jf_menu_mark_unplayed(const jf_menu_item *item)
{
	char *url;
	url = jf_concat(4, "/users/", g_options.userid, "/playeditems/", item->id);
	jf_net_request(url, JF_REQUEST_ASYNC_DETACH, JF_HTTP_DELETE, NULL);
	free(url);
}


bool jf_menu_playlist_forward()
{
	if (s_playlist_current < jf_disk_playlist_item_count()) {
		jf_menu_play_item(jf_disk_playlist_get_item(++s_playlist_current));
		return true;
	} else {
		return false;
	}
}


bool jf_menu_playlist_backward()
{
	if (s_playlist_current > 1) {
		jf_menu_play_item(jf_disk_playlist_get_item(--s_playlist_current));
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
		// CLEAR DISK CACHE
		jf_disk_refresh();

		// PRINT MENU
		if (! jf_menu_print_context()) {
			return;
		}
		// READ AND PROCESS USER COMMAND
		memset(&yy, 0, sizeof(yycontext));
		while (true) {
			switch (yy_cmd_get_parser_state(&yy)) {
				case JF_CMD_VALIDATE_START:
					// read input and do first pass (validation)
					line = jf_menu_linenoise("> ");
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
					fprintf(stderr, "Error: cannot open many folders or both folders and items with non-recursive command.\n");
					free(line);
					yyrelease(&yy);
					memset(&yy, 0, sizeof(yycontext));
					break;
				case JF_CMD_FAIL_SYNTAX:
					fprintf(stderr, "Error: malformed command.\n");
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
					fprintf(stderr, "Error: command parser ended in unexpected state. This is a bug.\n");
					free(line);
					yyrelease(&yy);
					memset(&yy, 0, sizeof(yycontext));
					break;
			}
		}
	}
}
/////////////////////////////////////////


////////// MISCELLANEOUS //////////
void jf_menu_init()
{
	// all linenoise setup
	linenoiseHistorySetMaxLen(10);
	
	// update server name
	s_root_menu->name = g_state.server_name;

	// init menu stack
	assert((s_menu_stack.items = malloc(10 * sizeof(jf_menu_item *))) != NULL);
	s_menu_stack.size = 10;
	s_menu_stack.used = 0;
}


void jf_menu_clear()
{
	// clear menu stack
	while (s_menu_stack.used > 0) {
		jf_menu_item_free(jf_menu_stack_pop());
	}
}


char *jf_menu_linenoise(const char *prompt)
{
	char *str;
	if ((str = linenoise(prompt)) == NULL) {
		if (errno != EAGAIN) {
			perror("FATAL: jf_menu_linenoise");
		}
		jf_exit(JF_EXIT_FAILURE);
	}
	return str;
}


bool jf_menu_user_ask_yn(const char *question)
{
	char *str;

	printf("%s [y/n]\n", question);
	while (true) {
		str = jf_menu_linenoise("> ");
		if (strcasecmp(str, "y") == 0) return true;
		if (strcasecmp(str, "n") == 0) return false;
		printf("Error: please answer \"y\" or \"n\".\n");
	}
}


size_t jf_menu_user_ask_selection(const size_t l, const size_t r)
{
	char *tmp;
	size_t i;

	// read the number, kronk
	while (true) {
		tmp = jf_menu_linenoise("> ");
		if (sscanf(tmp, " %zu ", &i) == 1 && l <= i && i <= r) {
			free(tmp);
			return i;
		}
		// wrong numbeeeeeer...
		fprintf(stderr, "Error: please choose exactly one listed item.\n");
	}
}
///////////////////////////////////
