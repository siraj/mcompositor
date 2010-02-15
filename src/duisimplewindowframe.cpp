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

#include "duisimplewindowframe.h"

#include <QApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QPalette>
#include <QLinearGradient>
#include <QDesktopWidget>
#include <QX11Info>
#include <QtDebug>
#include <QPushButton>

#include <X11/Xutil.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xmd.h>

QPixmap *DuiSimpleWindowFrame::homepix  = 0;
QPixmap *DuiSimpleWindowFrame::closepix = 0;

class SystemButton: public QLabel
{
    Q_OBJECT
public:
    SystemButton(const QPixmap &iconId, QWidget *p = 0)
        : QLabel(p) {
        setPixmap(iconId);
    }

    virtual void mouseReleaseEvent(QMouseEvent *e) {
        emit clicked();
        QLabel::mouseReleaseEvent(e);
    }

signals:
    void clicked();
};

DuiSimpleWindowFrame::DuiSimpleWindowFrame(Qt::HANDLE w)
    : QWidget(0),
      managed_window(w)
{
    // use failsafe images
    if (!homepix)
        homepix  = new QPixmap(":/images/homepix.png");
    if (!closepix)
        closepix = new QPixmap(":/images/closepix.png");

    // construct toolbar
    QHBoxLayout *l = new QHBoxLayout();
    l->setContentsMargins(0, 0, 0, 0);
    homesb = new SystemButton(*homepix, this);
    l->addWidget(homesb);
    l->addStretch();
    SystemButton *closesb = new SystemButton(*closepix, this);
    l->addWidget(closesb);

    connect(homesb, SIGNAL(clicked()), SLOT(minimizeWindow()));
    connect(closesb, SIGNAL(clicked()), SLOT(closeWindow()));

    QPalette palette;
    palette.setColor(QPalette::Background, Qt::black);
    setAutoFillBackground(true);
    setPalette(palette);

    QVBoxLayout *vb = new QVBoxLayout();
    vb->setContentsMargins(0, 0, 0, 0);
    window_area = new QWidget(this);
    vb->addLayout(l);
    vb->addWidget(window_area);
    setLayout(vb);

    // give suggested size for new windows
    QSize minimum = l->minimumSize();
    int wheight = QApplication::desktop()->height() - minimum.height();
    window_size = QSize(QApplication::desktop()->width(), wheight - 10);
    window_area->setMinimumSize(window_size);

    // prevent leaks for now. decorator handles this anyway
    // XTextProperty p;
//     if(XGetWMName(QX11Info::display(), w, &p))
//         XSetWMName(QX11Info::display(), winId(), &p);

    // resize to fit the whole screen
    resize(QApplication::desktop()->size());
}

DuiSimpleWindowFrame::~DuiSimpleWindowFrame()
{
    // QObject frees allocated resources
}

void DuiSimpleWindowFrame::setDialogDecoration(bool dialogDecor)
{
    dialogDecor ? homesb->hide() : homesb->show();
}

const QSize &DuiSimpleWindowFrame::suggestedWindowSize() const
{
    return window_size;
}

Qt::HANDLE DuiSimpleWindowFrame::windowArea()
{
    return window_area->winId();
}

Qt::HANDLE DuiSimpleWindowFrame::managedWindow()
{
    return managed_window;
}

void DuiSimpleWindowFrame::minimizeWindow()
{
    XEvent e;
    memset(&e, 0, sizeof(e));

    e.xclient.type = ClientMessage;
    e.xclient.message_type = XInternAtom(QX11Info::display(), "WM_CHANGE_STATE",
                                         False);
    e.xclient.display = QX11Info::display();
    e.xclient.window = managed_window;
    e.xclient.format = 32;
    e.xclient.data.l[0] = IconicState;
    e.xclient.data.l[1] = 0;
    e.xclient.data.l[2] = 0;
    e.xclient.data.l[3] = 0;
    e.xclient.data.l[4] = 0;
    XSendEvent(QX11Info::display(), QX11Info::appRootWindow(),
               False, (SubstructureNotifyMask | SubstructureRedirectMask), &e);

    XSync(QX11Info::display(), FALSE);
}

void DuiSimpleWindowFrame::closeWindow()
{
    XEvent e;
    memset(&e, 0, sizeof(e));

    e.xclient.type         = ClientMessage;
    e.xclient.window       = managed_window;
    e.xclient.message_type = XInternAtom(QX11Info::display(),
                                         "_NET_CLOSE_WINDOW", False);
    e.xclient.format       = 32;
    e.xclient.data.l[0]    = CurrentTime;
    e.xclient.data.l[1]    = QX11Info::appRootWindow();
    XSendEvent(QX11Info::display(), QX11Info::appRootWindow(),
               False, SubstructureRedirectMask, &e);

    XSync(QX11Info::display(), FALSE);
}

void DuiSimpleWindowFrame::setParentFrame(Qt::HANDLE frame)
{
    parent_frame = frame;
}

#include "duisimplewindowframe.moc"
