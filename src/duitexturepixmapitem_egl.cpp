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

#include "duitexturepixmapitem.h"
#include "duitexturepixmapitem_p.h"

#include <QPainterPath>
#include <QRect>
#include <QGLContext>
#include <QX11Info>
#include <vector>

#include <X11/Xlib.h>
#include <X11/extensions/Xcomposite.h>
#include <X11/extensions/Xrender.h>
#include <X11/extensions/Xfixes.h>

#include <GLES2/gl2.h>

/* Emulator doesn't support these configurations:
   - off screen pixmap
   - bind to texture
 */

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
        if (exts.contains("EGL_NOKIA_texture_from_pixmap") ||
                exts.contains("EGL_EXT_texture_from_pixmap"))
            has_tfp = true;
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

EglResourceManager *DuiTexturePixmapPrivate::eglresource = 0;
EGLConfig EglResourceManager::config = 0;
EGLConfig EglResourceManager::configAlpha = 0;
EGLDisplay EglResourceManager::dpy = 0;

void DuiTexturePixmapItem::init()
{
    if (!d->viewable) {
        qWarning("DuiTexturePixmapItem::init(): Failed getting offscreen pixmap");
        return;
    }

    if (!d->eglresource)
        d->eglresource = new EglResourceManager();

    d->glpixmap = EGL_NO_SURFACE;
    d->custom_tfp = !d->eglresource->texturePixmapSupport();
    if (d->custom_tfp) {
        initCustomTfp();
        return;
    }
    saveBackingStore();
    EGLint pixmapAttribs[] = {
        EGL_TEXTURE_TARGET, EGL_TEXTURE_2D,
        EGL_TEXTURE_FORMAT, d->has_alpha ? EGL_TEXTURE_RGBA : EGL_TEXTURE_RGB,
        EGL_NONE
    };

    // We cache the config so we dont have to loop thru it again when
    // rebinding pixmaps
    if (!d->eglresource->config || !d->eglresource->configAlpha) {
        EGLint pixmap_config[] = {
            EGL_SURFACE_TYPE,       EGL_PIXMAP_BIT,
            EGL_RENDERABLE_TYPE,    EGL_OPENGL_ES2_BIT,
            EGL_DEPTH_SIZE,         0,
            d->has_alpha ? EGL_BIND_TO_TEXTURE_RGBA : EGL_BIND_TO_TEXTURE_RGB, EGL_TRUE,
            EGL_NONE
        };
        EGLint ecfgs = 0;
        EGLConfig configs[20];
        if (!eglChooseConfig(d->eglresource->dpy, pixmap_config, configs,
                             20, &ecfgs) || !ecfgs) {
            qWarning("No appropriate EGL configuration for texture from "
                     "pixmap! Falling back to custom texture to pixmap ");
            d->custom_tfp = true;
            initCustomTfp();
            return;
        }

        // find best matching configuration for offscreen pixmap
        for (EGLint i = 0; i < ecfgs; i++) {
            d->glpixmap = eglCreatePixmapSurface(d->eglresource->dpy, configs[i],
                                                 (EGLNativePixmapType) d->windowp,
                                                 pixmapAttribs);
            if (d->glpixmap == EGL_NO_SURFACE)
                continue;
            else {
                if (d->has_alpha)
                    d->eglresource->configAlpha = configs[i];
                else
                    d->eglresource->config = configs[i];
                break;
            }
        }
    } else
        d->glpixmap = eglCreatePixmapSurface(d->eglresource->dpy,
                                             d->has_alpha ? d->eglresource->configAlpha : d->eglresource->config,
                                             (EGLNativePixmapType) d->windowp,
                                             pixmapAttribs);
    if (d->glpixmap == EGL_NO_SURFACE)
        qWarning("DuiTexturePixmapItem: Cannot create EGL surface: 0x%x",
                 eglGetError());

    d->textureId = d->eglresource->texman->getTexture();
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, d->textureId);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

DuiTexturePixmapItem::DuiTexturePixmapItem(Window window, QGLWidget *glwidget, QGraphicsItem *parent)
    : DuiCompositeWindow(window, parent),
      d(new DuiTexturePixmapPrivate(window, glwidget, this))
{
    init();
}

void DuiTexturePixmapItem::saveBackingStore(bool renew)
{
    d->saveBackingStore(renew);
}

void DuiTexturePixmapItem::rebindPixmap()
{
    EGLint pixmapAttribs[] = {
        EGL_TEXTURE_TARGET, EGL_TEXTURE_2D,
        EGL_TEXTURE_FORMAT, d->has_alpha ? EGL_TEXTURE_RGBA : EGL_TEXTURE_RGB,
        EGL_NONE
    };

    if (d->glpixmap != EGL_NO_SURFACE) {
        if (eglReleaseTexImage(d->eglresource->dpy, d->glpixmap, EGL_BACK_BUFFER) == EGL_FALSE)
            qWarning("Destroy surface: Cant release bound texture");
        glBindTexture(0, 0);
        eglSwapBuffers(d->eglresource->dpy, d->glpixmap);
        eglDestroySurface(d->eglresource->dpy, d->glpixmap);
    }
    d->glpixmap = eglCreatePixmapSurface(d->eglresource->dpy, d->has_alpha ? d->eglresource->configAlpha :
                                         d->eglresource->config,
                                         (EGLNativePixmapType) d->windowp,
                                         pixmapAttribs);
    if (d->glpixmap == EGL_NO_SURFACE)
        qWarning("DuiTexturePixmapItem: Cannot renew EGL surface: 0x%x",
                 eglGetError());
}

void DuiTexturePixmapItem::enableDirectFbRendering()
{
    if (d->direct_fb_render || d->glpixmap == EGL_NO_SURFACE)
        return;

    d->direct_fb_render = true;
    // Just free the off-screen surface but re-use the
    // existing texture id, so don't delete it yet.
    if (eglReleaseTexImage(d->eglresource->dpy, d->glpixmap, EGL_BACK_BUFFER) == EGL_FALSE)
        qWarning("DuiTexturePixmapItem::enableDirectFbRender: "
                 "Cant release bound texture: 0x%x", eglGetError());

    glBindTexture(0, 0);
    eglSwapBuffers(d->eglresource->dpy, d->glpixmap);
    eglDestroySurface(d->eglresource->dpy, d->glpixmap);
    d->glpixmap = EGL_NO_SURFACE;
    if (d->windowp) {
        XFreePixmap(QX11Info::display(), d->windowp);
        d->windowp = 0;
    }
    XCompositeUnredirectWindow(QX11Info::display(), window(),
                               CompositeRedirectManual);
    XSync(QX11Info::display(), FALSE);
}

void DuiTexturePixmapItem::enableRedirectedRendering()
{
    if (!d->direct_fb_render || d->glpixmap != EGL_NO_SURFACE)
        return;

    d->direct_fb_render = false;
    XCompositeRedirectWindow(QX11Info::display(), window(),
                             CompositeRedirectManual);
    XSync(QX11Info::display(), FALSE);
    saveBackingStore(true);
    updateWindowPixmap();
}

bool DuiTexturePixmapItem::isDirectRendered() const
{
    return d->direct_fb_render;
}

DuiTexturePixmapItem::~DuiTexturePixmapItem()
{
    cleanup();
    delete d;
}

void DuiTexturePixmapItem::initCustomTfp()
{
    d->ctextureId = d->eglresource->texman->getTexture();

    glBindTexture(GL_TEXTURE_2D, d->ctextureId);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
}

void DuiTexturePixmapItem::cleanup()
{
    eglDestroySurface(d->eglresource->dpy, d->glpixmap);
    d->glpixmap = EGL_NO_SURFACE;
    XSync(QX11Info::display(), FALSE);

    if (!d->custom_tfp)
        d->eglresource->texman->closeTexture(d->textureId);
    else
        d->eglresource->texman->closeTexture(d->ctextureId);

#if (QT_VERSION < 0x040600)
    eglMakeCurrent(d->eglresource->dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
#endif

    XFreePixmap(QX11Info::display(), d->windowp);
}

void DuiTexturePixmapItem::updateWindowPixmap(XRectangle *rects, int num)
{
    if (isTransitioning() || d->direct_fb_render || !windowVisible())
        return;

    // Our very own custom texture from pixmap
    if (d->custom_tfp) {
        QPixmap qp = QPixmap::fromX11Pixmap(d->windowp);

        QImage img = d->glwidget->convertToGLFormat(qp.toImage());
        glBindTexture(GL_TEXTURE_2D, d->ctextureId);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, img.width(), img.height(), 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, img.bits());
        update();
    } else {
        if (d->glpixmap == EGL_NO_SURFACE)
            saveBackingStore(true);

        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, d->textureId);
        bool valid = true;

        if (eglReleaseTexImage(d->eglresource->dpy, d->glpixmap, EGL_BACK_BUFFER) == EGL_FALSE) {
            qWarning("Update: Cant release bound texture 0x%x",
                     eglGetError());
            valid = false;
        }
        if (eglBindTexImage(d->eglresource->dpy, d->glpixmap, EGL_BACK_BUFFER) == EGL_FALSE) {
            qWarning("Update: Can't bind EGL texture to pixmap: 0x%x",
                     eglGetError());
            valid = false;
        }
        if (!valid)
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 0, 0, 0, GL_RGBA,
                         GL_UNSIGNED_BYTE, 0);
        else {
            d->enable_scissor = true;
            if (num <= 1) {
                d->enable_scissor = false;
                d->damage_rect.x      = d->brect.x();
                d->damage_rect.y      = d->brect.y();
                d->damage_rect.height = d->brect.height();
                d->damage_rect.width  = d->brect.width();
                d->glwidget->update();
                return;
            }
            for (register int i = 0; i < num; i++) {
                d->damage_rect = rects[i];
                d->glwidget->update();
            }
        }
    }
}

void DuiTexturePixmapItem::paint(QPainter *painter,
                                 const QStyleOptionGraphicsItem *option,
                                 QWidget *widget)
{
    Q_UNUSED(option)
    Q_UNUSED(widget)

    if (d->direct_fb_render) {
        glBindTexture(0, 0);
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

    if (d->has_alpha || opacity() < 1.0f) {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    }
    glBindTexture(GL_TEXTURE_2D, d->custom_tfp ? d->ctextureId : d->textureId);

    if (d->enable_scissor) {
        glEnable(GL_SCISSOR_TEST);
        glScissor(d->damage_rect.x,
                  d->brect.height() - (d->damage_rect.y + d->damage_rect.height),
                  d->damage_rect.width, d->damage_rect.height);
    }

    d->drawTexture(painter->combinedTransform(), boundingRect(), opacity());

    // Explicitly disable blending. for some reason, the latest drivers
    // still has blending left-over even if we call glDisable(GL_BLEND)
    glBlendFunc(GL_ONE, GL_ZERO);
    glDisable(GL_BLEND);
    if (d->enable_scissor)
        glDisable(GL_SCISSOR_TEST);
}

void DuiTexturePixmapItem::windowRaised()
{
    d->windowRaised();
}

void DuiTexturePixmapItem::resize(int w, int h)
{
    d->resize(w, h);
}

QSizeF DuiTexturePixmapItem::sizeHint(Qt::SizeHint, const QSizeF &) const
{
    return boundingRect().size();
}

QRectF DuiTexturePixmapItem::boundingRect() const
{
    return d->brect;
}

QPainterPath DuiTexturePixmapItem::shape() const
{
    QPainterPath path;
    path.addRect(boundingRect());
    return path;
}

bool DuiTexturePixmapItem::hasAlpha() const
{
    return d->has_alpha;
}

bool DuiTexturePixmapItem::isOverrideRedirect() const
{
    return d->override_redirect;
}

void DuiTexturePixmapItem::clearTexture()
{
    glBindTexture(GL_TEXTURE_2D, d->custom_tfp ? d->ctextureId : d->textureId);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 0, 0, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, 0);
}
