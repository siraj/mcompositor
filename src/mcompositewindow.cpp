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
#include "mdecoratorframe.h"

#include <QX11Info>
#include <QGraphicsScene>
#include <QGraphicsSceneMouseEvent>
#include <X11/Xatom.h>

int MCompositeWindow::window_transitioning = 0;

static QRectF fadeRect = QRectF();

MCompositeWindow::MCompositeWindow(Qt::HANDLE window, 
                                   MWindowPropertyCache *mpc, 
                                   QGraphicsItem *p)
    : QGraphicsItem(p),
      pc(mpc),
      scalefrom(1),
      scaleto(1),
      scaled(false),
      zval(1),
      blur(false),
      iconified(false),
      iconified_final(false),
      iconify_state(NoIconifyState),
      destroyed(false),
      window_status(Normal),
      need_decor(false),
      window_obscured(true), // true to synthesize initial visibility event
      is_transitioning(false),
      pinging_enabled(false),
      dimmed_effect(false),
      win_id(window)
{
    thumb_mode = false;
    if (!mpc->is_valid) {
        is_valid = false;
        pc = 0;
        anim = 0;
        newly_mapped = false;
        sent_ping_timestamp = 0;
        received_ping_timestamp = 0;
        t_ping = 0;
        window_visible = false;
        return;
    } else
        is_valid = true;
    anim = new MCompWindowAnimator(this);
    connect(anim, SIGNAL(transitionDone()),  SLOT(finalizeState()));
    connect(anim, SIGNAL(transitionStart()), SLOT(windowTransitioning()));
    setAcceptHoverEvents(true);

    t_ping = new QTimer(this);
    connect(t_ping, SIGNAL(timeout()), SLOT(pingTimeout()));
    
    MCompAtoms* atoms = MCompAtoms::instance(); 
    if (pc->windowType() == MCompAtoms::NORMAL)
        pc->setWindowTypeAtom(ATOM(_NET_WM_WINDOW_TYPE_NORMAL));
    else
        pc->setWindowTypeAtom(atoms->getType(window));

    // needed before calling isAppWindow()
    QVector<Atom> states = atoms->getAtomArray(window, ATOM(_NET_WM_STATE));
    pc->setNetWmState(states.toList());

    // Newly-mapped non-decorated application windows are not initially 
    // visible to prevent flickering when animation is started.
    // We initially prevent item visibility from compositor itself
    // or it's corresponding thumbnail rendered by the switcher
    bool is_app = isAppWindow();
    window_visible = !is_app;
    setVisible(window_visible);
    newly_mapped = is_app;

    if (fadeRect.isEmpty()) {
        QRectF d = QApplication::desktop()->availableGeometry();
        fadeRect.setWidth(d.width()/2);
        fadeRect.setHeight(d.height()/2);
        fadeRect.moveTo(fadeRect.width()/2, fadeRect.height()/2);
    }
}

MCompositeWindow::~MCompositeWindow()
{
    if (t_ping) {
        stopPing();
        t_ping = 0;
    }
    if (is_transitioning) {
        // we got destroyed during animation
        --window_transitioning;
        is_transitioning = false;
    }
    anim = 0;
    
    if (pc) {
        MCompositeManager *p = (MCompositeManager *) qApp;
        p->d->prop_caches.remove(window());
        pc->deleteLater();
    }    
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
void MCompositeWindow::iconify(const QRectF &icongeometry, bool defer)
{
    if (window_status != MCompositeWindow::Closing)
        window_status = MCompositeWindow::Minimizing;

    if (iconGeometry.isEmpty())
        this->iconGeometry = fadeRect;
    else
        this->iconGeometry = iconGeometry;
    if (!iconified)
        origPosition = pos();

    // horizontal and vert. scaling factors
    qreal sx = iconGeometry.width() / boundingRect().width();
    qreal sy = iconGeometry.height() / boundingRect().height();

    anim->deferAnimation(defer);
    anim->translateScale(qreal(1.0), qreal(1.0), sx, sy,
                         iconGeometry.topLeft());
    iconified = true;
    // do this to avoid stacking code disturbing Z values
    if (!is_transitioning) {
        ++window_transitioning;
        is_transitioning = true;
    }
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
        MCompositeWindow::setVisible(true);
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
    // do this to avoid stacking code disturbing Z values
    if (!is_transitioning) {
        ++window_transitioning;
        is_transitioning = true;
    }
}

void MCompositeWindow::showWindow()
{
    // defer putting this window in the _NET_CLIENT_LIST
    // only after animation is done to prevent the switcher from rendering it
    if (!isAppWindow())
        return;
    
    findBehindWindow();
    if (!is_transitioning) {
        ++window_transitioning;
        is_transitioning = true;
    }
    if (newly_mapped) 
        QTimer::singleShot(700, this, SLOT(q_fadeIn()));
    else
        q_fadeIn();
}

void MCompositeWindow::q_fadeIn()
{   
    if (is_transitioning) {
        --window_transitioning;
        is_transitioning = false;
    }
    newly_mapped = false;
    setVisible(true);
    setOpacity(0.0);
    updateWindowPixmap();
    origPosition = pos();
    setPos(fadeRect.topLeft());
    restore(fadeRect, false);
    newly_mapped = true;
}

void MCompositeWindow::closeWindow()
{
    if (!isAppWindow()) {
        setVisible(false);
        emit windowClosed(this);
        return;
    }
    window_status = MCompositeWindow::Closing;
    
    MCompositeManager *p = (MCompositeManager *) qApp;
    bool defer = false;
    setVisible(true);
    if (!p->isCompositing()) {
        p->d->enableCompositing(true);
        defer = true;
    }
    
    updateWindowPixmap();
    origPosition = pos();
    iconify(iconGeometry, defer);
}

void MCompositeWindow::deleteLater()
{
    destroyed = true;
    if (!is_transitioning)
        QObject::deleteLater();
}

void MCompositeWindow::prettyDestroy()
{
    setVisible(true);
    destroyed = true;
    iconify();
}

void MCompositeWindow::finalizeState()
{
    if (is_transitioning) {
        --window_transitioning;
        is_transitioning = false;
    }
    if (pc->windowTypeAtom() == ATOM(_NET_WM_WINDOW_TYPE_DESKTOP))
        emit desktopActivated(this);

    // iconification status
    if (iconified) {
        iconified_final = true;
        hide();
        iconify_state = TransitionIconifyState;
        emit itemIconified(this);
        if (isClosing()) {
            emit windowClosed(this);
            QTimer::singleShot(200, this, SLOT(deleteLater()));
            return;
        }
    } else {
        iconify_state = NoIconifyState;
        iconified_final = false;
        show();
        QTimer::singleShot(200, this, SLOT(q_itemRestored()));
    }
    window_status = Normal;

    // item lifetime
    if (destroyed)
        deleteLater();
}

void MCompositeWindow::q_itemRestored()
{
    emit itemRestored(this);
}

void MCompositeWindow::requestZValue(int zvalue)
{
    // when animating, Z-value is set again after finishing the animation
    // (setting it later in finalizeState() caused flickering)
    if (!anim->isActive() && !anim->pendingAnimation())
        setZValue(zvalue);
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
    if ((visible  && newly_mapped && isAppWindow()) ||
        (!visible && is_transitioning)) 
        return;
    
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
    if (pinging_enabled || !t_ping)
        // this function can be called repeatedly without extending the timeout
        return;
    // startup: send ping now, otherwise it is sent after timeout
    pinging_enabled = true;
    pingWindow();
    if (t_ping->isActive())
        t_ping->stop();
    // this could be configurable. But will do for now. Most WMs use 5s delay
    t_ping->setSingleShot(false);
    t_ping->setInterval(5000);
    t_ping->start();
}

void MCompositeWindow::stopPing()
{
    pinging_enabled = false;
    if (t_ping)
        t_ping->stop();
}

void MCompositeWindow::receivedPing(ulong serverTimeStamp)
{
    received_ping_timestamp = serverTimeStamp;
    
    if (window_status != Minimizing || window_status != Closing)
        window_status = Normal;
    if (blurred())
        setBlurred(false);
}

void MCompositeWindow::pingTimeout()
{
    if (pinging_enabled && received_ping_timestamp < sent_ping_timestamp
        && window_status != Hung) {
        setBlurred(true);
        
        if (window_status != Minimizing || window_status != Closing)
            window_status = Hung;
        emit windowHung(this);
    }
    if (pinging_enabled)
        // interval timer is still active
        pingWindow();
}

void MCompositeWindow::pingWindow()
{
    ulong t = QDateTime::currentDateTime().toTime_t();
    sent_ping_timestamp = t;
    Window w = window();

    XEvent ev;
    memset(&ev, 0, sizeof(ev));
    ev.xclient.type = ClientMessage;
    ev.xclient.window = w;
    ev.xclient.message_type = ATOM(WM_PROTOCOLS);
    ev.xclient.format = 32;
    ev.xclient.data.l[0] = ATOM(_NET_WM_PING);
    ev.xclient.data.l[1] = t;
    ev.xclient.data.l[2] = w;

    XSendEvent(QX11Info::display(), w, False, NoEventMask, &ev);
}

MCompositeWindow::WindowStatus MCompositeWindow::status() const
{
    return window_status;
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

void MCompositeWindow::windowTransitioning()
{
    if (!is_transitioning) {
        ++window_transitioning;
        is_transitioning = true;
    }
}

bool MCompositeWindow::hasTransitioningWindow()
{
    return window_transitioning > 0;
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
    if (zvalChanged) {
        findBehindWindow();
        p->d->setWindowDebugProperties(window());
    }
    
    if (zvalChanged || change == ItemVisibleHasChanged || change == ItemParentHasChanged)
        p->d->glwidget->update();

    return QGraphicsItem::itemChange(change, value);
}

void MCompositeWindow::findBehindWindow()
{
    MCompositeManager *p = (MCompositeManager *) qApp;
    int behind_i = indexInStack() - 1;
    if (behind_i >= 0 && behind_i < p->d->stacking_list.size()) {
        MCompositeWindow* w = MCompositeWindow::compositeWindow(p->d->stacking_list.at(behind_i));
        if (w->propertyCache()->windowState() == NormalState 
            && w->propertyCache()->isMapped()
            && !w->propertyCache()->isDecorator()) 
            behind_window = w;
        else if (w->propertyCache()->isDecorator() && MDecoratorFrame::instance()->managedClient())
            behind_window = MDecoratorFrame::instance()->managedClient();
        else
            behind_window = MCompositeWindow::compositeWindow(p->d->stack[DESKTOP_LAYER]);
    }
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

bool MCompositeWindow::isAppWindow(bool include_transients)
{
    MCompositeManager *p = (MCompositeManager *) qApp;
    
    if (!include_transients && p->d->getLastVisibleParent(pc))
        return false;
    
    if (!pc->isOverrideRedirect() &&
            (pc->windowTypeAtom() == ATOM(_NET_WM_WINDOW_TYPE_NORMAL) ||
             pc->windowTypeAtom() == ATOM(_KDE_NET_WM_WINDOW_TYPE_OVERRIDE) ||
             /* non-modal, non-transient dialogs behave like applications */
             (pc->windowTypeAtom() == ATOM(_NET_WM_WINDOW_TYPE_DIALOG) &&
              (pc->netWmState().indexOf(ATOM(_NET_WM_STATE_MODAL)) == -1)))
        && !pc->isDecorator())
        return true;
    return false;
}

QPainterPath MCompositeWindow::shape() const
{    
    QPainterPath path;
    const QRegion &shape = propertyCache()->shapeRegion();
    if (QRegion(boundingRect().toRect()).subtracted(shape).isEmpty())
        path.addRect(boundingRect());
    else
        path.addRegion(shape);
    return path;
}

Window MCompositeWindow::lastVisibleParent() const
{
    MCompositeManager *p = (MCompositeManager *) qApp;
    return p->d->getLastVisibleParent(propertyCache());
}

int MCompositeWindow::indexInStack() const
{
    MCompositeManager *p = (MCompositeManager *) qApp;
    return p->d->stacking_list.indexOf(window());
}
