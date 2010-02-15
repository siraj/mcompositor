/***************************************************************************
**
** Copyright (C) 2010 Nokia Corporation and/or its subsidiary(-ies).
** All rights reserved.
** Contact: Nokia Corporation (directui@nokia.com)
**
** This file is part of duicompositor.
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

#include <QX11Info>
#include <DuiApplication>
#include "ut_duicompositescene.h"

#include <X11/Xlib.h>
#include <X11/extensions/XTest.h>

// small helper class that will cause qFatal if
// mousepress event is received
class TestWindow : public QWidget
{
public:
    TestWindow() {
        setWindowFlags(Qt::FramelessWindowHint);
    }
protected:
    void mousePressEvent(QMouseEvent *event) {
        Q_UNUSED(event);
        qFatal("I do not like to receive mouse events");
    }
    void mouseReleaseEvent(QMouseEvent *event) {
        Q_UNUSED(event);
        qFatal("I do not like to receive mouse events");
    }
};

DuiApplication *app;
void Ut_DuiCompositeScene::initTestCase()
{
    int argc = 1;
    char *app_name = (char *) "./ut_duicompositescene";
    app = new DuiApplication(argc, &app_name);
}

void Ut_DuiCompositeScene::cleanupTestCase()
{
    delete app;
}

void Ut_DuiCompositeScene::init()
{
    m_subject = new DuiCompositeScene();
}

void Ut_DuiCompositeScene::cleanup()
{
    delete m_subject;
}

void Ut_DuiCompositeScene::testInputOverlay()
{
    // create X window, set input overlay, test event passing

    Window window;
    QRect geom;
    bool restoreInput = true;

    Display *dpy = QX11Info::display();
    int xres = ScreenOfDisplay(dpy, DefaultScreen(dpy))->width;
    int yres = ScreenOfDisplay(dpy, DefaultScreen(dpy))->height;

    // covers the whole screen
    geom = QRect(0, 0, xres, yres);

    // create window with Qt API and show it
    QWidget *win = new TestWindow();

    win->setGeometry(geom);
    win->show();

    while (QCoreApplication::hasPendingEvents())
        QCoreApplication::processEvents();

    XSync(dpy, false);

    window = win->effectiveWinId();

    // set input mask to the window to be empty (should not catch events)
    m_subject->setupOverlay(window, geom, restoreInput);

    XSync(dpy, false);

    // move mouse over window
    XTestFakeMotionEvent(dpy, DefaultScreen(dpy), 256, 256, 0);

    // emit mouse event to window and check that it does not hit it because of input mask
    XTestFakeButtonEvent(dpy, 1, true,  0);
    XTestFakeButtonEvent(dpy, 1, false, 10);

    XSync(dpy, false);

    while (QCoreApplication::hasPendingEvents())
        QCoreApplication::processEvents();

    // if we survive here without qFatal it means that test has succeeded

    delete win;
}

QTEST_APPLESS_MAIN(Ut_DuiCompositeScene)
