#ifndef _JF_MPV
#define _JF_MPV

#include <stdbool.h>
#include <mpv/client.h>


// workaround for mpv bug #3988
#if MPV_CLIENT_API_VERSION <= MPV_MAKE_VERSION(1,24)
#define JF_MPV_SET_OPTPROP mpv_set_option
#define JF_MPV_SET_OPTPROP_STRING mpv_set_option_string
#else
#define JF_MPV_SET_OPTPROP mpv_set_property
#define JF_MPV_SET_OPTPROP_STRING mpv_set_property_string
#endif


mpv_handle *jf_mpv_create(void);
void jf_mpv_terminal(mpv_handle *mpv_ctx, bool enable);

#endif
