/***************************************************************************
**
** Copyright (C) 2010 Nokia Corporation and/or its subsidiary(-ies).
** All rights reserved.
** Contact: Nokia Corporation (directui@nokia.com)
**
** This file is part of mcompositor.
**
** If you have questions regarding the use of this file, please contact
** Nokia at directui@nokia.com.
**
** This library is free software; you can redistribute it and/or
** modify it under the terms of the GNU Lesser General Public
** License version 2.1 as published by the Free Software Foundation
** and appearing in the file LICENSE.LGPL included in the packaging
** of this file.
**
****************************************************************************/

#include "mcompositewindow.h"
#include "mcompwindowanimator.h"
#include "mcompositemanager.h"
#include "mcompositemanager_p.h"
#include "mtexturepixmapitem.h"

#include <QX11Info>
#include <QGraphicsScene>
#include <QGraphicsSceneMouseEvent>
#include <X11/Xatom.h>

bool MCompositeWindow::window_transitioning = false;

MCompositeWindow::MCompositeWindow(Qt::HANDLE window, QGraphicsItem *p)
    : QGraphicsItem(p),
      scalefrom(1),
      scaleto(1),
      scaled(false),
      zval(1),
      blur(false),
      iconified(false),
      iconified_final(false),
      iconify_state(NoIconifyState),
      destroyed(false),
      requestzval(false),
      process_status(NORMAL),
      need_decor(false),
      is_decorator(false),
      window_visible(true),
      transient_for(0),
      wants_focus(false),
      window_obscured(false),
      win_id(window)
{
    memset(&req_geom, 0, sizeof(req_geom));
    anim = new MCompWindowAnimator(this);
    connect(anim, SIGNAL(transitionDone()),  SLOT(finalizeState()));
    connect(anim, SIGNAL(transitionDone()),  SLOT(windowSettled()));
    connect(anim, SIGNAL(transitionStart()), SLOT(windowTransitioning()));
    thumb_mode = false;
    setAcceptHoverEvents(true);

    t_ping = new QTimer(this);
    connect(t_ping, SIGNAL(timeout()), SLOT(pingWindow()));
}

MCompositeWindow::~MCompositeWindow()
{
    delete anim;
    anim = 0;
}

void MCompositeWindow::setBlurred(bool b)
{
    blur = b;
    update();
}

void MCompositeWindow::setUnBlurred()
{
    blur = false;
    update();
}

bool MCompositeWindow::blurred()
{
    return blur;
}

void MCompositeWindow::saveState()
{
    anim->saveState();
}

void MCompositeWindow::localSaveState()
{
    anim->localSaveState();
}

// void MCompositeWindow::restore()
// {
//     anim->restore();
// }

// set the scale point to actual values
void MCompositeWindow::setScalePoint(qreal from, qreal to)
{
    scalefrom = from;
    scaleto = to;
}

void MCompositeWindow::setThumbMode(bool mode)
{
    thumb_mode = mode;
}

/* This is a delayed animation. Actual animation is triggered
 * when startTransition() is called
 */
void MCompositeWindow::iconify(const QRectF &iconGeometry, bool defer)
{
    this->iconGeometry = iconGeometry;
    origPosition = pos();

    // horizontal and vert. scaling factors
    qreal sx = iconGeometry.width() / boundingRect().width();
    qreal sy = iconGeometry.height() / boundingRect().height();

    anim->deferAnimation(defer);
    anim->translateScale(qreal(1.0), qreal(1.0), sx, sy,
                         iconGeometry.topLeft());
    iconified = true;
}

void MCompositeWindow::setIconified(bool iconified)
{
    iconified_final = iconified;
    iconify_state = ManualIconifyState;
    if (iconified && !anim->pendingAnimation())
        emit itemIconified(this);
    else if (!iconified && !anim->pendingAnimation())
        iconify_state = NoIconifyState;
}

MCompositeWindow::IconifyState MCompositeWindow::iconifyState() const
{
    return iconify_state;
}

void MCompositeWindow::setWindowObscured(bool obscured, bool no_notify)
{
    if (obscured == window_obscured)
        return;
    window_obscured = obscured;

    if (!no_notify) {
        XVisibilityEvent c;
        c.type       = VisibilityNotify;
        c.send_event = True;
        c.window     = window();
        c.state      = obscured ? VisibilityFullyObscured :
                                  VisibilityUnobscured;
        XSendEvent(QX11Info::display(), window(), true,
                   VisibilityChangeMask, (XEvent *)&c);
    }
}

void MCompositeWindow::startTransition()
{
    if (anim->pendingAnimation()) {
        anim->startAnimation();
        anim->deferAnimation(false);
    }
}

// TODO: have an option of disabling the animation
void MCompositeWindow::restore(const QRectF &iconGeometry, bool defer)
{
    this->iconGeometry = iconGeometry;
    // horizontal and vert. scaling factors
    qreal sx = iconGeometry.width() / boundingRect().width();
    qreal sy = iconGeometry.height() / boundingRect().height();

    setVisible(true);

    anim->deferAnimation(defer);
    anim->translateScale(qreal(1.0), qreal(1.0), sx, sy, origPosition, true);
    iconified = false;
}

void MCompositeWindow::prettyDestroy()
{
    setVisible(true);
    destroyed = true;
    iconify();
}

void MCompositeWindow::finalizeState()
{
    // request zvalue
    if (requestzval)
        setZValue(zval);

    // iconification status
    if (iconified) {
        iconified_final = true;
        hide();
        iconify_state = TransitionIconifyState;
        emit itemIconified(this);
    } else {
        iconify_state = NoIconifyState;
        iconified_final = false;
        show();
        QTimer::singleShot(200, this, SLOT(q_itemRestored()));
    }

    // item lifetime
    if (destroyed)
        deleteLater();

    requestzval = false;
}

void MCompositeWindow::q_itemRestored()
{
    emit itemRestored(this);
}

void MCompositeWindow::requestZValue(int zvalue)
{
    if (anim->isActive()) {
        zval = zvalue;
        requestzval = true;
    } else {
        setZValue(zvalue);
        requestzval = false;
    }
}

bool MCompositeWindow::isIconified() const
{
    if (anim->isActive())
        return false;

    return iconified_final;
}
bool MCompositeWindow::isScaled() const
{
    return scaled;
}
void MCompositeWindow::setScaled(bool s)
{
    scaled = s;
}

Window MCompositeWindow::transientFor()
{
    /* TODO: make this update the property based on PropertyNotifys */
    XGetTransientForHint(QX11Info::display(), win_id, &transient_for);
    return transient_for;
}

bool MCompositeWindow::wantsFocus()
{
    /* FIXME: check if it is enough to cache this... */
    bool val = true;
    XWMHints *h = XGetWMHints(QX11Info::display(), win_id);
    if (h) {
        if ((h->flags & InputHint) && (h->input == False))
            val = false;
        XFree(h);
    }
    wants_focus = val;
    return wants_focus;
}

XID MCompositeWindow::windowGroup()
{
    /* FIXME: check if it is enough to cache this... */
    XID val = 0;
    XWMHints *h = XGetWMHints(QX11Info::display(), win_id);
    if (h) {
        if (h->flags & WindowGroupHint)
            val = h->window_group;
        XFree(h);
    }
    return val;
}

const QList<Atom>& MCompositeWindow::supportedProtocols()
{
    static Atom atom = 0;
    Atom actual_type;
    int actual_format;
    unsigned long actual_n, left;
    unsigned char *data = NULL;
    if (!atom)
        atom = XInternAtom(QX11Info::display(), "WM_PROTOCOLS", False);
    int result = XGetWindowProperty(QX11Info::display(), win_id,
                                    atom, 0, 100,
                                    False, XA_ATOM, &actual_type,
                                    &actual_format,
                                    &actual_n, &left, &data);
    if (result == Success && data && actual_type == XA_ATOM) {
        wm_protocols.clear();
        for (unsigned int i = 0; i < actual_n; ++i)
            wm_protocols.append(((Atom *)data)[i]);
    }
    if (data) XFree(data);

    return wm_protocols;
}

void MCompositeWindow::hoverEnterEvent(QGraphicsSceneHoverEvent *e)
{
    if (thumb_mode) {
        zval = zValue();
        setZValue(scene()->items().count() + 1);
        setZValue(100);
        anim->translateScale(scalefrom, scalefrom, scaleto, scaleto, pos());

    }
    return QGraphicsItem::hoverEnterEvent(e);
}

void MCompositeWindow::hoverLeaveEvent(QGraphicsSceneHoverEvent   *e)
{
    if (thumb_mode) {
        setZValue(zval);
        anim->translateScale(scalefrom, scalefrom, scaleto, scaleto, pos(), true);
    }
    return QGraphicsItem::hoverLeaveEvent(e);
}

void MCompositeWindow::mouseReleaseEvent(QGraphicsSceneMouseEvent *m)
{
    anim->restore();
    setThumbMode(false);
    setScaled(false);
    setZValue(100);

    emit acceptingInput();
    windowRaised();
    QGraphicsItem::mouseReleaseEvent(m);
}

void MCompositeWindow::manipulationEnabled(bool isEnabled)
{
    setFlag(QGraphicsItem::ItemIsMovable, isEnabled);
    setFlag(QGraphicsItem::ItemIsSelectable, isEnabled);
}

void MCompositeWindow::setVisible(bool visible)
{
    // Set the iconification status as well
    iconified_final = !visible;
    if (visible != window_visible)
        emit visualized(visible);
    window_visible = visible;

    QGraphicsItem::setVisible(visible);
    MCompositeManager *p = (MCompositeManager *) qApp;
    p->d->setWindowDebugProperties(window());
}

void MCompositeWindow::startPing()
{
    if (t_ping->isActive())
        t_ping->stop();
    // this could be configurable. But will do for now. Most WMs use 5s delay
    t_ping->start(5000);
}

void MCompositeWindow::stopPing()
{
    t_ping->stop();
}

void MCompositeWindow::receivedPing(ulong serverTimeStamp)
{
    ping_server_timestamp = serverTimeStamp;
    startPing();
    process_status = NORMAL;
    if (blurred())
        setBlurred(false);
}

void MCompositeWindow::pingTimeout()
{
    if (ping_server_timestamp != ping_client_timestamp && process_status != HUNG) {
        setBlurred(true);
        emit windowHung(this);
        process_status = HUNG;
    }
}

void MCompositeWindow::setClientTimeStamp(ulong timeStamp)
{
    ping_client_timestamp = timeStamp;
}

void MCompositeWindow::pingWindow()
{
    QTimer::singleShot(5000, this, SLOT(pingTimeout()));
    emit pingTriggered(this);
}

MCompositeWindow::ProcessStatus MCompositeWindow::status() const
{
    return process_status;
}

bool MCompositeWindow::needDecoration() const
{
    return need_decor;
}

void MCompositeWindow::setDecorated(bool decorated)
{
    need_decor = decorated;
}

MCompositeWindow *MCompositeWindow::compositeWindow(Qt::HANDLE window)
{
    MCompositeManager *p = (MCompositeManager *) qApp;
    return p->d->windows.value(window, 0);
}

Qt::HANDLE MCompositeWindow::window() const
{
    return win_id;
}

bool MCompositeWindow::isDecorator() const
{
    return is_decorator;
}

void MCompositeWindow::setIsDecorator(bool decorator)
{
    is_decorator = decorator;
}

void MCompositeWindow::windowTransitioning()
{
    window_transitioning = true;
}

void MCompositeWindow::windowSettled()
{
    static Atom desktop_atom = 0;
    window_transitioning = false;
    if (!desktop_atom)
        desktop_atom = XInternAtom(QX11Info::display(),
                                   "_NET_WM_WINDOW_TYPE_DESKTOP", False);
    if (window_type_atom && window_type_atom == desktop_atom)
        emit desktopActivated(this);
}

bool MCompositeWindow::isTransitioning()
{
    return window_transitioning;
}

void MCompositeWindow::delayShow(int delay)
{
    // TODO: only do this for qt/dui apps because it delays translucency
    setVisible(false);
    QTimer::singleShot(delay, this, SLOT(q_delayShow()));
}

void MCompositeWindow::q_delayShow()
{
    MCompositeWindow::setVisible(true);
    updateWindowPixmap();
    MCompositeManager *p = (MCompositeManager *) qApp;
    p->d->updateWinList();
}

QVariant MCompositeWindow::itemChange(GraphicsItemChange change, const QVariant &value)
{
    MCompositeManager *p = (MCompositeManager *) qApp;
    bool zvalChanged = (change == ItemZValueHasChanged);
    if (zvalChanged)
        p->d->setWindowDebugProperties(window());

    if (zvalChanged || change == ItemVisibleHasChanged || change == ItemParentHasChanged)
        p->d->glwidget->update();

    return QGraphicsItem::itemChange(change, value);
}

void MCompositeWindow::update()
{
    MCompositeManager *p = (MCompositeManager *) qApp;
    p->d->glwidget->update();
}

bool MCompositeWindow::windowVisible() const
{
    return window_visible;
}
