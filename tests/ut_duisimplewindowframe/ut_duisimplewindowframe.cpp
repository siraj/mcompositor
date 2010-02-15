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
#include "ut_duisimplewindowframe.h"

#include <X11/Xlib.h>

DuiApplication *app;
void Ut_DuiSimpleWindowFrame::initTestCase()
{
    int argc = 1;
    char *app_name = (char *) "./ut_duisimplewindowframe";
    app = new DuiApplication(argc, &app_name);
}

void Ut_DuiSimpleWindowFrame::cleanupTestCase()
{
    delete app;
}

void Ut_DuiSimpleWindowFrame::testConstructorInvalidInput()
{
    m_subject = new DuiSimpleWindowFrame(-1);

    if (m_subject)
        delete m_subject;
}

void Ut_DuiSimpleWindowFrame::testConstructorValidInput()
{
    // create simplewindowframe for X11 root window

    Display *dpy = QX11Info::display();

    m_subject = new DuiSimpleWindowFrame(DefaultRootWindow(dpy));

    QSize size = m_subject->window_size;

    int xres = ScreenOfDisplay(dpy, DefaultScreen(dpy))->width;
    int yres = ScreenOfDisplay(dpy, DefaultScreen(dpy))->height;

    // width should be the same as screen resolution, height
    // smaller as the titlebar takes some space

    QVERIFY(size.width() == xres);
    QVERIFY(size.height() < yres);

    delete m_subject;
}

QTEST_APPLESS_MAIN(Ut_DuiSimpleWindowFrame)
