#include "mpv.h"
#include "shared.h"
#include "config.h"
#include "disk.h"

#include <stdlib.h>
#include <assert.h>


////////// GLOBAL VARIABLES //////////
extern jf_global_state g_state;
extern jf_options g_options;
extern mpv_handle *g_mpv_ctx;
//////////////////////////////////////


////////// STATIC VARIABLES //////////
static int mpv_flag_yes = 1;
static int mpv_flag_no = 0;
//////////////////////////////////////


////////// STATIC FUNCTIONS //////////
static void jf_mpv_init_cache_dirs(mpv_handle *mpv_ctx);
//////////////////////////////////////


// acrobatically guess the default mpv cache dir and point as many cache
// dir properties there as possible (except for the demux cache)
// (this code is possibly in breach of GPL. sue me)
// CAN FATAL.
#if MPV_CLIENT_API_VERSION >= MPV_MAKE_VERSION(2,1)
static void jf_mpv_init_cache_dirs(mpv_handle *mpv_ctx)
{
    char *icc_cache_dir;
    char *gpu_shader_cache_dir;

    if (g_state.mpv_cache_dir == NULL) {
        char *home = getenv("HOME");
        char *xdg_config = getenv("XDG_CONFIG_HOME");

        char *old_mpv_home = NULL;
        char *mpv_home = NULL;

        // Maintain compatibility with old ~/.mpv
        if (home && home[0]) {
            old_mpv_home = jf_concat(2, home, "/.mpv");
        }

        if (xdg_config && xdg_config[0]) {
            mpv_home = jf_concat(2, xdg_config, "/mpv");
        } else if (home && home[0]) {
            mpv_home = jf_concat(2, home, "/.config/mpv");
        }

        // If the old ~/.mpv exists, and the XDG config dir doesn't, use the old
        // config dir only. Also do not use any other XDG directories.
        if (jf_disk_is_file_accessible(old_mpv_home) && !jf_disk_is_file_accessible(mpv_home)) {
            g_state.mpv_cache_dir = old_mpv_home;
        } else {
            char *xdg_cache = getenv("XDG_CACHE_HOME");

            if (xdg_cache && xdg_cache[0]) {
                g_state.mpv_cache_dir = jf_concat(2, xdg_cache, "/mpv");
            } else if (home && home[0]) {
                g_state.mpv_cache_dir = jf_concat(2, home, "/.cache/mpv");
            }
        }

        free(old_mpv_home);
        free(mpv_home);
    }

    // read property beforehand and honour user preference
    JF_MPV_ASSERT(mpv_get_property(mpv_ctx, "icc-cache-dir", MPV_FORMAT_STRING, &icc_cache_dir));
    if (icc_cache_dir == NULL || icc_cache_dir[0] == '\0') {
        JF_MPV_ASSERT(JF_MPV_SET_OPTPROP(mpv_ctx, "icc-cache-dir", MPV_FORMAT_STRING, &g_state.mpv_cache_dir));
    }

    JF_MPV_ASSERT(mpv_get_property(mpv_ctx, "gpu-shader-cache-dir", MPV_FORMAT_STRING, &gpu_shader_cache_dir));
    if (gpu_shader_cache_dir == NULL || gpu_shader_cache_dir[0] == '\0') {
        JF_MPV_ASSERT(JF_MPV_SET_OPTPROP(mpv_ctx, "gpu-shader-cache-dir", MPV_FORMAT_STRING, &g_state.mpv_cache_dir));
    }

    // let's make a macro if these ever become 3+...
}
#endif


mpv_handle *jf_mpv_create(void)
{
    mpv_handle *mpv_ctx;
    char *x_emby_token;

    // init mpv core
    assert((mpv_ctx = mpv_create()) != NULL);
    JF_MPV_ASSERT(JF_MPV_SET_OPTPROP(mpv_ctx, "config-dir", MPV_FORMAT_STRING, &g_state.config_dir));
    JF_MPV_ASSERT(JF_MPV_SET_OPTPROP(mpv_ctx, "config", MPV_FORMAT_FLAG, &mpv_flag_yes));
    JF_MPV_ASSERT(JF_MPV_SET_OPTPROP(mpv_ctx, "osc", MPV_FORMAT_FLAG, &mpv_flag_yes));
    JF_MPV_ASSERT(JF_MPV_SET_OPTPROP(mpv_ctx, "input-default-bindings", MPV_FORMAT_FLAG, &mpv_flag_yes));
    JF_MPV_ASSERT(JF_MPV_SET_OPTPROP(mpv_ctx, "input-vo-keyboard", MPV_FORMAT_FLAG, &mpv_flag_yes));
    JF_MPV_ASSERT(JF_MPV_SET_OPTPROP(mpv_ctx, "input-terminal", MPV_FORMAT_FLAG, &mpv_flag_yes));
    assert((x_emby_token = jf_concat(2, "x-emby-token: ", g_options.token)) != NULL);
    JF_MPV_ASSERT(JF_MPV_SET_OPTPROP_STRING(mpv_ctx, "http-header-fields", x_emby_token));
    free(x_emby_token);
    JF_MPV_ASSERT(mpv_observe_property(mpv_ctx, 0, "time-pos", MPV_FORMAT_INT64));
    JF_MPV_ASSERT(mpv_observe_property(mpv_ctx, 0, "sid", MPV_FORMAT_INT64));
    JF_MPV_ASSERT(mpv_observe_property(mpv_ctx, 0, "options/loop-playlist", MPV_FORMAT_NODE));  

    JF_MPV_ASSERT(mpv_initialize(mpv_ctx));

    // profile must be applied as a command
    if (g_options.mpv_profile != NULL) {
        const char *apply_profile[] = { "apply-profile", g_options.mpv_profile, NULL };
        if (mpv_command(mpv_ctx, apply_profile) < 0) {
            fprintf(stderr,
                    "FATAL: could not apply mpv profile \"%s\". Are you sure it exists in mpv.conf?\n",
                    g_options.mpv_profile);
            jf_exit(JF_EXIT_FAILURE);
        }
    }

// cache dirs may be set by user in profile
#if MPV_CLIENT_API_VERSION >= MPV_MAKE_VERSION(2,1)
    jf_mpv_init_cache_dirs(mpv_ctx);
#endif  

    return mpv_ctx;
}


void jf_mpv_terminal(mpv_handle *mpv_ctx, bool enable)
{
    JF_MPV_ASSERT(JF_MPV_SET_OPTPROP(mpv_ctx, "terminal", MPV_FORMAT_FLAG, enable ? &mpv_flag_yes : &mpv_flag_no));
}
