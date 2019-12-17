#ifndef _JF_PLAYBACK
#define _JF_PLAYBACK

#include "shared.h"
#include "json.h"
#include "net.h"
#include <mpv/client.h>
#include "menu.h"


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


inline void jf_playback_align_subtitle(const int64_t sid);


bool jf_playback_next(void);
bool jf_playback_previous(void);
#endif
