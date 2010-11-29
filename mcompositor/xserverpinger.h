#ifndef XSERVERPINGER_H
#define XSERVERPINGER_H

#include <QObject>
#include <QTimer>

#include <xcb/xcb.h>

class XServerPinger: public QObject
{
    Q_OBJECT

public:
    XServerPinger(int pingInterval);
protected:
    xcb_connection_t *xcb;
    xcb_get_input_focus_cookie_t request;
    QTimer *timer;

public slots:
    void xInput(int);
    void tick();
    void die(int);
};

#endif // ! XSERVERPINGER_H
