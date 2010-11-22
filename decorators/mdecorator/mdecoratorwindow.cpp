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

#include <QtDebug>

#include <MSceneManager>
#include <MScene>

#include <QApplication>
#include <QDesktopWidget>
#include <QX11Info>
#include <QGLFormat>
#include <QGLWidget>

#include "mdecoratorwindow.h"

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/Xmd.h>
#include <X11/extensions/Xfixes.h>
#include <X11/extensions/shape.h>

#include <mabstractdecorator.h>
#include <mdesktopentry.h>
#include <mbuttonmodel.h>

class MDecorator: public MAbstractDecorator
{
    Q_OBJECT
public:
    MDecorator(MDecoratorWindow *p)
        : MAbstractDecorator(p),
        decorwindow(p)
    {
    }

    ~MDecorator() {
    }

    virtual void manageEvent(Qt::HANDLE window)
    {
        XTextProperty p;
        QString title;

        if(XGetWMName(QX11Info::display(), window, &p)) {
            if (p.value) {
                title = (char*) p.value;
                XFree(p.value);
            }
        }
        decorwindow->managedWindowChanged(window);
        decorwindow->setInputRegion();
        setAvailableGeometry(decorwindow->availableClientRect());
        decorwindow->setWindowTitle(title);
    }

protected:
    virtual void activateEvent() {
    }

    virtual void showQueryDialog(bool visible) {
        decorwindow->showQueryDialog(visible);
    }

    virtual void setAutoRotation(bool mode)
    {
        Q_UNUSED(mode)
        // we follow the orientation of the topmost app
    }

    virtual void setOnlyStatusbar(bool mode) 
    {
        decorwindow->setOnlyStatusbar(mode);
        decorwindow->setInputRegion();
        setAvailableGeometry(decorwindow->availableClientRect());
    }

private:

    MDecoratorWindow *decorwindow;
};

#if 0
static QRect windowRectFromGraphicsItem(const QGraphicsView &view,
                                        const QGraphicsItem &item)
{
    return view.mapFromScene(
               item.mapToScene(
                   item.boundingRect()
               )
           ).boundingRect();
}
#endif

MDecoratorWindow::MDecoratorWindow(QWidget *parent)
    : MWindow(parent),
      messageBox(0),
      managed_window(0)
{
    locale.addTranslationPath(TRANSLATION_INSTALLDIR);
    locale.installTrCatalog("recovery");
    locale.setDefault(locale);

    onlyStatusbarAtom = XInternAtom(QX11Info::display(),
                                    "_MDECORATOR_ONLY_STATUSBAR", False);
    managedWindowAtom = XInternAtom(QX11Info::display(),
                                    "_MDECORATOR_MANAGED_WINDOW", False);

    homeButtonPanel = new MHomeButtonPanel();
    escapeButtonPanel = new MEscapeButtonPanel();
    navigationBar = new MNavigationBar();
    statusBar = new MStatusBar();

    connect(homeButtonPanel, SIGNAL(buttonClicked()), this,
            SIGNAL(homeClicked()));
    connect(escapeButtonPanel, SIGNAL(buttonClicked()), this,
            SIGNAL(escapeClicked()));

    sceneManager()->appearSceneWindowNow(statusBar);
    setOnlyStatusbar(false);
    requested_only_statusbar = false;

    d = new MDecorator(this);
    connect(this, SIGNAL(homeClicked()), d, SLOT(minimize()));
    connect(this, SIGNAL(escapeClicked()), d, SLOT(close()));
    connect(sceneManager(),
            SIGNAL(orientationChanged(M::Orientation)),
            this,
            SLOT(screenRotated(M::Orientation)));

    setTranslucentBackground(true); // for translucent messageBox
    setFocusPolicy(Qt::NoFocus);
    setSceneSize();
    setMDecoratorWindowProperty();

    setInputRegion();
    setProperty("followsCurrentApplicationWindowOrientation", true);
}

void MDecoratorWindow::yesButtonClicked()
{
    d->queryDialogAnswer(managed_window, true);
    showQueryDialog(false);
}

void MDecoratorWindow::noButtonClicked()
{
    d->queryDialogAnswer(managed_window, false);
    showQueryDialog(false);
}

void MDecoratorWindow::managedWindowChanged(Qt::HANDLE w)
{
    if (w != managed_window && messageBox)
        showQueryDialog(false);
    managed_window = w;
}

void MDecoratorWindow::setWindowTitle(const QString& title)
{
    navigationBar->setViewMenuDescription(title);
}

MDecoratorWindow::~MDecoratorWindow()
{
}

bool MDecoratorWindow::x11Event(XEvent *e)
{
    Atom actual;
    int format, result;
    unsigned long n, left;
    unsigned char *data = 0;
    if (e->type == PropertyNotify
        && ((XPropertyEvent*)e)->atom == onlyStatusbarAtom) {
        result = XGetWindowProperty(QX11Info::display(), winId(),
                                    onlyStatusbarAtom, 0, 1, False,
                                    XA_CARDINAL, &actual, &format,
                                    &n, &left, &data);
        if (result == Success && data) {
            bool val = *((long*)data);
            if (val != only_statusbar)
                d->RemoteSetOnlyStatusbar(val);
        }
        if (data)
            XFree(data);
        return true;
    } else if (e->type == PropertyNotify
               && ((XPropertyEvent*)e)->atom == managedWindowAtom) {
        result = XGetWindowProperty(QX11Info::display(), winId(),
                                    managedWindowAtom, 0, 1, False,
                                    XA_WINDOW, &actual, &format,
                                    &n, &left, &data);
        if (result == Success && data)
            d->RemoteSetManagedWinId(*((long*)data));
        if (data)
            XFree(data);
        return true;
    }
    return false;
}

void MDecoratorWindow::showQueryDialog(bool visible)
{
    if (visible && !messageBox) {
        char name[100];
        snprintf(name, 100, "window 0x%lx", managed_window);
        XClassHint cls = {0, 0};
        XGetClassHint(QX11Info::display(), managed_window, &cls);
        if (cls.res_name) {
            strncpy(name, cls.res_name, 100);
            MDesktopEntry de(QString("/usr/share/applications/")
                             + name + ".desktop");
            if (de.isValid() && !de.name().isEmpty())
                strncpy(name, de.name().toAscii().data(), 100);
        }
        if (cls.res_class) XFree(cls.res_class);
        if (cls.res_name) XFree(cls.res_name);

        XSetTransientForHint(QX11Info::display(), winId(), managed_window);
        requested_only_statusbar = only_statusbar;
        setOnlyStatusbar(true, true);
        messageBox = new MMessageBox(
                         qtTrId("qtn_reco_app_not_responding").replace("%1", name),
                         qtTrId("qtn_reco_close_app_question"),
                         M::NoStandardButton);
        MButtonModel *yes = messageBox->addButton(qtTrId("qtn_comm_command_yes"),
                                                  M::AcceptRole);
        MButtonModel *no = messageBox->addButton(qtTrId("qtn_comm_command_no"),
                                                 M::RejectRole);
        connect(yes, SIGNAL(clicked()), this, SLOT(yesButtonClicked()));
        connect(no, SIGNAL(clicked()), this, SLOT(noButtonClicked()));
        sceneManager()->appearSceneWindowNow(messageBox);
    } else if (!visible && messageBox) {
        XSetTransientForHint(QX11Info::display(), winId(), None);
        sceneManager()->disappearSceneWindowNow(messageBox);
        delete messageBox;
        messageBox = 0;
        setOnlyStatusbar(requested_only_statusbar);
    }
    setInputRegion();
    update();
}

void MDecoratorWindow::setOnlyStatusbar(bool mode, bool temporary)
{
    if (mode) {
        sceneManager()->disappearSceneWindowNow(navigationBar);
        sceneManager()->disappearSceneWindowNow(homeButtonPanel);
        sceneManager()->disappearSceneWindowNow(escapeButtonPanel);
    } else if (!messageBox) {
        sceneManager()->appearSceneWindowNow(navigationBar);
        sceneManager()->appearSceneWindowNow(homeButtonPanel);
        sceneManager()->appearSceneWindowNow(escapeButtonPanel);
    }
    if (!temporary)
        requested_only_statusbar = mode;
    only_statusbar = mode;
}

void MDecoratorWindow::screenRotated(const M::Orientation &orientation)
{
    Q_UNUSED(orientation);
    setInputRegion();
    d->setAvailableGeometry(availableClientRect());
}

// NOTE: this works with fullscreen-width rects only
QRect MDecoratorWindow::itemRectToScreenRect(const QRect& r)
{
    QRect rect;
    Display *dpy = QX11Info::display();
    int xres = ScreenOfDisplay(dpy, DefaultScreen(dpy))->width;
    int yres = ScreenOfDisplay(dpy, DefaultScreen(dpy))->height;
    switch (sceneManager()->orientationAngle()) {
    case 0:
            rect = r;
            break;
    case 90:
            rect = QRect(xres - r.height(), 0, r.height(), r.width());
            break;
    case 270:
            rect = QRect(0, 0, r.height(), r.width());
            break;
    case 180:
            rect = QRect(0, yres - r.height(), r.width(), r.height());
            break;
    default:
            break;
    }
    return rect;
}

static void set_shape(Window win, XRectangle *r, int n)
{
    static XRectangle *prev_rects = 0;
    static int prev_n = 0;
    if (prev_n != n ||
        (prev_rects && memcmp(prev_rects, r, sizeof(XRectangle[n])))) {
        Display *dpy = QX11Info::display();
        XserverRegion shapeRegion = XFixesCreateRegion(dpy, r, n);
        XShapeCombineRectangles(dpy, win, ShapeBounding, 0, 0, r, n,
                                ShapeSet, Unsorted);
        XFixesSetWindowShapeRegion(dpy, win, ShapeInput, 0, 0, shapeRegion);
        XFixesDestroyRegion(dpy, shapeRegion);
    } else
        return;
    if (prev_rects && prev_n != n) {
        delete[] prev_rects;
        prev_rects = new XRectangle[n];
    }
    prev_n = n;
    if (prev_rects)
        memcpy(prev_rects, r, sizeof(XRectangle[n]));
}

void MDecoratorWindow::setInputRegion()
{
    QRegion region;
    const QRect fs(QApplication::desktop()->screenGeometry());
    if (messageBox) {
        XRectangle rect;
        region = fs;
        rect.x = fs.x();
        rect.y = fs.y();
        rect.width = fs.width();
        rect.height = fs.height();
        availableRect = QRect(0, 0, 0, 0);
        set_shape(winId(), &rect, 1);
    } else {
        region += statusBar->geometry().toRect();
        if (!only_statusbar) {
            region += navigationBar->geometry().toRect();
            region += homeButtonPanel->geometry().toRect();
            region += escapeButtonPanel->geometry().toRect();
        }

        const QVector<QRect> rects = region.rects();
        QRegion screen_region;
        XRectangle *xrects = new XRectangle[rects.count()];
        int count = 0;
        for (int i = 0; i < rects.count(); ++i) {
            QRect r = itemRectToScreenRect(rects[i]);
            if (r.x() || (r.width() != fs.width() && r.width() != fs.height()))
                // only fullscreen-width components supported
                continue;
            screen_region += r;
            xrects[count].x = r.x();
            xrects[count].y = r.y();
            xrects[count].width = r.width();
            xrects[count].height = r.height();
            ++count;
        }
        set_shape(winId(), xrects, count);
        delete[] xrects;

        // NOTE: assumes that the available region is a single rectangle
        availableRect = QRegion(QRegion(fs) - screen_region).boundingRect();
    }
}

void MDecoratorWindow::setSceneSize()
{
    // always keep landscape size
    Display *dpy = QX11Info::display();
    int xres = ScreenOfDisplay(dpy, DefaultScreen(dpy))->width;
    int yres = ScreenOfDisplay(dpy, DefaultScreen(dpy))->height;
    scene()->setSceneRect(0, 0, xres, yres);
    setMinimumSize(xres, yres);
    setMaximumSize(xres, yres);
}

void MDecoratorWindow::setMDecoratorWindowProperty()
{

    long on = 1;

    XChangeProperty(QX11Info::display(), winId(),
                    XInternAtom(QX11Info::display(), "_MEEGOTOUCH_DECORATOR_WINDOW", False),
                    XA_CARDINAL,
                    32, PropModeReplace,
                    (unsigned char *) &on, 1);
}


const QRect MDecoratorWindow::availableClientRect() const
{
    return availableRect;
}

void MDecoratorWindow::closeEvent(QCloseEvent * event )
{
    // never close the decorator!
    return event->ignore();
}

#include "mdecoratorwindow.moc"
