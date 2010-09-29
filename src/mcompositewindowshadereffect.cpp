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
#include "mcompositewindowshadereffect.h"
#include "mtexturepixmapitem_p.h"
#include <QByteArray>

static const char default_frag[] = "\
    lowp vec4 customShader(lowp sampler2D imageTexture, highp vec2 textureCoords) { \
        return texture2D(imageTexture, textureCoords); \
    }\n";

MCompositeWindowShaderEffectPrivate::MCompositeWindowShaderEffectPrivate(MCompositeWindowShaderEffect* e)
    :effect(e),
     priv_render(0),
     active_fragment(0)
{
}

void MCompositeWindowShaderEffectPrivate::drawTexture(MTexturePixmapPrivate* render, const QTransform &transform, const QRectF &drawRect, qreal opacity)
{
    priv_render = render;
    effect->drawTexture(transform, drawRect, opacity);
    enabled = false;
}

MCompositeWindowShaderEffect::MCompositeWindowShaderEffect(QObject* parent)
    :QObject(parent),
     d(new MCompositeWindowShaderEffectPrivate(this))
{    
    // install default pixel shader
    QByteArray code = default_frag;
    d->active_fragment = installShaderFragment(code);
}

MCompositeWindowShaderEffect::~MCompositeWindowShaderEffect()
{
}
    
/*#
    Sets the source code for a pixel shader fragment for
    this shader effect to \a code.

    The \a code must define a GLSL function with the signature
    \c{lowp vec4 customShader(lowp sampler2D imageTexture, highp vec2 textureCoords)}
    that returns the source pixel value to use in the paint engine's
    shader program.  The following is the default pixel shader fragment,
    which draws a pixmap with no effect applied:

    \code
    lowp vec4 customShader(lowp sampler2D imageTexture, highp vec2 textureCoords) {
        return texture2D(imageTexture, textureCoords);
    }
    \endcode

    returns the id of the fragment shader

    \sa pixelShaderFragment(), setUniforms()
*/
GLuint MCompositeWindowShaderEffect::installShaderFragment(const QByteArray& code)
{
    GLuint id = MTexturePixmapPrivate::installPixelShader(code);
    d->pixfrag_ids.push_back(id);
    return id;
}

GLuint MCompositeWindowShaderEffect::texture() const
{
    // TODO: This assumes we have always have hadware TFP support 
    if (d->priv_render)
        return d->priv_render->textureId;
    return 0;
}

const QVector<GLuint>& MCompositeWindowShaderEffect::fragmentIds() const
{
    return d->pixfrag_ids;
}

void MCompositeWindowShaderEffect::setActiveShaderFragment(GLuint id)
{
    d->active_fragment = id;
}

GLuint MCompositeWindowShaderEffect::activeShaderFragment() const
{
    return d->active_fragment;
}

void MCompositeWindowShaderEffect::drawSource(const QTransform &transform, 
                                              const QRectF &drawRect, 
                                              qreal opacity)
{
    if (d->priv_render)
        d->priv_render->q_drawTexture(transform, drawRect, opacity);
}

void MCompositeWindowShaderEffect::installEffect(MCompositeWindow* window)
{
    // only happens with GL. sorry n800 guys :p
#ifdef QT_OPENGL_LIB
    MTexturePixmapItem* item = (MTexturePixmapItem*) window;
    item->d->installEffect(this);    
#endif
}

bool MCompositeWindowShaderEffect::enabled() const
{
    return d->enabled;
}

void MCompositeWindowShaderEffect::setEnabled(bool enabled)
{
    d->enabled = enabled;
    emit enabledChanged(enabled);
}

/*!
 * set the uniform values on the current executed fragment shader
 */
void MCompositeWindowShaderEffect::setUniforms(QGLShaderProgram* program)
{
    Q_UNUSED(program);
}

