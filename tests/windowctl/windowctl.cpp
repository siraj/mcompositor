/* Simple test program to show and manipulate windows.
 *
 * Compiling standalone:
 * g++ -lQtCore -lX11 -lXrender -lXdamage -Wall -I/usr/include/qt4/QtCore/ -I/usr/include/qt4/ windowctl.cpp -o windowctl 
 *
 * */

#include <QtCore>
#include <iostream>

#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <unistd.h>
#include <X11/extensions/Xrender.h>
#include <X11/extensions/Xdamage.h>
#include <signal.h>
#include <sys/prctl.h>

#define WIN_W 864
#define WIN_H 480

static Atom win_type_atom, trans_for_atom, workarea_atom;
static int xerror_happened;

static void set_fullscreen (Display *dpy, Window w)
{
  Atom wm_state, state_fs;

  wm_state = XInternAtom (dpy, "_NET_WM_STATE", False);
  state_fs = XInternAtom (dpy, "_NET_WM_STATE_FULLSCREEN", False);

  XChangeProperty (dpy, w, wm_state,
                   XA_ATOM, 32, PropModeAppend,
                   (unsigned char *) &state_fs, 1);
}

static void set_modal (Display *dpy, Window w)
{
  Atom wm_state, state_modal;

  wm_state = XInternAtom (dpy, "_NET_WM_STATE", False);
  state_modal = XInternAtom (dpy, "_NET_WM_STATE_MODAL", False);

  XChangeProperty (dpy, w, wm_state,
                   XA_ATOM, 32, PropModeAppend,
                   (unsigned char *) &state_modal, 1);
}

static void set_kde_override (Display *dpy, Window w)
{
  Atom kde_override;
  kde_override = XInternAtom (dpy, "_KDE_NET_WM_WINDOW_TYPE_OVERRIDE", False);
  XChangeProperty (dpy, w, win_type_atom,
                   XA_ATOM, 32, PropModeAppend,
                   (unsigned char *) &kde_override, 1);
}

typedef enum {
	TYPE_INVALID = 0,
	TYPE_NORMAL,
	TYPE_DIALOG,
	TYPE_INPUT,
	TYPE_NOTIFICATION
} WindowType;

static const char *win_type_str[] = {"INVALID",
	 		       "TYPE_NORMAL", "TYPE_DIALOG", "TYPE_INPUT"};

static void set_window_type (Display *dpy, Window w, WindowType type)
{
  Atom type_atom;

  switch (type) {
  case TYPE_NORMAL:
     type_atom = XInternAtom (dpy, "_NET_WM_WINDOW_TYPE_NORMAL", False);
     break;
  case TYPE_DIALOG:
     type_atom = XInternAtom (dpy, "_NET_WM_WINDOW_TYPE_DIALOG", False);
     break;
  case TYPE_INPUT:
     type_atom = XInternAtom (dpy, "_NET_WM_WINDOW_TYPE_INPUT", False);
     break;
  case TYPE_NOTIFICATION:
     type_atom = XInternAtom (dpy, "_NET_WM_WINDOW_TYPE_NOTIFICATION", False);
     break;
  default:
     return;
  }

  XChangeProperty (dpy, w, win_type_atom,
                   XA_ATOM, 32, PropModeAppend,
                   (unsigned char *) &type_atom, 1);
}

#define _NET_WM_STATE_REMOVE        0    /* remove/unset property */
#define _NET_WM_STATE_ADD           1    /* add/set property */
#define _NET_WM_STATE_TOGGLE        2    /* toggle property  */  

/* this can be used to set fullscreenness after mapping the window */
static void toggle_fullscreen (Display *display, Window xwindow, int mode)
{
  XClientMessageEvent xclient;
  memset (&xclient, 0, sizeof (xclient));
  xclient.type = ClientMessage;
  xclient.window = xwindow;
  xclient.message_type = XInternAtom (display, "_NET_WM_STATE", False);
  xclient.format = 32;
  xclient.data.l[0] = mode;
  xclient.data.l[1] = XInternAtom (display, "_NET_WM_STATE_FULLSCREEN", False);
  xclient.data.l[2] = None;
  xclient.data.l[3] = 0;
  xclient.data.l[4] = 0;
  
  XSendEvent (display, DefaultRootWindow (display), False,
              SubstructureRedirectMask | SubstructureNotifyMask,
              (XEvent *)&xclient);
}

static Bool
timestamp_predicate (Display *display,
                     XEvent  *xevent,
                     XPointer arg)
{
  Q_UNUSED(arg);
  if (xevent->type == PropertyNotify &&
      xevent->xproperty.window == DefaultRootWindow (display) &&
      xevent->xproperty.atom == workarea_atom)
    return True;

  return False;
}

static Time get_server_time (Display *dpy)
{
  XEvent xevent;
  long data = 0;

  /* zero-length append to get timestamp in the PropertyNotify */
  XChangeProperty (dpy, DefaultRootWindow (dpy), workarea_atom,
                   XA_CARDINAL, 32, PropModeAppend,
                   (unsigned char*)&data, 0);

  XIfEvent (dpy, &xevent, timestamp_predicate, NULL);

  return xevent.xproperty.time;
}

static void set_meego_layer (Display *dpy, Window w, int layer)
{
  long data = layer;
  Atom a = XInternAtom (dpy, "_MEEGO_STACKING_LAYER", False);
  XChangeProperty (dpy, w, a, XA_CARDINAL, 32, PropModeReplace,
                   (unsigned char*)&data, 1);
  XSync(dpy, False);
}

static void activate_window (Display *dpy, Window window)
{
      XClientMessageEvent xclient;

      memset (&xclient, 0, sizeof (xclient));
      xclient.type = ClientMessage;
      xclient.window = window;
      xclient.message_type = XInternAtom (dpy, "_NET_ACTIVE_WINDOW", False);
      xclient.format = 32;
      xclient.data.l[0] = 1; /* requestor type; we're an app */
      xclient.data.l[1] = get_server_time (dpy);
      xclient.data.l[2] = None; /* currently active window */
      xclient.data.l[3] = 0;
      xclient.data.l[4] = 0;
      
      XSendEvent (dpy, DefaultRootWindow (dpy), False,
                  SubstructureRedirectMask | SubstructureNotifyMask,
                  (XEvent *)&xclient);
}

static Visual*
get_argb32_visual (Display *dpy)
{
  XVisualInfo         * xvi;
  XVisualInfo           templ;
  int                   nvi;
  int                   i;
  XRenderPictFormat   * format;
  Visual              * visual = NULL;

  templ.depth  = 32;
  templ.c_class  = TrueColor;

  if ((xvi = XGetVisualInfo (dpy,
                             VisualDepthMask|VisualClassMask,
                             &templ,
                             &nvi)) == NULL)
    return NULL;
  
  for (i = 0; i < nvi; i++) 
    {
      format = XRenderFindVisualFormat (dpy, xvi[i].visual);
      if (format->type == PictTypeDirect && format->direct.alphaMask)
        {
          /*printf("%s: ARGB visual found\n", __func__);*/
          visual = xvi[i].visual;
          break;
        }
    }

  XFree (xvi);
  return visual;
}

static int error_handler (Display *dpy, XErrorEvent *err)
{
	Q_UNUSED(dpy);
	Q_UNUSED(err);
	xerror_happened = 1;
	return 0;
}

static Window create_window(Display *dpy, int width, int height,
		            int argb, int fullscreen, Colormap *colormap,
			    int override)
{
	Window w;
        XSetWindowAttributes attrs;
	int attr_mask = CWOverrideRedirect;
        attrs.override_redirect = override ? True : False;
        if (argb) {
          /* use ARGB window */
          Visual *visual;

          visual = get_argb32_visual (dpy);
          *colormap = XCreateColormap (dpy, DefaultRootWindow (dpy),
                                      visual, AllocNone);
          attrs.colormap = *colormap;
          attrs.border_pixel = BlackPixel (dpy, 0);
          w = XCreateWindow(dpy, DefaultRootWindow (dpy), 0, 0,
                            width, height, 0, 32,
                            InputOutput,
                            visual,
                            attr_mask | CWColormap | CWBorderPixel, &attrs);
        } else {
	  attrs.background_pixel = BlackPixel (dpy, 0);
          w = XCreateWindow(dpy, DefaultRootWindow (dpy), 0, 0,
                            width, height, 0, CopyFromParent, InputOutput,
                            CopyFromParent,
                            attr_mask | CWBackPixel, &attrs);
	  *colormap = DefaultColormap (dpy, 0);
        }
	if (fullscreen)
          set_fullscreen(dpy, w);
	return w;
}

static Bool
wait_predicate (Display *display,
                XEvent  *xevent,
                XPointer arg)
{
  Q_UNUSED(display);
  if ((xevent->type == MapNotify || xevent->type == UnmapNotify) &&
      xevent->xmap.window == (Window)arg)
    return True;

  return False;
}

static void wait_for_mapnotify(Display *dpy, Window w)
{
  XWindowAttributes attrs;
  XEvent xevent;

  /* first check if it's already mapped */
  if (!XGetWindowAttributes (dpy, w, &attrs))
    /* window is not valid */
    exit(1);
  if (attrs.map_state == IsViewable)
    return;

  XIfEvent (dpy, &xevent, wait_predicate, (XPointer)w);
  if (xevent.type == UnmapNotify)
    exit(1);
}

static void print_usage_and_exit(QString& stdOut)
{
#define PROG "windowctl"
  stdOut = "Usage 1: " PROG " [afoemk](n|d|i|b) [transient for <XID>]\n"
	 "a - ARGB (32-bit) window, otherwise 16-bit is used\n"
	 "f - fullscreen window\n"
	 "o - override-redirect window\n"
	 "e - do not exit on unmapping of the window\n"
	 "m - set _NET_WM_STATE_MODAL (makes sense for dialogs only)\n"
	 "k - set _KDE_NET_WM_WINDOW_TYPE_OVERRIDE\n"
	 "n - WM_TYPE_NORMAL window (if 'k' is given, that is the first type)\n"
	 "d - WM_TYPE_DIALOG window\n"
	 "i - WM_TYPE_INPUT window\n"
	 "b - WM_TYPE_NOTIFICATION window ('b' is for banner)\n\n"
	 "Usage 2: " PROG " N|U|F|C|M|T|A|W <XID>\n"
	 "N - unfullscreen the window with <XID>\n"
	 "U - unmap the window with <XID>\n"
	 "F - fullscreen the window with <XID>\n"
	 "C - toggle fullscreenness of the window with <XID>\n"
	 "M - map the window with <XID>\n"
	 "T - make the window with <XID> non-transient\n"
	 "A - activate (_NET_ACTIVE_WINDOW) the window with <XID>\n"
	 "W - wait for mapping of the window with <XID>\n\n"
	 "Usage 3: " PROG " t|L|V|G <XID> (<XID>|'None')\n"
	 "t - make the first window transient for the second one\n"
	 "L - configure the first window beLow the second one\n"
	 "V - configure the first window aboVe the second one\n"
	 "G - set the window to window group of the second XID\n"
	 "Usage 4: " PROG " R t|b|l|r\n"
	 "R - rotate Screen.TopEdge to top, bottom, left, or right\n"
	 "    (requires context-provide)\n"
	 "Usage 5: " PROG " K <name>\n"
	 "K - run 'pkill <name>'\n"
	 "Usage 6: " PROG " E [<XID>] (0-10)\n"
	 "E - set _MEEGO_STACKING_LAYER of new window / window <XID> to 0-10\n";
}

static void configure (Display *dpy, char *first, char *second, bool above)
{
	Window w = strtol(first, NULL, 16);
	Window sibling = None;
	XWindowChanges wc;
	int mask = CWStackMode;

	if (strcmp(second, "None")) {
	    sibling = strtol(second, NULL, 16);
	    mask |= CWSibling;
	}

	memset(&wc, 0, sizeof(wc));
	wc.sibling = sibling;
	wc.stack_mode = above ? Above : Below;
	XConfigureWindow(dpy, w, mask, &wc);
	XSync(dpy, False);
}

static void set_group (Display *dpy, char *first, char *second)
{
	Window w = strtol(first, NULL, 16);
	XID group = strtol(second, NULL, 16);
	XWMHints h;
	memset(&h, 0, sizeof(h));

	h.flags = WindowGroupHint;
	h.window_group = group;
	XSetWMHints(dpy, w, &h);
	XSync(dpy, False);
}

static void do_command (Display *dpy, char command, Window window,
		        Window target, QString& stdOut)
{
	switch (command) {
		case 'N':
			toggle_fullscreen(dpy, window, _NET_WM_STATE_REMOVE);
			break;
		case 'U':
			XSync(dpy, False);
			xerror_happened = 0;
			XUnmapWindow(dpy, window);	
			XSync(dpy, False);
			if (!xerror_happened)
			  stdOut = "Unmap succeeded\n";
			else
			  stdOut = "Unmap failed\n";
			break;
		case 'F':
			toggle_fullscreen(dpy, window, _NET_WM_STATE_ADD);
			break;
		case 'C':
			toggle_fullscreen(dpy, window, _NET_WM_STATE_TOGGLE);
			break;
		case 'M':
			XMapWindow(dpy, window);	
			break;
		case 'T':
			XDeleteProperty(dpy, window, trans_for_atom);
			break;
		case 'A':
			activate_window(dpy, window);
			break;
		case 'W':
			wait_for_mapnotify(dpy, window);
			break;
		case 't':
			XSetTransientForHint(dpy, window, target);
			/*
			XChangeProperty(dpy, window, trans_for_atom, XA_WINDOW,
				32, PropModeReplace,
				(unsigned char*)&target, 1);
				*/
                        break;
		default:
			printf ("unknown command %c\n", command);
			exit(1);
	}
	XFlush(dpy);
}

#if 0
static int child_exit_status, child_exit_signal;

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <errno.h>

static void sig_child(int n)
{
	int status;
	wait(&status);
	if (WIFEXITED(status))
		child_exit_status = WEXITSTATUS(status);
	else if (WIFSIGNALED(status))
		child_exit_signal = WTERMSIG(status);
}
#endif

#include <sys/types.h>
#include <sys/wait.h>

extern char** environ;

static pid_t run_pkill(char *arg, QString& stdOut)
{
	char buf[30];
	pid_t pid = -1;

	snprintf(buf, 30, "'%s'", arg);
	stdOut = buf;
	if ((pid = fork())) {
		int status;
		if (pid == -1)
			stdOut = "fork() error\n";
		else
			wait(&status);
	} else {
		const char *args[] = {"/usr/bin/pkill", NULL, NULL};
		args[1] = arg;
		execve(args[0], (char**)args, environ);
		exit(1);
	}
	return pid;
}

static pid_t rotate_screen(char *o, QString& stdOut)
{
	pid_t pid;
	char *orientation;
	int pipefd[2];

	switch (*o) {
	case 't':
  		orientation = (char*)"top";
		break;
	case 'b':
  		orientation = (char*)"bottom";
		break;
	case 'l':
  		orientation = (char*)"left";
		break;
	case 'r':
  		orientation = (char*)"right";
		break;
	default:
		return -1;
	}

	pipe(pipefd);

	if ((pid = fork())) {
		if (pid == -1) {
			stdOut = "fork() error\n";
			return -1;
		}
		close(pipefd[0]);
		sleep(5);
		kill(pid, SIGKILL);
		return pid;
	} else {
		/*
		context-provide org.freedesktop.ContextKit.Commander \
		string Screen.TopEdge left
		*/
		char *args[] = {(char*)"/usr/bin/context-provide",
			        (char*)"org.freedesktop.ContextKit.Commander",
				(char*)"string", (char*)"Screen.TopEdge",
				NULL, NULL};
		args[4] = orientation;
		close(pipefd[1]);
		dup2(pipefd[0], STDIN_FILENO);
		close(pipefd[0]);

		execve(args[0], args, environ);
		exit(1);
	}
	return -1;
}

static bool old_main(QStringList& args, QString& stdOut)
{
        Display *dpy;
        Window w;
        GC green_gc;
        XColor green_col;
        Colormap colormap;
        char green[] = "#00ff00";
        time_t last_time;
	int argb = 0, fullscreen = 0, override_redirect = 0,
            exit_on_unmap = 1, modal = 0, kde_override = 0, meego_layer = -1;
	WindowType windowtype = TYPE_INVALID;

	if (args.count() < 1 || args.count() > 4) {
	  print_usage_and_exit(stdOut);
	  return false;
	}

        if (!(dpy = XOpenDisplay(NULL))) {
	  stdOut = "Can't open X display\n";
	  return false;
	}

        /* catch X errors */
        XSetErrorHandler (error_handler);

        win_type_atom = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE", False);
	/*trans_for_atom = XInternAtom(dpy, "WM_TRANSIENT_FOR", False);*/
	workarea_atom = XInternAtom(dpy, "_NET_WORKAREA", False);

        /* receive MapNotifys and UnmapNotifys for root's children,
	 * PropertyChangeMask is needed for get_server_time() */
        XSelectInput (dpy, DefaultRootWindow (dpy),
		      SubstructureNotifyMask | PropertyChangeMask);

	/*printf ("argc == %d, argv[1] = '%s'\n", argc, argv[1]);*/
	for (char *p = args.at(0).toAscii().data(); *p; ++p) {
		char *command;
		if (*p == 'a') {
			argb = 1;
			continue;
		}
		if (*p == 'f') {
			fullscreen = 1;
			continue;
		}
		if (*p == 'm') {
			modal = 1;
                        continue;
                }
		if (*p == 'k') {
			kde_override = 1;
                        continue;
                }
		if (*p == 'd') {
		       windowtype = TYPE_DIALOG;	
		       continue;
		}
		if (*p == 'n') {
		       windowtype = TYPE_NORMAL;	
		       continue;
		}
		if (*p == 'i') {
		       windowtype = TYPE_INPUT;	
		       continue;
		}
		if (*p == 'b') {
		       windowtype = TYPE_NOTIFICATION;	
		       continue;
		}
		if (*p == 'o') {
		       override_redirect = 1;	
		       continue;
		}
		if (*p == 't') {
			if (args.count() != 3) {
	  			print_usage_and_exit(stdOut);
				return false;
			} else
				do_command(dpy, *p,
					   strtol(args.at(1).toAscii().data(),
						  NULL, 16),
					   strtol(args.at(2).toAscii().data(),
						  NULL, 16), stdOut);
			return true;
		}
		if (*p == 'L' || *p == 'V') {
			if (args.count() != 3) {
	  			print_usage_and_exit(stdOut);
				return false;
			} else {
                                bool mode = (*p == 'V');
				configure(dpy, args.at(1).toAscii().data(),
					  args.at(2).toAscii().data(), mode);
                        }
			return true;
		}
		if (*p == 'G') {
			if (args.count() != 3) {
	  			print_usage_and_exit(stdOut);
				return false;
			} else
				set_group(dpy, args.at(1).toAscii().data(),
					  args.at(2).toAscii().data());
			return true;
		}
		if (*p == 'e') {
		       exit_on_unmap = 0;	
		       continue;
		}
		if (*p == 'K') {
			if (args.count() != 2) {
	  			print_usage_and_exit(stdOut);
				return false;
			}
        		signal (SIGCHLD, SIG_IGN);
			run_pkill(args.at(1).toAscii().data(), stdOut);
			return true;
		}
		if (*p == 'R') {
			if (args.count() != 2) {
	  			print_usage_and_exit(stdOut);
				return false;
			}
        		signal (SIGCHLD, SIG_IGN);
			rotate_screen(args.at(1).toAscii().data(), stdOut);
			return true;
		}
		if (*p == 'E') {
			if (args.count() == 3) {
                                set_meego_layer(dpy,
                                    strtol(args.at(1).toAscii().data(), 0, 16),
                                    atoi(args.at(2).toAscii().data()));
                                return true;
                        } else if (args.count() == 2) {
			        meego_layer = atoi(args.at(1).toAscii().data());
                                break;
                        } else {
	  			print_usage_and_exit(stdOut);
				return false;
                        }
                }
		if ((command = strchr("NUFCMTAW", *p))) {
			if (args.count() != 2) {
	  			print_usage_and_exit(stdOut);
				return false;
			} else
				do_command(dpy, *command,
					   strtol(args.at(1).toAscii().data(),
						  NULL, 16), 0, stdOut);
			return true;
		}
	  	print_usage_and_exit(stdOut);
		return false;
	}

	w = create_window (dpy, WIN_W * 2 / 3, WIN_H * 2 / 3, argb, fullscreen,
                           &colormap, override_redirect);
	/* print XID of the window */
	{
		char buf[20];
		snprintf(buf, 19, "0x%lx\n", w);
		stdOut.append(buf);
	}
        signal (SIGCHLD, SIG_IGN);
	if (fork())
	  return true;

	if (args.count() == 2 && strcmp(args.at(0).toAscii().data(), "E"))
		XSetTransientForHint(dpy, w,
				     strtol(args.at(1).toAscii().data(),
					    NULL, 16));

        if (modal) set_modal(dpy, w);

        if (meego_layer >= 0)
                set_meego_layer(dpy, w, meego_layer);

        green_gc = XCreateGC (dpy, w, 0, NULL);
        XParseColor (dpy, colormap, green, &green_col);
        XAllocColor (dpy, colormap, &green_col);
        XSetForeground (dpy, green_gc, green_col.pixel);

        if (kde_override && windowtype == TYPE_NORMAL)
            set_kde_override(dpy, w);
        set_window_type(dpy, w, windowtype);
        if (kde_override && windowtype != TYPE_NORMAL)
            set_kde_override(dpy, w);

        XSelectInput (dpy, w,
                      ExposureMask | ButtonReleaseMask | ButtonPressMask);

	/* set WM_NAME */
	{
	        char *wmname[] = {(char*)"windowctl"};
		XTextProperty text_prop;
		XStringListToTextProperty (wmname, 1, &text_prop);
		XSetWMName (dpy, w, &text_prop);
	}
	/* set _NET_WM_NAME */
	{
		char name[] = "windowctl";
                Atom a = XInternAtom(dpy, "_NET_WM_NAME", False);
                Atom utf8_a = XInternAtom(dpy, "UTF8_STRING", False);
		XChangeProperty(dpy, w, a, utf8_a,
				8, PropModeReplace, (unsigned char*)name,
				sizeof(name));
	}

        int damage_event, damage_error;
        XDamageQueryExtension(dpy, &damage_event, &damage_error);
        static const int damage_ev = damage_event + XDamageNotify;
        // listen to root window damage
        XDamageCreate(dpy, DefaultRootWindow(dpy), XDamageReportNonEmpty); 

        struct timeval map_time;
        gettimeofday(&map_time, NULL);

        XMapWindow(dpy, w);  /* map the window */

        last_time = time(NULL);

        for (;;) {
                XEvent xev;

                /*if (XEventsQueued (dpy, QueuedAfterFlush))*/
                XNextEvent(dpy, &xev);

                if (xev.type == Expose) {
		  char str[200];
		  XTextItem item;
		  int len = snprintf(str, 199,
				     "This is %s%s window 0x%lx",
				     win_type_str[windowtype],
				     override_redirect ? " (OR)" : "",
				     w);
		  item.chars = str;
		  item.nchars = len > 0 ? len : 0;
		  item.delta = 0;
		  item.font = None;

                  if (argb) {
                    /* draw background with transparent colour */
                    XImage ximage;
                    ximage.width = WIN_W;
                    ximage.height = WIN_H;
                    ximage.format = ZPixmap;
                    ximage.byte_order = LSBFirst;
                    ximage.bitmap_unit = 32;
                    ximage.bitmap_bit_order = LSBFirst;
                    ximage.bitmap_pad = 32;
                    ximage.depth = 32;
                    ximage.red_mask = 0;
                    ximage.green_mask = 0;
                    ximage.blue_mask = 0;
                    ximage.xoffset = 0;
                    ximage.bits_per_pixel = 32;
                    ximage.bytes_per_line = WIN_W * 4;
                    ximage.data = (char*)calloc (1, WIN_W * WIN_H * 4);

                    XInitImage (&ximage);

                    XPutImage (dpy, w, green_gc, &ximage, 0, 0, 0, 0,
                               WIN_W, WIN_H);
                    free (ximage.data);
                  }
                  //draw_rect (dpy, w, green_gc, &green_col, 100, 100);
		  XDrawText (dpy, w, green_gc, 300, 200, &item, 1);
                }
                else if (xev.type == PropertyNotify) {
                }
                else if (xev.type == ButtonRelease) {
                  /*XButtonEvent *e = (XButtonEvent*)&xev;*/
                }
                else if (xev.type == MapNotify) {
                  /*XMapEvent *e = (XMapEvent*)&xev;*/
                }
                else if (xev.type == UnmapNotify) {
                  XUnmapEvent *e = (XUnmapEvent*)&xev;
		  if (e->window == w && exit_on_unmap)
		    /* our window was unmapped */
	            exit(0);
                }
                else if (xev.type == ConfigureNotify) {
                  /*XConfigureEvent *e = (XConfigureEvent*)&xev;*/
                }
                else if (xev.type == damage_ev) {
                  struct timeval dtime;
                  gettimeofday(&dtime, NULL);
                  unsigned int secs = dtime.tv_sec - map_time.tv_sec;
                  unsigned int usecs = dtime.tv_usec - map_time.tv_usec;
                  printf("Damage received, %us and %uus from mapping\n",
                         secs, usecs);
                  /*
                  XDamageSubtract(dpy, ((XDamageNotifyEvent*)&xev)->damage,
                                  None, None);
                                  */
                }
        }

        return 0;
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