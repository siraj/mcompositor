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
#include <X11/extensions/Xfixes.h>

//#define GL_GLEXT_PROTOTYPES

#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

static PFNEGLCREATEIMAGEKHRPROC eglCreateImageKHR = 0; 
static PFNEGLDESTROYIMAGEKHRPROC eglDestroyImageKHR = 0; 
PFNGLEGLIMAGETARGETTEXTURE2DOESPROC glEGLImageTargetTexture2DOES = 0;
static EGLint attribs[] = { EGL_IMAGE_PRESERVED_KHR, EGL_TRUE, EGL_NONE }; 

class EglTextureManager
{
public:
    // TODO: this should be dynamic
    // use QCache instead like Qt's GL backend
    static const int sz = 20;

    EglTextureManager() {
        glGenTextures(sz, tex);
        for (int i = 0; i < sz; i++)
            textures.push_back(tex[i]);
    }

    ~EglTextureManager() {
        glDeleteTextures(sz, tex);
    }

    GLuint getTexture() {
        if (textures.empty()) {
            qWarning("Empty texture stack");
            return 0;
        }
        GLuint ret = textures.back();
        textures.pop_back();
        return ret;
    }

    void closeTexture(GLuint texture) {
        // clear this texture
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 0, 0, 0, GL_RGBA,
                     GL_UNSIGNED_BYTE, 0);
        textures.push_back(texture);
    }

    GLuint tex[sz+1];
    std::vector<GLuint> textures;
};

class EglResourceManager
{
public:
    EglResourceManager()
        : has_tfp(false) {
        if (!dpy)
            dpy = eglGetDisplay(EGLNativeDisplayType(QX11Info::display()));

        QString exts = QLatin1String(eglQueryString(dpy, EGL_EXTENSIONS));
        if ((exts.contains("EGL_KHR_image") &&
             exts.contains("EGL_KHR_image_pixmap") &&
             exts.contains("EGL_KHR_gl_texture_2D_image")) || 1) {
            has_tfp = true;
            eglCreateImageKHR = (PFNEGLCREATEIMAGEKHRPROC) eglGetProcAddress("eglCreateImageKHR");
            eglDestroyImageKHR = (PFNEGLDESTROYIMAGEKHRPROC) eglGetProcAddress("eglDestroyImageKHR"); 
            glEGLImageTargetTexture2DOES = (PFNGLEGLIMAGETARGETTEXTURE2DOESPROC) eglGetProcAddress("glEGLImageTargetTexture2DOES"); 
        } else {
            qCritical() << "EGL extensions:" << exts;
            qFatal("no EGL tfp support, aborting\n");
        }
        texman = new EglTextureManager();
    }

    bool texturePixmapSupport() {
        return has_tfp;
    }

    EglTextureManager *texman;
    static EGLConfig config;
    static EGLConfig configAlpha;
    static EGLDisplay dpy;

    bool has_tfp;
};

EglResourceManager *MTexturePixmapPrivate::eglresource = 0;
EGLConfig EglResourceManager::config = 0;
EGLConfig EglResourceManager::configAlpha = 0;
EGLDisplay EglResourceManager::dpy = 0;

void MTexturePixmapItem::init()
{
    if (isValid() && !propertyCache()->isMapped()) {
        qWarning("MTexturePixmapItem::%s(): Failed getting offscreen pixmap",
                 __func__);
        return;
    } else if (!isValid() || propertyCache()->isInputOnly())
        return;
    
    if (!d->eglresource)
        d->eglresource = new EglResourceManager();

    d->custom_tfp = !d->eglresource->texturePixmapSupport();
    if (d->custom_tfp) {
        initCustomTfp();
        return;
    }
    saveBackingStore();
    d->ctx->makeCurrent();
    d->egl_image = eglCreateImageKHR(d->eglresource->dpy, 0,
                                     EGL_NATIVE_PIXMAP_KHR,
                                     (EGLClientBuffer)d->windowp,
                                     attribs);
    if (d->egl_image == EGL_NO_IMAGE_KHR) {
        qWarning("MTexturePixmapItem::%s(): Cannot create EGL image: 0x%x",
                 __func__, eglGetError());
        return;
    }
    
    d->textureId = d->eglresource->texman->getTexture();
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, d->textureId);
    
    if (d->egl_image != EGL_NO_IMAGE_KHR)
        glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, d->egl_image);
    
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

MTexturePixmapItem::MTexturePixmapItem(Window window, MWindowPropertyCache *mpc,
                                       QGLWidget *glwidget,
                                       QGraphicsItem* parent)
    : MCompositeWindow(window, mpc, parent),
      d(new MTexturePixmapPrivate(window, this))
{
    if (!d->ctx)
        d->ctx = const_cast<QGLContext *>(glwidget->context());
    init();
}

void MTexturePixmapItem::saveBackingStore(bool renew)
{
    d->saveBackingStore(renew);
}

void MTexturePixmapItem::rebindPixmap()
{
    if (d->egl_image != EGL_NO_IMAGE_KHR) {
        eglDestroyImageKHR(d->eglresource->dpy, d->egl_image);
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    if (!d->windowp) {
        d->egl_image = EGL_NO_IMAGE_KHR;
        return;
    }
    
    d->ctx->makeCurrent();
    d->egl_image = eglCreateImageKHR(d->eglresource->dpy, 0,
                                     EGL_NATIVE_PIXMAP_KHR,
                                     (EGLClientBuffer)d->windowp,
                                     attribs);
    if (d->egl_image == EGL_NO_IMAGE_KHR)
        qWarning("MTexturePixmapItem::%s(): Cannot create EGL image: 0x%x",
                 __func__, eglGetError());
    
    glBindTexture(GL_TEXTURE_2D, d->textureId);
    if (d->egl_image != EGL_NO_IMAGE_KHR)
        glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, d->egl_image);
}

void MTexturePixmapItem::enableDirectFbRendering()
{
    if (d->item->propertyCache())
        d->item->propertyCache()->damageTracking(false);

    if (d->direct_fb_render && d->egl_image == EGL_NO_IMAGE_KHR)
        return;

    d->direct_fb_render = true;

    glBindTexture(GL_TEXTURE_2D, 0);
    eglDestroyImageKHR(d->eglresource->dpy, d->egl_image);
    d->egl_image = EGL_NO_IMAGE_KHR;
    if (d->windowp) {
        XFreePixmap(QX11Info::display(), d->windowp);
        d->windowp = 0;
    }
    XCompositeUnredirectWindow(QX11Info::display(), window(),
                               CompositeRedirectManual);
}

void MTexturePixmapItem::enableRedirectedRendering()
{
    if (d->item->propertyCache())
        d->item->propertyCache()->damageTracking(true);

    if (!d->direct_fb_render && d->egl_image != EGL_NO_IMAGE_KHR)
        return;

    d->direct_fb_render = false;
    XCompositeRedirectWindow(QX11Info::display(), window(),
                             CompositeRedirectManual);
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
    d->ctextureId = d->eglresource->texman->getTexture();

    glBindTexture(GL_TEXTURE_2D, d->ctextureId);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
}

void MTexturePixmapItem::cleanup()
{    
    EGLImageKHR egl_image =  d->egl_image;
    EGLDisplay dpy = d->eglresource->dpy;
    eglDestroyImageKHR(dpy, egl_image);
    d->egl_image = EGL_NO_IMAGE_KHR;

    if (!d->custom_tfp)
        d->eglresource->texman->closeTexture(d->textureId);
    else
        d->eglresource->texman->closeTexture(d->ctextureId);

    // Work-around for crashes on some versions of below Qt 4.6
#if (QT_VERSION < 0x040600)
    eglMakeCurrent(d->eglresource->dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
#endif

    XFreePixmap(QX11Info::display(), d->windowp);
}

void MTexturePixmapItem::updateWindowPixmap(XRectangle *rects, int num)
{
    if (hasTransitioningWindow() || d->direct_fb_render || !windowVisible()
        || propertyCache()->isInputOnly())
        return;

    QRegion r;
    for (int i = 0; i < num; ++i)
        r += QRegion(rects[i].x, rects[i].y, rects[i].width, rects[i].height);
    d->damageRegion = r;

    // Our very own custom texture from pixmap
    if (d->custom_tfp) {
        QPixmap qp = QPixmap::fromX11Pixmap(d->windowp);

        QImage img = d->glwidget->convertToGLFormat(qp.toImage());
        glBindTexture(GL_TEXTURE_2D, d->ctextureId);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, img.width(), img.height(), 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, img.bits());
        update();
    } else {
        if (d->egl_image == EGL_NO_IMAGE_KHR)
            saveBackingStore(true);
        d->glwidget->update();
    }
}

void MTexturePixmapItem::paint(QPainter *painter,
                                 const QStyleOptionGraphicsItem *option,
                                 QWidget *widget)
{
    Q_UNUSED(option)
    Q_UNUSED(widget)

    if (d->direct_fb_render) {
        glBindTexture(GL_TEXTURE_2D, 0);
        return;
    }

#if QT_VERSION < 0x040600
    if (painter->paintEngine()->type()  != QPaintEngine::OpenGL)
        return;
#else
    if (painter->paintEngine()->type()  != QPaintEngine::OpenGL2) {
        return;
    }
#endif
    QGLWidget *gl = (QGLWidget *) painter->device();
    if (!d->ctx)
        d->ctx = const_cast<QGLContext *>(gl->context());

    if (propertyCache()->hasAlpha() || (opacity() < 1.0f && !dimmedEffect()) ) {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    } 
    glBindTexture(GL_TEXTURE_2D, d->custom_tfp ? d->ctextureId : d->textureId);

    const QRegion &shape = propertyCache()->shapeRegion();
    // FIXME: not optimal. probably would be better to replace with 
    // eglSwapBuffersRegionNOK()

    bool shape_on = !QRegion(boundingRect().toRect()).subtracted(shape).isEmpty();
    bool scissor_on = d->damageRegion.numRects() > 1 || shape_on;
    
    if (scissor_on)
        glEnable(GL_SCISSOR_TEST);
    
    // Damage regions taking precedence over shape rects 
    if (d->damageRegion.numRects() > 1) {
        for (int i = 0; i < d->damageRegion.numRects(); ++i) {
            glScissor(d->damageRegion.rects().at(i).x(),
                      d->brect.height() -
                      (d->damageRegion.rects().at(i).y() +
                       d->damageRegion.rects().at(i).height()),
                      d->damageRegion.rects().at(i).width(),
                      d->damageRegion.rects().at(i).height());
            d->drawTexture(painter->combinedTransform(), boundingRect(), opacity());        
        }
    } else if (shape_on) {
        // draw a shaped window using glScissor
        for (int i = 0; i < shape.numRects(); ++i) {
            glScissor(shape.rects().at(i).x(),
                      d->brect.height() -
                      (shape.rects().at(i).y() +
                       shape.rects().at(i).height()),
                      shape.rects().at(i).width(),
                      shape.rects().at(i).height());
            d->drawTexture(painter->combinedTransform(),
                           boundingRect(), opacity());
        }
    } else
        d->drawTexture(painter->combinedTransform(), boundingRect(), opacity());
    
    if (scissor_on)
        glDisable(GL_SCISSOR_TEST);

    // Explicitly disable blending. for some reason, the latest drivers
    // still has blending left-over even if we call glDisable(GL_BLEND)
    glBlendFunc(GL_ONE, GL_ZERO);
    glDisable(GL_BLEND);
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

void MTexturePixmapItem::clearTexture()
{
    glBindTexture(GL_TEXTURE_2D, d->custom_tfp ? d->ctextureId : d->textureId);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 0, 0, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, 0);

    glClearColor(0.0, 0.0, 0.0, 0.0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}
