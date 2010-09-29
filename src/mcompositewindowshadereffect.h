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

    /*!
     * Install this effect on a composite window. Note that
     * we override QGraphicsItem::setGraphicsEffect() because
     * we have a completely different painting engine.
     * If a window has a previous effect, it will be overriden by the new one
     */
    void installEffect(MCompositeWindow* window);
    
    bool enabled() const;

 public slots:
    
    /*!
     * Enable this effect 
     */
    void setEnabled(bool enabled);

 signals:
    void enabledChanged( bool enabled);
    
 protected:
    void drawSource(const QTransform &transform,
                    const QRectF &drawRect, qreal opacity);
    
    virtual void drawTexture(const QTransform &transform,
                             const QRectF &drawRect, qreal opacity) = 0;
    /*!
     * Set the uniform values on the currently active shader.
     * Default implementation does nothing. Reimplement this function to
     * set values if you specified a custom pixel shader in your effects.
     */
    virtual void setUniforms(QGLShaderProgram* );

 private:    
    const QVector<GLuint>& fragmentIds() const;

    MCompositeWindowShaderEffectPrivate* d;
    friend class MTexturePixmapPrivate;
    friend class MCompositeWindowShaderEffectPrivate;
};

/*
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
#endif
