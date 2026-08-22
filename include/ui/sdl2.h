#pragma once

/* Avoid compiler warning because macro is redefined in SDL_syswm.h. */
#undef WIN32_LEAN_AND_MEAN

#include <SDL.h>

/* with Alpine / muslc SDL headers pull in directfb headers
 * which in turn trigger warning about redundant decls for
 * direct_waitqueue_deinit.
 */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wredundant-decls"

#include <SDL_syswm.h>

#pragma GCC diagnostic pop

#ifdef CONFIG_SDL_IMAGE
    #include <SDL_image.h>
#endif

#include "ui/kbd-state.h"

struct sdl2_console
{
    DisplayChangeListener dcl;
    DisplaySurface*       surface;
    DisplayOptions*       opts;
    SDL_Texture*          texture;
    SDL_Window*           real_window;
    SDL_Renderer*         real_renderer;
    int                   idx;
    int                   last_vm_running; /* per console for caption reasons */
    int                   x, y, w, h;
    int                   hidden;
    int                   updates;
    int                   idle_counter;
    int                   ignore_hotkeys;
    bool                  gui_keysym;
    QKbdState*            kbd;
    bool                  has_dmabuf;
};

void sdl2_window_create(struct sdl2_console* scon);
void sdl2_window_destroy(struct sdl2_console* scon);
void sdl2_window_resize(struct sdl2_console* scon);
void sdl2_poll_events(struct sdl2_console* scon);

void sdl2_process_key(struct sdl2_console* scon, SDL_KeyboardEvent* ev);
void sdl2_release_modifiers(struct sdl2_console* scon);

void sdl2_2d_update(DisplayChangeListener* dcl, int x, int y, int w, int h);
void sdl2_2d_switch(DisplayChangeListener* dcl, DisplaySurface* new_surface);
void sdl2_2d_refresh(DisplayChangeListener* dcl);
void sdl2_2d_redraw(struct sdl2_console* scon);
bool sdl2_2d_check_format(DisplayChangeListener* dcl, pixman_format_code_t format);
