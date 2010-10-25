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

/*!
  \class MCompositeWindowShaderEffect
  \brief MCompositeWindowShaderEffect is the base class for shader effects on windows
  
  Shader effects can change the appearance of composited windows by hooking
  into the rendering pipeline of the compositor. It allows manipulation
  of how a composited window is rendered by using custom GLSL fragment shaders
  and direct access to the texture rendering function.  
  An effect can be disabled by calling setEnabled(false). If effects are 
  disabled, the window texture is rendered directly.
  
  To add special effects to composited windows, subclass MCompositeWindowShaderEffect 
  and reimplement the pure virtual function drawTexture()
*/

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

/*!
 * Creates a window effect object
 */
MCompositeWindowShaderEffect::MCompositeWindowShaderEffect(QObject* parent)
    :QObject(parent),
     d(new MCompositeWindowShaderEffectPrivate(this))
{    
    // install default pixel shader
    QByteArray code = default_frag;
    d->active_fragment = installShaderFragment(code);
}

/*!
  Destroys the graphics effect and deletes GLSL fragment programs from
  the compositor's rendering engine if any are installed from this effect
*/
MCompositeWindowShaderEffect::~MCompositeWindowShaderEffect()
{
}

/*!
  Adds and installs the source code for a pixel shader fragment to \a code.
  
  The \a code must define a GLSL function with the signature
  \c{lowp vec4 customShader(lowp sampler2D imageTexture, highp vec2 textureCoords)}
  that returns the source pixel value to use in the paint engine's
  shader program.  The following is the default pixel shader fragment,
  which draws a texture with no effect applied:
  
  \code
  lowp vec4 customShader(lowp sampler2D imageTexture, highp vec2 textureCoords)
  {
      return texture2D(imageTexture, textureCoords);
  }
  \endcode
  
  \return The id of the fragment shader. If the fragment shader is invalid,
  return 0.
  
  \sa setUniforms()
*/   
GLuint MCompositeWindowShaderEffect::installShaderFragment(const QByteArray& code)
{
    GLuint id = MTexturePixmapPrivate::installPixelShader(code);
    d->pixfrag_ids.push_back(id);
    return id;
}

 /*!
   \return Texture id of the currently rendered window
 */
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

/*!
  Sets the rendering pipeline to use shader fragment specified by \a id 
*/
void MCompositeWindowShaderEffect::setActiveShaderFragment(GLuint id)
{
    d->active_fragment = id;
}

/*!
  \return The id of the currently active shader fragment
*/
GLuint MCompositeWindowShaderEffect::activeShaderFragment() const
{
    return d->active_fragment;
}

/*!
  \fn void MCompositeWindowShaderEffect::enabledChanged( bool enabled);
  Emmitted if this effect gets disabled or enabled
*/

/*!
  \fn virtual void MCompositeWindowShaderEffect::drawTexture(const QTransform &transform, const QRectF &drawRect, qreal opacity) = 0;

  This pure virtual function is called whenever the windows texture is
  rendered. Reimplement it to completely customize how the texture of this
  window is rendered using the given transformation 
  matrix \a transform, texture geometry \a drawRect, and \a opacity.
*/

/*!
  Draws currently bound source window texture. Specify a transformation matrix to
  \a transform and texture geometry to \a drawRect. Opacity can be set
  by specifying \a opacity (0.0 - 1.0). 
  This function should only be called inside drawTexture()      
*/
void MCompositeWindowShaderEffect::drawSource(const QTransform &transform, 
                                              const QRectF &drawRect, 
                                              qreal opacity)
{
    if (d->priv_render)
        d->priv_render->q_drawTexture(transform, drawRect, opacity);
}

/*!
  Install this effect on a composite window object \a window. Note that
  we override QGraphicsItem::setGraphicsEffect() because
  we have a completely different painting engine.
  If a window has a previous effect, it will be overriden by the new one
*/
void MCompositeWindowShaderEffect::installEffect(MCompositeWindow* window)
{
    if (!window->isValid())
        return;

    // only happens with GL. sorry n800 guys :p
#ifdef QT_OPENGL_LIB
    MTexturePixmapItem* item = (MTexturePixmapItem*) window;
    item->d->installEffect(this);    
#endif
}

/*!
  Remove this effect from a composite window object \a window. After removing
  the effect the window will use default shaders.
*/
void MCompositeWindowShaderEffect::removeEffect(MCompositeWindow* window)
{
    if (!window->isValid())
        return;

#ifdef QT_OPENGL_LIB
    MTexturePixmapItem* item = (MTexturePixmapItem*) window;
    item->d->installEffect(0);    
#endif
}

/*!
  \return Whether this effect is enabled or not
*/
bool MCompositeWindowShaderEffect::enabled() const
{
    return d->enabled;
}

/*!
  Enable or disable this effect 
*/
void MCompositeWindowShaderEffect::setEnabled(bool enabled)
{
    d->enabled = enabled;
    emit enabledChanged(enabled);
}

/*!
  Set the uniform values on the currently active shader \a program.
  Default implementation does nothing. Reimplement this function to
  set values if you specified a custom pixel shader in your effects.
*/
void MCompositeWindowShaderEffect::setUniforms(QGLShaderProgram* program)
{
    Q_UNUSED(program);
}

