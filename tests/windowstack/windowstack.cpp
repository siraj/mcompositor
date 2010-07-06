/* utility to print the window stack for top-levels and useful
 * information for each top-level
 *
 * Compiling standalone:
 * g++ -lQtCore -lX11 -lXcomposite -Wall -DSTANDALONE=1 -I/usr/include/qt4/QtCore/ -I/usr/include/qt4/ windowstack.cpp -o windowstack
 *
 * Author: Kimmo H. <kimmo.hamalainen@nokia.com> */

#include <QtCore>
#include <QDebug>
#include <QHash>
#include <iostream>

#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/extensions/Xcomposite.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int verbose, list_mapped_only;

static Atom class_atom, name_atom, name_atom2, xembed_atom, pid_atom,
            trans_atom, visi_atom,
            utf8_string_atom,
	    current_app_atom, win_type_atom, wm_state_atom;

static char *get_atom_prop(Display *dpy, Window w, Atom atom)
{ 
          Atom type;
          int format, rc;
          unsigned long items;
          unsigned long left;
          Atom *value = 0;
	  char *copy;

          rc = XGetWindowProperty (dpy, w, atom, 0, 1, False,
                              XA_ATOM, &type, &format,
                              &items, &left, (unsigned char**)&value);
          if (!value || type != XA_ATOM || format == 0 || rc != Success) {
	    copy = strdup("");
	  } else {
	    char *s = XGetAtomName(dpy, *value);
            if (s) {
	        copy = strdup(s);
	        XFree(s);
            } else
	        copy = strdup("");
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

static unsigned long get_card_prop(Display *dpy, Window w, Atom atom)
{ 
          Atom type;
          int format, rc;
          unsigned long items;
          unsigned long left;
          unsigned long *value;

          rc = XGetWindowProperty (dpy, w, atom, 0, 1, False,
                              XA_CARDINAL, &type, &format,
                              &items, &left, (unsigned char**)&value);
          if (type == None || rc != Success)
            return 0;
          else
          {
            return *value;
          }
}

#if 0
static long get_int_prop(Display *dpy, Window w, Atom atom)
{ 
          Atom type;
          int format, rc;
          unsigned long items;
          unsigned long left;
          unsigned long *value;

          rc = XGetWindowProperty (dpy, w, atom, 0, 1, False,
                              XA_INTEGER, &type, &format,
                              &items, &left, (unsigned char**)&value);
          if (type == None || rc != Success)
            return -1;
          else
          {
            return *value;
          }
}

static long get_xembed_prop(Display *dpy, Window w)
{ 
          Atom type;
          int format, rc;
          unsigned long items;
          unsigned long left;
          unsigned long *value;

          rc = XGetWindowProperty (dpy, w, xembed_atom, 0, 2, False,
                              XA_CARDINAL, &type, &format,
                              &items, &left, (unsigned char**)&value);
          if (type == None || rc != Success)
            return -1;
          else
          {
	    long ret;
	    ret = value[1] & (1 << 0);
	    XFree(value);
            return ret;
          }
}
#endif

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

static const char *get_map_state(int state)
{
	switch (state) {
		case IsUnmapped:
			return "unmapped";
		case IsUnviewable:
			return "unviewable";
		case IsViewable:
			return "viewable";
		default:
			return "error";
	}
}

static int xerror;
static int ignoring_error_handler (Display *dpy, XErrorEvent *err)
{
        Q_UNUSED(dpy);
        Q_UNUSED(err);
        xerror = 1;
        return 0;
}

static void print_children(Display *dpy, Window w, int level, QString& stdOut)
{
	unsigned int n_children = 0;
	Window *child_l = NULL;
	Window root_ret, parent_ret;
	int i;
	char *wmclass, *wmname, *wmname2, *wmtype, *wmstate;
	Window trans_for;
	char buf[100];
	XWindowAttributes attrs;
	/*
	char xembed_buf[50];
	long xembed;
	*/

	if (!XQueryTree(dpy, w, &root_ret, &parent_ret,
	                &child_l, &n_children))
		return;

	wmclass = get_str_prop(dpy, w, class_atom);
	wmname = get_utf8_prop(dpy, w, name_atom);
	wmname2 = get_str_prop(dpy, w, name_atom2);
	wmtype = get_atom_prop(dpy, w, win_type_atom);
	wmstate = get_atom_prop(dpy, w, wm_state_atom);
	trans_for = get_win_prop(dpy, w, trans_atom);
	if (!XGetWindowAttributes(dpy, w, &attrs))
        	return;

	if (trans_for)
		snprintf(buf, 100, "TR_FOR:0x%lx", trans_for);
	else
		buf[0] = '\0';

#if 0
	xembed = get_xembed_prop(dpy, w);
	if (xembed != -1)
		snprintf(xembed_buf, 50, "(XEmbed %ld)", xembed);
	else
		xembed_buf[0] = '\0';
#endif

	/* print children in reverse order to get the top of the
	 * stack first */
	for (i = n_children - 1; i >= 0; --i) {
		print_children(dpy, child_l[i], level + 1, stdOut);
	}
	XFree(child_l);
	if (level > 0
	    && (!list_mapped_only || attrs.map_state == IsViewable)) {
		int type_i;
		char outbuf[200], indent[10];
		indent[0] = '\0';

		if (wmtype[0] &&
		    strcmp(wmtype, "_KDE_NET_WM_WINDOW_TYPE_OVERRIDE") == 0)
	       	    type_i = sizeof ("_KDE_NET_WM_WINDOW_TYPE");
		else
	       	    type_i = sizeof ("_NET_WM_WINDOW_TYPE");
		const int state_i = sizeof ("_NET_WM_STATE");
		for (i = 1; i < level; ++i)
			strcat(indent, " ");
                long visibility = get_card_prop(dpy, w, visi_atom);
		if (verbose) {
                  int x2, y2;
                  unsigned int w2, h2, b2, d2;
                  Window r2;
                  Pixmap pixmap;
	          XSetErrorHandler(ignoring_error_handler);
                  XSync(dpy, False);
                  xerror = 0;
                  pixmap = XCompositeNameWindowPixmap(dpy, w);
                  /* generate X error for invalid pixmap */
                  XGetGeometry(dpy, pixmap, &r2, &x2, &y2, &w2, &h2, &b2, &d2);
		  snprintf(outbuf, 200,
			"%s0x%lx %s %s %s %s '%s' '%s' %s %s %s %s %d,%d %dx%dx%d\n",
			indent, w,
			wmtype[0] ? wmtype + type_i : "no-TYPE",
			wmstate[0] ? wmstate + state_i : "no-STATE",
			get_map_state(attrs.map_state),
			wmclass ? wmclass : "no-CLASS",
			wmname2 ? wmname2 : "no-NAME",
			wmname ? wmname : "no-NNAME",
			buf[0] ? buf : "no-TR",
                        xerror == 0 ? "redir." : "dir.",
			attrs.override_redirect ? "OR" : "no-OR",
                        (visibility ? "UNOBS." : "OBS."),
			attrs.x, attrs.y,
			attrs.width, attrs.height, attrs.depth);
                  if (pixmap != None)
                      XFreePixmap(dpy, pixmap);
                }
		else
		  snprintf(outbuf, 200,
		           "%s0x%lx %s %s %s '%s' %s %s %s %d,%d %dx%dx%d\n",
			indent, w,
			wmtype[0] ? wmtype + type_i : "no-TYPE",
			/*wmstate[0] ? wmstate + state_i : "no-STATE",*/
			get_map_state(attrs.map_state),
			wmclass ? wmclass : "no-CLASS",
			wmname2 ? wmname2 : "no-NAME",
			/*wmname ? wmname : "no-NNAME"*/
			buf[0] ? buf : "no-TR",
			attrs.override_redirect ? "OR" : "no-OR",
                        (visibility ? "UNOBS." : "OBS."),
			attrs.x, attrs.y,
			attrs.width, attrs.height, attrs.depth);
		stdOut.append(outbuf);
	}
	if (level == 0) {
		char buf[100];
          	snprintf(buf, 100,
			"root 0x%lx, _NET_ACTIVE_WINDOW = 0x%lx\n", w,
			get_win_prop(dpy, w, current_app_atom));
		stdOut.append(buf);
	}

	free(wmclass);
	free(wmname);
	free(wmtype);
	free(wmstate);
}

static int error_handler (Display *dpy, XErrorEvent *err)
{
        Q_UNUSED(dpy);
        Q_UNUSED(err);
        return 0;
}

static bool old_main(QStringList& args, QString& stdOut)
{
	Display *dpy;
	Window root;

	if (args.count() > 0) {
		int i;
		for (i = 0; i < args.count(); ++i) {
			int match = 0;
			if (args.at(i).contains(QChar('v'))) {
				verbose = 1;
				list_mapped_only = 1;
				match = 1;
			}
			if (args.at(i).contains(QChar('m'))) {
				list_mapped_only = 1;
				match = 1;
			}
			if (!match) {
				stdOut = "Usage: windowstack [v][m]\n"
				       "v  verbose\n"
				       "m  list mapped only\n";
				return false;
			}
		}
	}

	dpy = XOpenDisplay(NULL);
	if (dpy == NULL) {
		stdOut = "couldn't open display\n";
		return false;
	}
	XSetErrorHandler(error_handler);

        class_atom = XInternAtom(dpy, "WM_CLASS", False);
        name_atom = XInternAtom(dpy, "_NET_WM_NAME", False);
        name_atom2 = XInternAtom(dpy, "WM_NAME", False);
        xembed_atom = XInternAtom(dpy, "_XEMBED_INFO", False);
        pid_atom = XInternAtom(dpy, "_NET_WM_PID", False);
        trans_atom = XInternAtom(dpy, "WM_TRANSIENT_FOR", False);
        utf8_string_atom = XInternAtom(dpy, "UTF8_STRING", False);
        current_app_atom = XInternAtom(dpy, "_NET_ACTIVE_WINDOW", False);
        win_type_atom = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE", False);
        wm_state_atom = XInternAtom(dpy, "_NET_WM_STATE", False);
        visi_atom = XInternAtom(dpy, "_M_TEST_VISIBILITY", False);

	root = XDefaultRootWindow(dpy);

	print_children(dpy, root, 0, stdOut);

	return true;
}

int main(int argc, char *argv[])
{
   QStringList args;
   QString stdOut;
   int ret;
   for (int i = 1; i < argc; ++i)
        args.append(QString(argv[i]));
   ret = old_main(args, stdOut);
   std::cout << stdOut.toStdString();
   return ret;
}
