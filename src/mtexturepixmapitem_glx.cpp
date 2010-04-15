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

#define GLX_GLXEXT_PROTOTYPES 1
#define GLX_EXT_texture_from_pixmap 1

#include "mtexturepixmapitem.h"
#include "mtexturepixmapitem_p.h"

#include <QPainterPath>
#include <QRect>
#include <QGLContext>
#include <QX11Info>
#include <vector>

#include <X11/Xlib.h>
#include <X11/extensions/Xcomposite.h>
#include <X11/extensions/Xrender.h>

#include <GL/gl.h>
#include <GL/glu.h>
#include <GL/glx.h>
#include <GL/glxext.h>

#if defined(GLX_VERSION_1_3)
#ifndef GLX_TEXTURE_2D_BIT_EXT
#define GLX_TEXTURE_2D_BIT_EXT             0x00000002
#define GLX_TEXTURE_RECTANGLE_BIT_EXT      0x00000004
#define GLX_BIND_TO_TEXTURE_RGB_EXT        0x20D0
#define GLX_BIND_TO_TEXTURE_RGBA_EXT       0x20D1
#define GLX_BIND_TO_MIPMAP_TEXTURE_EXT     0x20D2
#define GLX_BIND_TO_TEXTURE_TARGETS_EXT    0x20D3
#define GLX_Y_INVERTED_EXT                 0x20D4
#define GLX_TEXTURE_FORMAT_EXT             0x20D5
#define GLX_TEXTURE_TARGET_EXT             0x20D6
#define GLX_MIPMAP_TEXTURE_EXT             0x20D7
#define GLX_TEXTURE_FORMAT_NONE_EXT        0x20D8
#define GLX_TEXTURE_FORMAT_RGB_EXT         0x20D9
#define GLX_TEXTURE_FORMAT_RGBA_EXT        0x20DA
#define GLX_TEXTURE_2D_EXT                 0x20DC
#define GLX_TEXTURE_RECTANGLE_EXT          0x20DD
#define GLX_FRONT_LEFT_EXT                 0x20DE
#endif

typedef void (*_glx_bind)(Display *, GLXDrawable, int , const int *);
typedef void (*_glx_release)(Display *, GLXDrawable, int);
static  _glx_bind glXBindTexImageEXT = 0;
static  _glx_release glXReleaseTexImageEXT = 0;
static GLXFBConfig config = 0;
static GLXFBConfig configAlpha = 0;

static bool hasTextureFromPixmap()
{
    static bool hasTfp = false;

    if (!hasTfp) {
        hasTfp = true;

        QList<QByteArray> exts = QByteArray(glXQueryExtensionsString(QX11Info::display(), QX11Info::appScreen())).split(' ');
        if (exts.contains("GLX_EXT_texture_from_pixmap")) {
            glXBindTexImageEXT = (_glx_bind) glXGetProcAddress((const GLubyte *)"glXBindTexImageEXT");
            glXReleaseTexImageEXT = (_glx_release) glXGetProcAddress((const GLubyte *)"glXReleaseTexImageEXT");
        }
    }
    return glXBindTexImageEXT && glXReleaseTexImageEXT;
}
#endif

void MTexturePixmapItem::init()
{
    if (!d->viewable) {
        qWarning("MTexturePixmapItem::init(): Failed getting offscreen pixmap");
        return;
    }

    d->glpixmap = 0;
    saveBackingStore();

    d->custom_tfp = !hasTextureFromPixmap();

    if (d->custom_tfp) {
        initCustomTfp();
        return;
    }
    static const int pixmapAttribs[] = {
        GLX_TEXTURE_TARGET_EXT, GLX_TEXTURE_2D_EXT,
        GLX_TEXTURE_FORMAT_EXT, d->has_alpha ? GLX_TEXTURE_FORMAT_RGBA_EXT : GLX_TEXTURE_FORMAT_RGB_EXT,
        None
    };

    if ((!d->has_alpha && !config) || (d->has_alpha && !configAlpha)) {
        Display *display = QX11Info::display();
        int screen = QX11Info::appScreen();

        int pixmap_config[] = {
            d->has_alpha ? GLX_BIND_TO_TEXTURE_RGBA_EXT : GLX_BIND_TO_TEXTURE_RGB_EXT, True,
            GLX_DRAWABLE_TYPE, GLX_PIXMAP_BIT,
            GLX_BIND_TO_TEXTURE_TARGETS_EXT, GLX_TEXTURE_2D_BIT_EXT,
            GLX_DOUBLEBUFFER, True,
            GLX_Y_INVERTED_EXT, GLX_DONT_CARE,
            None
        };

        int c = 0;
        GLXFBConfig *configs = 0;
        configs = glXChooseFBConfig(display, screen, pixmap_config, &c);
        if (!configs) {
            qWarning("No appropriate GLXFBconfig found!"
                     "Falling back to custom texture to pixmap ");
            d->custom_tfp = true;
            initCustomTfp();
            return;
        }

        int inverted;
        glXGetFBConfigAttrib(display, configs[0], GLX_Y_INVERTED_EXT, &inverted);
        if (d->has_alpha) {
            configAlpha = configs[0];
            d->inverted_texture = inverted ? true : false;
        } else {
            config = configs[0];
            d->inverted_texture = inverted ? true : false;
        }
        XFree(configs);
    }

    glGenTextures(1, &d->textureId);
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, d->textureId);
    d->glpixmap = glXCreatePixmap(QX11Info::display(), d->has_alpha ? configAlpha : config, d->windowp, pixmapAttribs);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glXBindTexImageEXT(QX11Info::display(), d->glpixmap, GLX_FRONT_LEFT_EXT, NULL);
}

MTexturePixmapItem::MTexturePixmapItem(Window window, QGLWidget *glwidget, QGraphicsItem *parent)
    : MCompositeWindow(window, parent),
      d(new MTexturePixmapPrivate(window, glwidget, this))
{
    init();
}

void MTexturePixmapItem::saveBackingStore(bool renew)
{
    d->saveBackingStore(renew);
}

void MTexturePixmapItem::rebindPixmap()
{
    static const int pixmapAttribs[] = {
        GLX_TEXTURE_TARGET_EXT, GLX_TEXTURE_2D_EXT,
        GLX_TEXTURE_FORMAT_EXT, d->has_alpha ? GLX_TEXTURE_FORMAT_RGBA_EXT : GLX_TEXTURE_FORMAT_RGB_EXT,
        None
    };

    if (!d->custom_tfp) {
        Display *display = QX11Info::display();
        glXReleaseTexImageEXT(display, d->glpixmap, GLX_FRONT_LEFT_EXT);
        glXDestroyPixmap(display, d->glpixmap);
        d->glpixmap = glXCreatePixmap(display, d->has_alpha ? configAlpha : config, d->windowp, pixmapAttribs);
        glBindTexture(GL_TEXTURE_2D, d->textureId);
        glXBindTexImageEXT(display, d->glpixmap, GLX_FRONT_LEFT_EXT, NULL);
    }
}

void MTexturePixmapItem::enableDirectFbRendering()
{
    d->damageTracking(false);

    if ((d->direct_fb_render || d->glpixmap == 0) && !d->custom_tfp)
        return;

    d->direct_fb_render = true;
    // Just free the off-screen surface but re-use the
    // existing texture id, so don't delete it yet.

    if (!d->custom_tfp) {
        glXReleaseTexImageEXT(QX11Info::display(), d->glpixmap, GLX_FRONT_LEFT_EXT);
        glXDestroyPixmap(QX11Info::display(), d->glpixmap);
        d->glpixmap = 0;
    }

    if (d->windowp) {
        XFreePixmap(QX11Info::display(), d->windowp);
        d->windowp = 0;
    }
    XCompositeUnredirectWindow(QX11Info::display(), window(),
                               CompositeRedirectManual);
    XSync(QX11Info::display(), FALSE);
}

void MTexturePixmapItem::enableRedirectedRendering()
{
    d->damageTracking(true);

    if ((!d->direct_fb_render || d->glpixmap != 0) && !d->custom_tfp)
        return;

    d->direct_fb_render = false;
    XCompositeRedirectWindow(QX11Info::display(), window(),
                             CompositeRedirectManual);
    XSync(QX11Info::display(), FALSE);
    saveBackingStore(true);
    updateWindowPixmap();
}

bool MTexturePixmapItem::isDirectRendered() const
{
    return d->direct_fb_render;
}

MTexturePixmapItem::~MTexturePixmapItem()
{
    cleanup();
    delete d;
}

void MTexturePixmapItem::initCustomTfp()
{
    glGenTextures(1, &d->ctextureId);

    glBindTexture(GL_TEXTURE_2D, d->ctextureId);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    d->inverted_texture = false;
}

void MTexturePixmapItem::cleanup()
{
    if (!d->custom_tfp) {
        glXReleaseTexImageEXT(QX11Info::display(), d->glpixmap, GLX_FRONT_LEFT_EXT);
        glXDestroyPixmap(QX11Info::display(), d->glpixmap);
        glDeleteTextures(1, &d->textureId);
    } else
        glDeleteTextures(1, &d->ctextureId);

    if (d->windowp)
        XFreePixmap(QX11Info::display(), d->windowp);
}

void MTexturePixmapItem::updateWindowPixmap(XRectangle *rects, int num)
{
    Q_UNUSED(rects);
    Q_UNUSED(num);

    if (isTransitioning() || d->direct_fb_render || !windowVisible())
        return;

    // Our very own custom texture from pixmap
    if (d->custom_tfp && d->windowp) {
        QPixmap qp = QPixmap::fromX11Pixmap(d->windowp);

        QImage img = d->glwidget->convertToGLFormat(qp.toImage());
        glBindTexture(GL_TEXTURE_2D, d->ctextureId);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, img.width(), img.height(), 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, img.bits());
    }
    update();
}

void MTexturePixmapItem::paint(QPainter *painter,
                                 const QStyleOptionGraphicsItem *option,
                                 QWidget *widget)
{
    Q_UNUSED(option)
    Q_UNUSED(widget)

#if QT_VERSION < 0x040600
    if (painter->paintEngine()->type()  != QPaintEngine::OpenGL)
        return;
#else
    if (painter->paintEngine()->type()  != QPaintEngine::OpenGL2) {
        return;
    }
#endif

#if (QT_VERSION >= 0x040600)
    painter->beginNativePainting();
#endif

    glEnable(GL_TEXTURE_2D);
    if (d->has_alpha || opacity() < 1.0f) {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glColor4f(1.0, 1.0, 1.0, opacity());
    }

    glBindTexture(GL_TEXTURE_2D, d->custom_tfp ? d->ctextureId : d->textureId);

    d->drawTexture(painter->combinedTransform(), boundingRect(), opacity());

#if (QT_VERSION >= 0x040600)
    painter->endNativePainting();
#endif

}

void MTexturePixmapItem::windowRaised()
{
    d->windowRaised();
}

void MTexturePixmapItem::resize(int w, int h)
{
    d->resize(w, h);
}

QSizeF MTexturePixmapItem::sizeHint(Qt::SizeHint, const QSizeF &) const
{
    return boundingRect().size();
}

QRectF MTexturePixmapItem::boundingRect() const
{
    return d->brect;
}

QPainterPath MTexturePixmapItem::shape() const
{
    QPainterPath path;
    path.addRect(boundingRect());
    return path;
}

bool MTexturePixmapItem::hasAlpha() const
{
    return d->has_alpha;
}

bool MTexturePixmapItem::isOverrideRedirect() const
{
    return d->override_redirect;
}

void MTexturePixmapItem::clearTexture()
{
    glBindTexture(GL_TEXTURE_2D, d->custom_tfp ? d->ctextureId : d->textureId);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 0, 0, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, 0);
}
