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

#ifndef DUICOMPOSITEMANAGER_P_H
#define DUICOMPOSITEMANAGER_P_H

#include <QObject>
#include <QHash>
#include <QPixmap>

#include <X11/Xutil.h>
#include <X11/Xlib.h>
#include <X11/extensions/Xdamage.h>

class QGraphicsScene;
class QGLWidget;

class MCompositeScene;
class MSimpleWindowFrame;
class MCompAtoms;
class MCompositeWindow;
class MDeviceState;
class MWindowPropertyCache;

enum {
    INPUT_LAYER = 0,
    DOCK_LAYER,
    SYSTEM_LAYER,
    APPLICATION_LAYER,
    DESKTOP_LAYER,
    TOTAL_LAYERS
};

/*!
 * Internal implementation of MCompositeManager
 */

class MCompositeManagerPrivate: public QObject
{
    Q_OBJECT
public:
    enum StackPosition {
        STACK_BOTTOM = 0,
        STACK_TOP
    };
    enum ForcingLevel {
        NO_FORCED = 0,
        FORCED
    };

    MCompositeManagerPrivate(QObject *p);
    ~MCompositeManagerPrivate();

    static Window parentWindow(Window child);
    MCompositeWindow *bindWindow(Window w, XWindowAttributes *attr = 0);
    QGraphicsScene *scene();

    void prepare();
    void activateWindow(Window w, Time timestamp,
		        bool disableCompositing = true);
    void updateWinList();
    void setWindowState(Window , int);
    void setWindowDebugProperties(Window w);
    void positionWindow(Window w, StackPosition pos);
    void addItem(MCompositeWindow *item);
    void damageEvent(XDamageNotifyEvent *);
    void destroyEvent(XDestroyWindowEvent *);
    void propertyEvent(XPropertyEvent *);
    void unmapEvent(XUnmapEvent *);
    void configureEvent(XConfigureEvent *);
    void configureRequestEvent(XConfigureRequestEvent *);
    void mapEvent(XMapEvent *);
    void mapRequestEvent(XMapRequestEvent *);
    void rootMessageEvent(XClientMessageEvent *);
    void clientMessageEvent(XClientMessageEvent *);
    void keyEvent(XKeyEvent*);
    
    void redirectWindows();
    void mapOverlayWindow();
    void enableRedirection();
    void setExposeDesktop(bool exposed);
    void checkStacking(bool force_visibility_check,
                       Time timestamp = CurrentTime);
    void checkInputFocus(Time timestamp = CurrentTime);
    void configureWindow(MCompositeWindow *cw, XConfigureRequestEvent *e);

    Window getTopmostApp(int *index_in_stacking_list = 0,
                         Window ignore_window = 0);
    Window getLastVisibleParent(MWindowPropertyCache *pc);

    bool possiblyUnredirectTopmostWindow();
    bool isRedirected(Window window);
    bool x11EventFilter(XEvent *event);
    bool removeWindow(Window w);
    bool needDecoration(Window w, MWindowPropertyCache *pc = 0);
    MCompositeWindow *getHighestDecorated();
    void roughSort();
    int transiencyRelation(MCompositeWindow *cw_a, MCompositeWindow *cw_b);

    MCompositeScene *watch;
    Window localwin;
    Window xoverlay;
    Window prev_focus;

    static Window stack[TOTAL_LAYERS];

    MCompAtoms *atom;
    QGLWidget *glwidget;

    QList<Window> stacking_list;
    QList<Window> windows_as_mapped;

    QHash<Window, MCompositeWindow *> windows;
    struct FrameData {
        FrameData(): frame(0), parentWindow(0), mapped(false) {}
        MSimpleWindowFrame *frame;
        Window                parentWindow;
        bool mapped;
    };
    QHash<Window, FrameData> framed_windows;
    QHash<Window, QList<XConfigureRequestEvent*> > configure_reqs;
    QHash<Window, MWindowPropertyCache*> prop_caches;
    QRegion dock_region;

    int damage_event;
    int damage_error;

    bool compositing;
    bool overlay_mapped;
    MDeviceState *device_state;

signals:
    void inputEnabled();
    void compositingEnabled();

public slots:

    void gotHungWindow(MCompositeWindow *window);
    void sendPing(MCompositeWindow *window);
    void enableInput();
    void disableInput();
    void enableCompositing(bool forced = false);
    void disableCompositing(ForcingLevel forced = NO_FORCED);
    void showLaunchIndicator(int timeout);
    void hideLaunchIndicator();
    void iconifyOnLower(MCompositeWindow *window);
    void raiseOnRestore(MCompositeWindow *window);
    void onDesktopActivated(MCompositeWindow*);
    void exposeDesktop();
    void enablePaintedCompositing();
    void exposeSwitcher();
    
    void displayOff(bool display_off);
    void callOngoing(bool call_ongoing);
};

#endif
