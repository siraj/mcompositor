// Instances of XServerPinger send some simple X requests periodically
// and warn no reply arrives until the next ping.
#include "xserverpinger.h"

#include <QCoreApplication>
#include <QSocketNotifier>
#include <QtDebug>

#include <X11/Xlib.h>
#include <X11/Xlib-xcb.h>

#include <xcb/xcb.h>
#include <xcb/xcbext.h>

#include <signal.h>
#include <sys/prctl.h>

// Ping X in @pingInterval miliseconds.
XServerPinger::XServerPinger(int pingInterval)
{
    Display *dpy;

    // Open a separate connection to X.
    dpy = XOpenDisplay(NULL);
    xcb = XGetXCBConnection(dpy);
    connect(new QSocketNotifier(ConnectionNumber(dpy), QSocketNotifier::Read),
            SIGNAL(activated(int)), SLOT(xInput(int)));

    // XGetInputFocus() is our ping request.
    request = xcb_get_input_focus(xcb);
    xcb_flush(xcb);

    timer = new QTimer();
    connect(timer, SIGNAL(timeout()), SLOT(tick()));
    timer->start(pingInterval);
}

void XServerPinger::xInput(int)
{
    void *reply;
    xcb_generic_error_t *error;
    xcb_generic_event_t *event;

    // X has sent us something, read it.
    if ((event = xcb_poll_for_event(xcb)) != NULL)
        // Some event, ignore it
        free(event);

    if (request.sequence
        && xcb_poll_for_reply(xcb, request.sequence, &reply, &error)) {
        if (reply) {
            free(reply);
            request.sequence = 0;
        } else if (error)
            // Ignore
            free(error);
    }
}

void XServerPinger::tick()
{
    if (!request.sequence) {
        // Last ping was successful, keep pinging.
        request = xcb_get_input_focus(xcb);
        xcb_flush(xcb);
    } else
        // Ping timed out
        qWarning("X is on holidays");
}

// Start XServerPinger in a separate process if it's not running yet.
static void altmain() __attribute__((constructor));
static void altmain()
{
    // Don't run again if the parent restarted.
    if (getenv("XSERVERPINGER"))
        return;
    putenv("XSERVERPINGER=1");

    // Start a new process and let our parent (the real mcompositor) go.
    if (fork())
        return;

    // Die with the parent.
    prctl(PR_SET_PDEATHSIG, SIGKILL);

    int meh = 0;
    QCoreApplication app(meh, 0);

    XServerPinger pinger(2500);
    app.exec();
    exit(0);
}
