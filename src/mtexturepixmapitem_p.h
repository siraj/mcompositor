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

#ifndef DUITEXTUREPIXMAPITEM_P_H
#define DUITEXTUREPIXMAPITEM_P_H

#include <QObject>
#include <QRect>
#include <QRegion>
#include <QPointer>

#include <X11/extensions/Xdamage.h>

#ifdef GLES2_VERSION
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <EGL/eglext.h>
class EglResourceManager;
#elif DESKTOP_VERSION
#include <GL/glx.h>
#include <GL/gl.h>
#endif

class QGLWidget;
class QGraphicsItem;
class MTexturePixmapItem;
class QGLContext;
class QTransform;
class MGLResourceManager;
class MCompositeWindowShaderEffect;

/*! Internal private implementation of MTexturePixmapItem
  Warning! Interface here may change at any time!
 */
class MTexturePixmapPrivate: QObject
{
    Q_OBJECT
public:
    MTexturePixmapPrivate(Window window, QGLWidget *w, MTexturePixmapItem *item);
    ~MTexturePixmapPrivate();
    void init();
    void updateWindowPixmap(XRectangle *rects = 0, int num = 0);
    void saveBackingStore(bool renew = false);
    void clearTexture();
    bool isDirectRendered() const;
    void resize(int w, int h);
    void windowRaised();
    void drawTexture(const QTransform& transform, const QRectF& drawRect, qreal opacity);
    
    void q_drawTexture(const QTransform& transform, const QRectF& drawRect, qreal opacity);
    void damageTracking(bool enabled);
    void installEffect(MCompositeWindowShaderEffect* effect);
    static GLuint installPixelShader(const QByteArray& code);
                
    QGLContext *ctx;
    QGLWidget *glwidget;
    Window window;
    Pixmap windowp;
#ifdef GLES2_VERSION
    EGLImageKHR egl_image;
#else
    GLXPixmap glpixmap;
#endif
    GLuint textureId;
    GLuint ctextureId;
    static bool inverted_texture;
    bool custom_tfp;
    bool direct_fb_render;

    QRect brect;
    QRegion damageRegion;
    qreal angle;

    Damage damage_object;
    MTexturePixmapItem *item;
    QPointer<MCompositeWindowShaderEffect> current_effect;

#ifdef GLES2_VERSION
    static EglResourceManager *eglresource;
#endif
    static MGLResourceManager* glresource;

private slots:
    void activateEffect(bool enabled);
    void removeEffect();
};

#endif //DUITEXTUREPIXMAPITEM_P_H
