#ifndef UI_GTK_H
#define UI_GTK_H

/* Work around an -Wstrict-prototypes warning in GTK headers */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstrict-prototypes"
#include <gtk/gtk.h>
#pragma GCC diagnostic pop

#include <gdk/gdkkeysyms.h>

#ifdef GDK_WINDOWING_X11
#include <gdk/gdkx.h>
#include <X11/XKBlib.h>
#endif

#ifdef GDK_WINDOWING_WAYLAND
#include <gdk/gdkwayland.h>
#endif

#include "ui/clipboard.h"
#include "ui/console.h"
#include "ui/kbd-state.h"
#ifdef CONFIG_VTE
#include "qemu/fifo8.h"
#endif

#define MAX_VCS 10

typedef struct GtkDisplayState GtkDisplayState;

typedef struct VirtualGfxConsole {
    GtkWidget *drawing_area;
    DisplayChangeListener dcl;
    QKbdState *kbd;
    DisplaySurface *ds;
    pixman_image_t *convert;
    cairo_surface_t *surface;
    double preferred_scale;
    double scale_x;
    double scale_y;
} VirtualGfxConsole;

#if defined(CONFIG_VTE)
typedef struct VirtualVteConsole {
    GtkWidget *box;
    GtkWidget *scrollbar;
    GtkWidget *terminal;
    Chardev *chr;
    Fifo8 out_fifo;
    bool echo;
} VirtualVteConsole;
#endif

typedef enum VirtualConsoleType {
    GD_VC_GFX,
    GD_VC_VTE,
} VirtualConsoleType;

typedef struct VirtualConsole {
    GtkDisplayState *s;
    char *label;
    GtkWidget *window;
    GtkWidget *menu_item;
    GtkWidget *tab_item;
    GtkWidget *focus;
    VirtualConsoleType type;
    union {
        VirtualGfxConsole gfx;
#if defined(CONFIG_VTE)
        VirtualVteConsole vte;
#endif
    };
} VirtualConsole;

struct GtkDisplayState {
    GtkWidget *window;

    GtkWidget *menu_bar;

    GtkAccelGroup *accel_group;

    GtkWidget *machine_menu_item;
    GtkWidget *machine_menu;
    GtkWidget *pause_item;
    GtkWidget *reset_item;
    GtkWidget *powerdown_item;
    GtkWidget *quit_item;

    GtkWidget *view_menu_item;
    GtkWidget *view_menu;
    GtkWidget *full_screen_item;
    GtkWidget *copy_item;
    GtkWidget *zoom_in_item;
    GtkWidget *zoom_out_item;
    GtkWidget *zoom_fixed_item;
    GtkWidget *zoom_fit_item;
    GtkWidget *grab_item;
    GtkWidget *grab_on_hover_item;

    int nb_vcs;
    VirtualConsole vc[MAX_VCS];

    GtkWidget *show_tabs_item;
    GtkWidget *untabify_item;
    GtkWidget *show_menubar_item;

    GtkWidget *vbox;
    GtkWidget *notebook;
    int button_mask;
    gboolean last_set;
    int last_x;
    int last_y;
    int grab_x_root;
    int grab_y_root;
    VirtualConsole *kbd_owner;
    VirtualConsole *ptr_owner;

    gboolean full_screen;

    GdkCursor *null_cursor;
    Notifier mouse_mode_notifier;
    gboolean free_scale;
    gboolean keep_aspect_ratio;

    bool external_pause_update;

    QemuClipboardPeer cbpeer;
    uint32_t cbpending[QEMU_CLIPBOARD_SELECTION__COUNT];
    GtkClipboard *gtkcb[QEMU_CLIPBOARD_SELECTION__COUNT];
    bool cbowner[QEMU_CLIPBOARD_SELECTION__COUNT];

    DisplayOptions *opts;
};

/* ui/gtk.c */
void gd_update_windowsize(VirtualConsole *vc);
void gd_update_monitor_refresh_rate(VirtualConsole *vc, GtkWidget *widget);
void gd_hw_gl_flushed(void *vc);

/* gtk-clipboard.c */
void gd_clipboard_init(GtkDisplayState *gd);

void gd_update_scale(VirtualConsole *vc, int ww, int wh, int fbw, int fbh);

#endif /* UI_GTK_H */
