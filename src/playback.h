#ifndef _JF_PLAYBACK
#define _JF_PLAYBACK


#include "shared.h"


#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>


// Update playback progress marker of the currently playing item on the server
// (as of g_state.now_playing).
// Detect if we moved across split-file parts since the last such update and
// mark parts previous to current as played, next to current as unplayed (so
// that the item only has one overall progress marker on the server).
//
// Parameters:
//  - playback_ticks: current position in Jellyfin ticks, referring to the
//    whole merged file in case of split-part.
//
// jf_playback_update_progress will POST to /sessions/playing/progress
// and should thus be called for ongoing playback.
//
// jf_playback_update_stopped will POST to /sessions/playing/stopped
// and should thus be called for playback that just ended.
void jf_playback_update_progress(const int64_t playback_ticks);
void jf_playback_update_stopped(const int64_t playback_ticks);


void jf_playback_load_external_subtitles(void);
void jf_playback_align_subtitle(const int64_t sid);


void jf_playback_play_item(jf_menu_item *item);
void jf_playback_play_video(const jf_menu_item *item);
bool jf_playback_next(void);
bool jf_playback_previous(void);
void jf_playback_end(void);
// won't move item currently playing
void jf_playback_shuffle_playlist(void);

// Will print part or the entirety of the current jftui playback playlist to
// stdout.
//
//  - slice_height: number of items before AND after to try to print.
//      If 0 will print whole playlist.
//
// CAN'T FAIL.
void jf_playback_print_playlist(size_t slice_height);
#endif
