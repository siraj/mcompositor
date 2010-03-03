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

#ifndef DUITEXTUREPIXMAPITEM_P_H
#define DUITEXTUREPIXMAPITEM_P_H

#include <QRect>
#include <QRegion>

#include <X11/extensions/Xdamage.h>

#ifdef GLES2_VERSION
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#define GLSurface EGLSurface
class EglResourceManager;
#elif DESKTOP_VERSION
#include <GL/glx.h>
#include <GL/gl.h>
#define GLSurface GLXPixmap
#endif

class QGLWidget;
class QGraphicsItem;
class DuiTexturePixmapItem;
class QGLContext;
class DuiGLResourceManager;

/*! Internal private implementation of DuiTexturePixmapItem 
  Warning! Interface here may change at any time!
 */
class DuiTexturePixmapPrivate
{
public:
    DuiTexturePixmapPrivate(Window window, QGLWidget *w, DuiTexturePixmapItem *item);
    ~DuiTexturePixmapPrivate();
    void init();
    void updateWindowPixmap(XRectangle *rects = 0, int num = 0);
    void saveBackingStore(bool renew = false);
    void clearTexture();
    bool isDirectRendered() const;
    bool hasAlpha() const;
    bool isOverrideRedirect() const;
    void resize(int w, int h);
    void windowRaised();
    void drawTexture(const QTransform& transform, const QRectF& drawRect, qreal opacity);
                
    QGLContext *ctx;
    QGLWidget *glwidget;
    Pixmap windowp;
    GLSurface glpixmap;
    GLuint textureId;
    GLuint ctextureId;
    static bool inverted_texture;
    bool custom_tfp;
    bool has_alpha;
    bool direct_fb_render;
    bool override_redirect;
    bool viewable;

    QRect brect;
    QRegion damageRegion;
    qreal angle;

    Damage damage_object;
    DuiTexturePixmapItem *item;

#ifdef GLES2_VERSION
    static EglResourceManager *eglresource;
#endif
    static DuiGLResourceManager* glresource;
};

#endif //DUITEXTUREPIXMAPITEM_P_H
