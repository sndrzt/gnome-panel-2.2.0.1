/*
 * GNOME panel x stuff
 *
 * Copyright (C) 2000, 2001 Eazel, Inc.
 *               2002 Sun Microsystems Inc.
 *
 * Authors: George Lebl <jirka@5z.com>
 *          Mark McLoughlin <mark@skynet.ie>
 *
 *  Contains code from the Window Maker window manager
 *
 *  Copyright (c) 1997-2002 Alfredo K. Kojima

 */
#include <config.h>
#include <string.h>
#include <unistd.h>

#include <gdk/gdk.h>
#include <gdk/gdkx.h>

#include <X11/Xlib.h>
#include <X11/Xatom.h>

#include "xstuff.h"

#include "global-keys.h"

static Atom
panel_atom_get (const char *atom_name)
{
	static GHashTable *atom_hash;
	Display           *xdisplay;
	Atom               retval;

	g_return_val_if_fail (atom_name != NULL, None);

	xdisplay = GDK_DISPLAY_XDISPLAY (gdk_display_get_default ());

	if (!atom_hash)
		atom_hash = g_hash_table_new_full (
				g_str_hash, g_str_equal, g_free, NULL);

	retval = GPOINTER_TO_UINT (g_hash_table_lookup (atom_hash, atom_name));
	if (!retval) {
		retval = XInternAtom (xdisplay, atom_name, FALSE);

		if (retval != None)
			g_hash_table_insert (atom_hash, g_strdup (atom_name),
					     GUINT_TO_POINTER (retval));
	}

	return retval;
}

/* Stolen from deskguide */
static gpointer
get_typed_property_data (Display *xdisplay,
			 Window   xwindow,
			 Atom     property,
			 Atom     requested_type,
			 gint    *size_p,
			 guint    expected_format)
{
  static const guint prop_buffer_lengh = 1024 * 1024;
  unsigned char *prop_data = NULL;
  Atom type_returned = 0;
  unsigned long nitems_return = 0, bytes_after_return = 0;
  int format_returned = 0;
  gpointer data = NULL;
  gboolean abort = FALSE;

  g_return_val_if_fail (size_p != NULL, NULL);
  *size_p = 0;

  gdk_error_trap_push ();

  abort = XGetWindowProperty (xdisplay,
			      xwindow,
			      property,
			      0, prop_buffer_lengh,
			      False,
			      requested_type,
			      &type_returned, &format_returned,
			      &nitems_return,
			      &bytes_after_return,
			      &prop_data) != Success;
  if (gdk_error_trap_pop () ||
      type_returned == None)
    abort++;
  if (!abort &&
      requested_type != AnyPropertyType &&
      requested_type != type_returned)
    {
      g_warning (G_GNUC_PRETTY_FUNCTION "(): Property has wrong type, probably on crack");
      abort++;
    }
  if (!abort && bytes_after_return)
    {
      g_warning (G_GNUC_PRETTY_FUNCTION "(): Eeek, property has more than %u bytes, stored on harddisk?",
		 prop_buffer_lengh);
      abort++;
    }
  if (!abort && expected_format && expected_format != format_returned)
    {
      g_warning (G_GNUC_PRETTY_FUNCTION "(): Expected format (%u) unmatched (%d), programmer was drunk?",
		 expected_format, format_returned);
      abort++;
    }
  if (!abort && prop_data && nitems_return && format_returned)
    {
      switch (format_returned)
	{
	case 32:
	  *size_p = nitems_return * 4;
	  if (sizeof (gulong) == 8)
	    {
	      guint32 i, *mem = g_malloc0 (*size_p + 1);
	      gulong *prop_longs = (gulong*) prop_data;

	      for (i = 0; i < *size_p / 4; i++)
		mem[i] = prop_longs[i];
	      data = mem;
	    }
	  break;
	case 16:
	  *size_p = nitems_return * 2;
	  break;
	case 8:
	  *size_p = nitems_return;
	  break;
	default:
	  g_warning ("Unknown property data format with %d bits (extraterrestrial?)",
		     format_returned);
	  break;
	}
      if (!data && *size_p)
	{
	  guint8 *mem = g_malloc (*size_p + 1);

	  memcpy (mem, prop_data, *size_p);
	  mem[*size_p] = 0;
	  data = mem;
	}
    }

  if (prop_data)
    XFree (prop_data);
  
  return data;
}

gboolean
xstuff_is_compliant_wm (void)
{
	Display  *xdisplay;
	Window    root_window;
	gpointer  data;
	int       size;

	xdisplay = GDK_DISPLAY_XDISPLAY (gdk_display_get_default ());
	root_window = GDK_WINDOW_XWINDOW (
				gdk_get_default_root_window ());

        /* FIXME this is totally broken; should be using
         * gdk_net_wm_supports() on particular hints when we rely
         * on those particular hints
         */
	data = get_typed_property_data (
			xdisplay, root_window,
			panel_atom_get ("_NET_SUPPORTED"),
			XA_ATOM, &size, 32);

	if (!data)
		return FALSE;

	/* Actually checks for some of these */
	g_free (data);
	return TRUE;
}

gboolean
xstuff_net_wm_supports (const char *hint)
{
	return gdk_net_wm_supports (gdk_atom_intern (hint, FALSE));
}

void
xstuff_set_no_group_and_no_input (GdkWindow *win)
{
	XWMHints *old_wmhints;
	XWMHints wmhints = {0};

	XDeleteProperty (GDK_WINDOW_XDISPLAY (win),
			 GDK_WINDOW_XWINDOW (win),
			 panel_atom_get ("WM_CLIENT_LEADER"));

	old_wmhints = XGetWMHints (GDK_WINDOW_XDISPLAY (win),
				   GDK_WINDOW_XWINDOW (win));
	/* General paranoia */
	if (old_wmhints != NULL) {
		memcpy (&wmhints, old_wmhints, sizeof (XWMHints));
		XFree (old_wmhints);

		wmhints.flags &= ~WindowGroupHint;
		wmhints.flags |= InputHint;
		wmhints.input = False;
		wmhints.window_group = 0;
	} else {
		/* General paranoia */
		wmhints.flags = InputHint | StateHint;
		wmhints.window_group = 0;
		wmhints.input = False;
		wmhints.initial_state = NormalState;
	}

	XSetWMHints (GDK_WINDOW_XDISPLAY (win),
		     GDK_WINDOW_XWINDOW (win),
		     &wmhints);
}

/* This is such a broken stupid function. */   
void
xstuff_set_pos_size (GdkWindow *window, int x, int y, int w, int h)
{
	XSizeHints size_hints;
	int old_x, old_y, old_w, old_h;

	old_x = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (window), "xstuff-cached-x"));
	old_y = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (window), "xstuff-cached-y"));
	old_w = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (window), "xstuff-cached-w"));
	old_h = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (window), "xstuff-cached-h"));

	if (x == old_x && y == old_y && w == old_w && h == old_h)
		return;

	/* Do not add USPosition / USSize here, fix the damn WM */
	size_hints.flags = PPosition | PSize | PMaxSize | PMinSize;
	size_hints.x = 0; /* window managers aren't supposed to and  */
	size_hints.y = 0; /* don't use these fields */
	size_hints.width = w;
	size_hints.height = h;
	size_hints.min_width = w;
       	size_hints.min_height = h;
	size_hints.max_width = w;
	size_hints.max_height = h;
  
	gdk_error_trap_push ();

	XSetWMNormalHints (GDK_WINDOW_XDISPLAY (window),
			   GDK_WINDOW_XWINDOW (window),
			   &size_hints);

	gdk_window_move_resize (window, x, y, w, h);

	gdk_flush ();
	gdk_error_trap_pop ();

	g_object_set_data (G_OBJECT (window), "xstuff-cached-x", GINT_TO_POINTER (x));
	g_object_set_data (G_OBJECT (window), "xstuff-cached-y", GINT_TO_POINTER (y));
	g_object_set_data (G_OBJECT (window), "xstuff-cached-w", GINT_TO_POINTER (w));
	g_object_set_data (G_OBJECT (window), "xstuff-cached-h", GINT_TO_POINTER (h));
}

void
xstuff_set_wmspec_dock_hints (GdkWindow *window,
			      gboolean autohide)
{
        Atom atoms [2] = { None, None };
        
	if (!autohide)
		atoms [0] = panel_atom_get ("_NET_WM_WINDOW_TYPE_DOCK");
	else {
		atoms [0] = panel_atom_get ("_GNOME_WINDOW_TYPE_AUTOHIDE_PANEL");
		atoms [1] = panel_atom_get ("_NET_WM_WINDOW_TYPE_DOCK");
	}

        XChangeProperty (GDK_WINDOW_XDISPLAY (window),
                         GDK_WINDOW_XWINDOW (window),
			 panel_atom_get ("_NET_WM_WINDOW_TYPE"),
                         XA_ATOM, 32, PropModeReplace,
                         (unsigned char *) atoms, 
			 autohide ? 2 : 1);
}

void
xstuff_set_wmspec_strut (GdkWindow *window,
			 int        left,
			 int        right,
			 int        top,
			 int        bottom)
{
	long vals [4];
        
	vals [0] = left;
	vals [1] = right;
	vals [2] = top;
	vals [3] = bottom;

        XChangeProperty (GDK_WINDOW_XDISPLAY (window),
                         GDK_WINDOW_XWINDOW (window),
			 panel_atom_get ("_NET_WM_STRUT"),
                         XA_CARDINAL, 32, PropModeReplace,
                         (unsigned char *) vals, 4);
}

void
xstuff_delete_property (GdkWindow *window, const char *name)
{
	Display *xdisplay = GDK_WINDOW_XDISPLAY (window);
	Window   xwindow  = GDK_WINDOW_XWINDOW (window);

        XDeleteProperty (xdisplay, xwindow,
			 panel_atom_get (name));
}

void
xstuff_init (void)
{
	GdkDisplay *display;
	int         screens, i;

	display = gdk_display_get_default ();
	screens = gdk_display_get_n_screens (display);

	for (i = 0; i < screens; i++) {
		GdkScreen *screen;
		GdkWindow *root_window;

		screen = gdk_display_get_screen (display, i);
		root_window = gdk_screen_get_root_window (screen);

		gdk_window_add_filter (
			root_window,
			(GdkFilterFunc) panel_global_keys_filter,
			screen);
	}
}

/* Zoom animation */
#define MINIATURIZE_ANIMATION_FRAMES_Z   1
#define MINIATURIZE_ANIMATION_STEPS_Z    6
/* the delay per draw */
#define MINIATURIZE_ANIMATION_DELAY_Z    10

static void 
draw_zoom_animation (GdkScreen *gscreen,
		     int x, int y, int w, int h,
		     int fx, int fy, int fw, int fh, 
		     int steps)
{
#define FRAMES (MINIATURIZE_ANIMATION_FRAMES_Z)
	float cx[FRAMES], cy[FRAMES], cw[FRAMES], ch[FRAMES];
	float xstep, ystep, wstep, hstep;
	int i, j;
	GC frame_gc;
	XGCValues gcv;
	GdkColor color = { 65535, 65535, 65535 };
	Display *dpy;
	Window root_win;
	int screen;
	int depth;

	dpy = gdk_x11_display_get_xdisplay (gdk_screen_get_display (gscreen));
	root_win = gdk_x11_drawable_get_xid (gdk_screen_get_root_window (gscreen));
	screen = gdk_screen_get_number (gscreen);
	depth = gdk_drawable_get_depth (gdk_screen_get_root_window (gscreen));

	/* frame GC */
	gdk_colormap_alloc_color (
		gdk_screen_get_system_colormap (gscreen), &color, FALSE, TRUE);
	gcv.function = GXxor;
	/* this will raise the probability of the XORed color being different
	 * of the original color in PseudoColor when not all color cells are
	 * initialized */
	if (DefaultVisual(dpy, screen)->class==PseudoColor)
		gcv.plane_mask = (1<<(depth-1))|1;
	else
		gcv.plane_mask = AllPlanes;
	gcv.foreground = color.pixel;
	if (gcv.foreground == 0)
		gcv.foreground = 1;
	gcv.line_width = 1;
	gcv.subwindow_mode = IncludeInferiors;
	gcv.graphics_exposures = False;

	frame_gc = XCreateGC(dpy, root_win, GCForeground|GCGraphicsExposures
			     |GCFunction|GCSubwindowMode|GCLineWidth
			     |GCPlaneMask, &gcv);

	xstep = (float)(fx-x)/steps;
	ystep = (float)(fy-y)/steps;
	wstep = (float)(fw-w)/steps;
	hstep = (float)(fh-h)/steps;
    
	for (j=0; j<FRAMES; j++) {
		cx[j] = (float)x;
		cy[j] = (float)y;
		cw[j] = (float)w;
		ch[j] = (float)h;
	}
	XGrabServer(dpy);
	for (i=0; i<steps; i++) {
		for (j=0; j<FRAMES; j++) {
			XDrawRectangle(dpy, root_win, frame_gc, 
				       (int)cx[j], (int)cy[j], (int)cw[j], (int)ch[j]);
		}
		XFlush(dpy);
#if (MINIATURIZE_ANIMATION_DELAY_Z > 0)
		usleep(MINIATURIZE_ANIMATION_DELAY_Z);
#else
		usleep(10);
#endif
		for (j=0; j<FRAMES; j++) {
			XDrawRectangle(dpy, root_win, frame_gc, 
				       (int)cx[j], (int)cy[j], (int)cw[j], (int)ch[j]);
			if (j<FRAMES-1) {
				cx[j]=cx[j+1];
				cy[j]=cy[j+1];
				cw[j]=cw[j+1];
				ch[j]=ch[j+1];
			} else {
				cx[j]+=xstep;
				cy[j]+=ystep;
				cw[j]+=wstep;
				ch[j]+=hstep;
			}
		}
	}

	for (j=0; j<FRAMES; j++) {
		XDrawRectangle(dpy, root_win, frame_gc, 
			       (int)cx[j], (int)cy[j], (int)cw[j], (int)ch[j]);
	}
	XFlush(dpy);
#if (MINIATURIZE_ANIMATION_DELAY_Z > 0)
	usleep(MINIATURIZE_ANIMATION_DELAY_Z);
#else
	usleep(10);
#endif
	for (j=0; j<FRAMES; j++) {
		XDrawRectangle(dpy, root_win, frame_gc, 
			       (int)cx[j], (int)cy[j], (int)cw[j], (int)ch[j]);
	}
    
	XUngrabServer(dpy);
	XFreeGC (dpy, frame_gc);
	gdk_colormap_free_colors (gdk_screen_get_system_colormap (gscreen),
				  &color, 1);
}
#undef FRAMES

void
xstuff_zoom_animate (GtkWidget *widget, GdkRectangle *opt_rect)
{
	GdkScreen *gscreen;
	GdkRectangle rect, dest;
	int monitor;

	if (opt_rect)
		rect = *opt_rect;
	else {
		gdk_window_get_origin (widget->window, &rect.x, &rect.y);
		if (GTK_WIDGET_NO_WINDOW (widget)) {
			rect.x += widget->allocation.x;
			rect.y += widget->allocation.y;
		}
		rect.height = widget->allocation.height;
		rect.width = widget->allocation.width;
	}

	gscreen = gtk_widget_get_screen (widget);
	monitor = gdk_screen_get_monitor_at_window (gscreen, widget->window);
	gdk_screen_get_monitor_geometry (gscreen, monitor, &dest);

	draw_zoom_animation (gscreen,
			     rect.x, rect.y, rect.width, rect.height,
			     dest.x, dest.y, dest.width, dest.height,
			     MINIATURIZE_ANIMATION_DELAY_Z);
}
