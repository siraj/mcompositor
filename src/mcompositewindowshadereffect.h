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

#ifndef MCOMPOSITEWINDOWSHADEREFFECT_H
#define MCOMPOSITEWINDOWSHADEREFFECT_H

#include <QObject>
#include <QVector>
#include <QGLShaderProgram>

class QTransform;
class QRectF;
class MCompositeWindowShaderEffect;
class MTexturePixmapPrivate;
class MCompositeWindow;
class MCompositeWindowShaderEffectPrivate;

class MCompositeWindowShaderEffect: public QObject
{
    Q_OBJECT
 public:
    MCompositeWindowShaderEffect(QObject* parent = 0);
    virtual ~MCompositeWindowShaderEffect();
    
    GLuint installShaderFragment(const QByteArray& code);
    GLuint texture() const;
    void setActiveShaderFragment(GLuint id);
    GLuint activeShaderFragment() const;

    void installEffect(MCompositeWindow* window);
    void removeEffect(MCompositeWindow* window);
    bool enabled() const;

 public slots:
    void setEnabled(bool enabled);

 signals:
    void enabledChanged( bool enabled);
    
 protected: 
    MCompositeWindow *comp_window;
    void drawSource(const QTransform &transform,
                    const QRectF &drawRect, qreal opacity,
                    bool texcoords_from_rect = false);
    virtual void drawTexture(const QTransform &transform,
                             const QRectF &drawRect, qreal opacity) = 0;
    virtual void setUniforms(QGLShaderProgram* program);

 private:    
    /* \cond */
    const QVector<GLuint>& fragmentIds() const;

    MCompositeWindowShaderEffectPrivate* d;
    friend class MTexturePixmapPrivate;
    friend class MCompositeWindowShaderEffectPrivate;
    /* \endcond */

};

/* \cond
 * Internal class. Do not use! Not part of public API
 */
class MCompositeWindowShaderEffectPrivate
{
 public:    
    void drawTexture(MTexturePixmapPrivate* render,
                     const QTransform &transform,
                     const QRectF &drawRect, qreal opacity);

 private:
    explicit MCompositeWindowShaderEffectPrivate(MCompositeWindowShaderEffect*);
    
    MCompositeWindowShaderEffect* effect;
    MTexturePixmapPrivate* priv_render;
    //  QMap<GLuint, QByteArray> pixelfragments;
    QVector<GLuint> pixfrag_ids;
    GLuint active_fragment;
    
    bool enabled;

    friend class MCompositeWindowShaderEffect;
};

/* \endcond */
#endif
