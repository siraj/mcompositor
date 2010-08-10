#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static Atom class_atom, name_atom, name_atom2,
  trans_atom, win_type_atom,
  utf8_string_atom;

static int error_happened = 0;

static int error_handler(Display *d, XErrorEvent *ev)
{
        error_happened = 1;
        /*
        printf("Error %d for %lx\n", ev->error_code, ev->resourceid);
        */
        return 0;
}

static char *get_atom_prop(Display *dpy, Window w, Atom atom)
{ 
        Atom type;
        int format, rc;
        unsigned long items;
        unsigned long left;
        Atom *value;
	char *copy;

        rc = XGetWindowProperty (dpy, w, atom, 0, 1, False,
                              XA_ATOM, &type, &format,
                              &items, &left, (unsigned char**)&value);
        if (type != XA_ATOM || format == 0 || rc != Success) {
	        copy = strdup("");
        } else {
	        char *s = XGetAtomName(dpy, *value);
	        copy = strdup(s);
	        XFree(s);
        }
        return copy;
}

static Window get_win_prop(Display *dpy, Window w, Atom atom)
{ 
          Atom type;
          int format, rc;
          unsigned long items;
          unsigned long left;
          Window *value;

          rc = XGetWindowProperty (dpy, w, atom, 0, 1, False,
                              XA_WINDOW, &type, &format,
                              &items, &left, (unsigned char**)&value);
          if (type == None || rc != Success)
            return 0;
          else
          {
            return *value;
          }
}

static char *get_str_prop(Display *dpy, Window w, Atom atom)
{ 
          Atom type;
          int format, rc;
          unsigned long items;
          unsigned long left;
          char *value;

          rc = XGetWindowProperty (dpy, w, atom, 0, 200, False,
                              XA_STRING, &type, &format,
                              &items, &left, (unsigned char**)&value);
          if (type == None || rc != Success)
            return NULL;
          else
          {
            char *s = strdup((const char*)value);
	    XFree(value);
            return s;
          }
}

static char *get_utf8_prop(Display *dpy, Window w, Atom atom)
{ 
          Atom type;
          int format, rc;
          unsigned long items;
          unsigned long left;
          char *value;

          rc = XGetWindowProperty (dpy, w, atom, 0, 200, False,
				   utf8_string_atom, &type, &format,
				   &items, &left, (unsigned char**)&value);
          if (type == None || rc != Success)
            return NULL;
          else
          {
            char *s = strdup((const char*)value);
	    XFree(value);
            return s;
          }
}

static char*
get_window_name (Display *dpy, Window w)
{
  char *wmname;

  wmname = get_utf8_prop(dpy, w, name_atom);
  if (wmname && wmname[0]) return wmname;
  if (wmname) free(wmname);

  wmname = get_str_prop(dpy, w, name_atom2);
  if (wmname && wmname[0]) return wmname;
  if (wmname) free(wmname);

  return NULL;
}

static void
dump_window (Display *dpy, Window root, Window w)
{
  Window parent = get_win_prop(dpy, w, trans_atom);
  char *type = get_atom_prop(dpy, w, win_type_atom);
  char *wmclass, *name;
  int free_type = 1;

  if (!type || strlen(type) < 2) {
          free(type);
          free_type = 0;
          type="UNKNOWN";
  }

  if (root == w)
        wmclass = strdup("(root window)");
  else
        wmclass = get_str_prop(dpy, w, class_atom);

  name = get_window_name (dpy, w);
  if (strstr(type, "_NET_WM_WINDOW_"))
    printf ("%07lx %s %-7s %s",
	  w,
          wmclass,
          type + strlen("_NET_WM_WINDOW_"),
	  !name ? "(no name)" : name);
  else
    printf ("%07lx %s %-7s %s",
	  w,
          wmclass,
          type,
	  !name ? "(no name)" : name);

  free(name);
  free(wmclass);
  if (free_type) free(type);

  if (parent) {
    name = get_window_name (dpy, parent);
    printf (" (tr. to %07lx %s)",
	    parent,
	    !name ? "(no name)" : name);
    free(name);
  }

  printf ("\n");
}

static int select_input(Display *dpy, Window win)
{
        error_happened = 0;
        XSelectInput(dpy, win, FocusChangeMask);
        XSync(dpy, False);
        return !error_happened;
}

int main(int argc, char *argv[])
{
        Display *dpy;
	Window root, focused = None, old_focused = None;
        int reverted_to;
        struct timespec ts;
        XFocusChangeEvent *xev;
        int one_shot = 0;
        
        ts.tv_sec = 0;
        ts.tv_nsec = 10000000; /* 0.01 seconds */
        xev = malloc(sizeof(XEvent));

	dpy = XOpenDisplay(NULL);
	if (dpy == NULL) {
		printf("couldn't open display\n");
		return 1;
	}
        if (argc == 2) one_shot = 1;

        XSetErrorHandler(error_handler);

        class_atom = XInternAtom(dpy, "WM_CLASS", False);
        trans_atom = XInternAtom(dpy, "WM_TRANSIENT_FOR", False);
        name_atom = XInternAtom(dpy, "_NET_WM_NAME", False);
        name_atom2 = XInternAtom(dpy, "WM_NAME", False);
        win_type_atom = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE", False);
        utf8_string_atom = XInternAtom(dpy, "UTF8_STRING", False);

        root = XDefaultRootWindow(dpy);

        for (; ; old_focused = focused) {

                XGetInputFocus(dpy, &focused, &reverted_to);
                if (focused != old_focused) {
                        printf("Focused  ");
                        dump_window(dpy, root, focused);
                        if (one_shot) return 0;
                }

                if (select_input(dpy, focused)) {
                        /* block here only if XSelectInput succeeded */
                        XNextEvent(dpy, (XEvent*)xev);

                        /* TODO: filter out inferior etc. events... */
                        if (xev->type == FocusOut) {
                                if (xev->mode == NotifyGrab)
                                        printf("FocusOut (NotifyGrab) ");
                                else if (xev->mode == NotifyUngrab)
                                        printf("FocusOut (NotifyUngrab) ");
                                else
                                        printf("FocusOut ");
                                dump_window(dpy, root, focused);
                                old_focused = None; /* focus unknown */
                        }
                        else if (xev->type == FocusIn) {
                                printf("FocusIn  ");
                                dump_window(dpy, root, focused);
                        }
                        xev->type = 0;
                } else
                        nanosleep(&ts, NULL);
        }

        return 0;
}
