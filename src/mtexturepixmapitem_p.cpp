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

#ifdef DESKTOP_VERSION
#define GL_GLEXT_PROTOTYPES 1
#endif
#include "mtexturepixmapitem.h"
#include "texturepixmapshaders.h"
#include "mcompositewindowshadereffect.h"
#include "mcompositemanager.h"

#include <QX11Info>
#include <QRect>

#include <X11/Xlib.h>
#include <X11/extensions/Xcomposite.h>
#include <X11/extensions/Xrender.h>
#ifdef GLES2_VERSION
#include <GLES2/gl2.h>
#elif DESKTOP_VERSION
#include <GL/gl.h>
#include <GL/glu.h>
#include <GL/glx.h>
#include <GL/glxext.h>
#endif

#include "mtexturepixmapitem_p.h"

bool MTexturePixmapPrivate::inverted_texture = true;
QGLWidget *MTexturePixmapPrivate::glwidget = 0;
QGLContext *MTexturePixmapPrivate::ctx = 0;
MGLResourceManager *MTexturePixmapPrivate::glresource = 0;

static const GLuint D_VERTEX_COORDS = 0;
static const GLuint D_TEXTURE_COORDS = 1;

#ifdef QT_OPENGL_LIB

static void bindAttribLocation(QGLShaderProgram *p, const char *attrib, int location)
{
    p->bindAttributeLocation(attrib, location);
}

class MShaderProgram : public QGLShaderProgram
{
public:
    MShaderProgram(const QGLContext* context, QObject* parent)
        : QGLShaderProgram(context, parent)
    {
        texture = -1;
        opacity = -1;
        blurstep = -1;
    }
    void setWorldMatrix(GLfloat m[4][4]) {
        static bool init = true;
        if (init || memcmp(m, worldMatrix, sizeof(worldMatrix))) {
            setUniformValue("matWorld", m);
            memcpy(worldMatrix, m, sizeof(worldMatrix));
            init = false;
        }
    }

    void setTexture(GLuint t) {
        if (t != texture) {
            setUniformValue("texture", t);
            texture = t;
        }
    }

    void setOpacity(GLfloat o) {
        if (o != opacity) {
            setUniformValue("opacity", o);
            opacity = o;
        }
    }
    void setBlurStep(GLfloat b) {
        if (b != blurstep) {
            setUniformValue("blurstep", b);
            blurstep = b;
        }
    }

private:
    // static because this is set in the shared vertex shader
    static GLfloat worldMatrix[4][4];
    GLfloat opacity, blurstep;
    GLuint texture;
};

GLfloat MShaderProgram::worldMatrix[4][4];

// OpenGL ES 2.0 / OpenGL 2.0 - compatible texture painter
class MGLResourceManager: public QObject
{
public:

    /*
     * This is the default set of shaders.
     * Use MCompositeWindowShaderEffect class to add more shader effects 
     */
    enum ShaderType {
        NormalShader = 0,
        BlurShader,
        ShaderTotal
    };

    MGLResourceManager(QGLWidget *glwidget)
        : QObject(glwidget),
          glcontext(glwidget->context()),
          currentShader(0)
    {
        sharedVertexShader = new QGLShader(QGLShader::Vertex,
                glwidget->context(), this);
        if (!sharedVertexShader->compileSourceCode(QLatin1String(TexpVertShaderSource)))
            qWarning("vertex shader failed to compile");

        MShaderProgram *normalShader = new MShaderProgram(glwidget->context(), 
                                                          this);
        normalShader->addShader(sharedVertexShader);
        if (!normalShader->addShaderFromSourceCode(QGLShader::Fragment,
                QLatin1String(TexpFragShaderSource)))
            qWarning("normal fragment shader failed to compile");
        shader[NormalShader] = normalShader;

        MShaderProgram *blurShader = new MShaderProgram(glwidget->context(), 
                                                        this);
        shader[BlurShader] = blurShader;
        blurShader->addShader(sharedVertexShader);
        if (!blurShader->addShaderFromSourceCode(QGLShader::Fragment,
                QLatin1String(blurshader)))
            qWarning("blur fragment shader failed to compile");

        bindAttribLocation(normalShader, "inputVertex", D_VERTEX_COORDS);
        bindAttribLocation(normalShader, "textureCoord", D_TEXTURE_COORDS);
        bindAttribLocation(blurShader, "inputVertex", D_VERTEX_COORDS);
        bindAttribLocation(blurShader, "textureCoord", D_TEXTURE_COORDS);

        normalShader->link();
        blurShader->link();
    }

    void initVertices(QGLWidget *glwidget) {
        texCoords[0] = 0.0f; texCoords[1] = 1.0f;
        texCoords[2] = 0.0f; texCoords[3] = 0.0f;
        texCoords[4] = 1.0f; texCoords[5] = 0.0f;
        texCoords[6] = 1.0f; texCoords[7] = 1.0f;
        texCoordsInv[0] = 0.0f; texCoordsInv[1] = 0.0f;
        texCoordsInv[2] = 0.0f; texCoordsInv[3] = 1.0f;
        texCoordsInv[4] = 1.0f; texCoordsInv[5] = 1.0f;
        texCoordsInv[6] = 1.0f; texCoordsInv[7] = 0.0f;

        projMatrix[0][0] =  2.0 / glwidget->width(); projMatrix[1][0] =  0.0;
        projMatrix[2][0] =  0.0;                   projMatrix[3][0] = -1.0;
        projMatrix[0][1] =  0.0;                   projMatrix[1][1] = -2.0 / glwidget->height();
        projMatrix[2][1] =  0.0;                   projMatrix[3][1] =  1.0;
        projMatrix[0][2] =  0.0;                   projMatrix[1][2] =  0.0;
        projMatrix[2][2] = -1.0;                   projMatrix[3][2] =  0.0;
        projMatrix[0][3] =  0.0;                   projMatrix[1][3] =  0.0;
        projMatrix[2][3] =  0.0;                   projMatrix[3][3] =  1.0;

        worldMatrix[0][2] = 0.0;
        worldMatrix[1][2] = 0.0;
        worldMatrix[2][0] = 0.0;
        worldMatrix[2][1] = 0.0;
        worldMatrix[2][2] = 1.0;
        worldMatrix[2][3] = 0.0;
        worldMatrix[3][2] = 0.0;
        width = glwidget->width();
        height = glwidget->height();
        glViewport(0, 0, width, height);

        for (int i = 0; i < ShaderTotal; i++) {
            shader[i]->bind();
            shader[i]->setUniformValue("matProj", projMatrix);
        }
    }

    void updateVertices(const QTransform &t) 
    {
        worldMatrix[0][0] = t.m11();
        worldMatrix[0][1] = t.m12();
        worldMatrix[0][3] = t.m13();
        worldMatrix[1][0] = t.m21();
        worldMatrix[1][1] = t.m22();
        worldMatrix[1][3] = t.m23();
        worldMatrix[3][0] = t.dx();
        worldMatrix[3][1] = t.dy();
        worldMatrix[3][3] = t.m33();
    }

    void updateVertices(const QTransform &t, ShaderType type) 
    {        
        if (shader[type] != currentShader)
            currentShader = shader[type];
        
        updateVertices(t);
        if (!currentShader->bind())
            qWarning() << __func__ << "failed to bind shader program";
        currentShader->setWorldMatrix(worldMatrix);
    }

    
    void updateVertices(const QTransform &t, GLuint customShaderId) 
    {                
        if (!customShaderId)
            return;
        MShaderProgram* frag = customShaders.value(customShaderId, 0);
        if (!frag)
            return;
        currentShader = frag;        
        updateVertices(t);
        if (!currentShader->bind())
            qWarning() << __func__ << "failed to bind shader program";
        currentShader->setWorldMatrix(worldMatrix);
    }

    GLuint installPixelShader(const QByteArray& code)
    {
        QByteArray source = code;
        source.append(TexpCustomShaderSource);
        MShaderProgram *p = new MShaderProgram(glcontext, this);
        p->addShader(sharedVertexShader);
        if (!p->addShaderFromSourceCode(QGLShader::Fragment,
                QLatin1String(source)))
            qWarning("custom fragment shader failed to compile");

        bindAttribLocation(p, "inputVertex", D_VERTEX_COORDS);
        bindAttribLocation(p, "textureCoord", D_TEXTURE_COORDS);

        if (p->link()) {
            customShaders[p->programId()] = p;
            return p->programId();
        } 
       
        qWarning() << "failed installing custom fragment shader:"
                   << p->log();
        p->deleteLater();
        
        return 0;
    }

private:
    static MShaderProgram *shader[ShaderTotal];
    QHash<GLuint, MShaderProgram *> customShaders;
    QGLShader *sharedVertexShader;
    const QGLContext* glcontext;    
    
    GLfloat projMatrix[4][4];
    GLfloat worldMatrix[4][4];
    GLfloat vertCoords[8];
    GLfloat texCoords[8];
    GLfloat texCoordsInv[8];
    MShaderProgram *currentShader;
    int width;
    int height;

    friend class MTexturePixmapPrivate;
};

MShaderProgram *MGLResourceManager::shader[ShaderTotal];
#endif


void MTexturePixmapPrivate::drawTexture(const QTransform &transform,
                                        const QRectF &drawRect,
                                        qreal opacity)
{
    if (current_effect) {
        current_effect->d->drawTexture(this, transform, drawRect, opacity);
    } else
        q_drawTexture(transform, drawRect, opacity);
}

void MTexturePixmapPrivate::q_drawTexture(const QTransform &transform,
                                          const QRectF &drawRect,
                                          qreal opacity,
                                          bool texcoords_from_rect)
{
    if (current_effect)
        glresource->updateVertices(transform, current_effect->activeShaderFragment());
    else
        glresource->updateVertices(transform, item->blurred() ?
                                   MGLResourceManager::BlurShader :
                                   MGLResourceManager::NormalShader);
    GLfloat vertexCoords[] = {
        drawRect.left(),  drawRect.top(),
        drawRect.left(),  drawRect.bottom(),
        drawRect.right(), drawRect.bottom(),
        drawRect.right(), drawRect.top()
    };
    glEnableVertexAttribArray(D_VERTEX_COORDS);
    glEnableVertexAttribArray(D_TEXTURE_COORDS);
    glVertexAttribPointer(D_VERTEX_COORDS, 2, GL_FLOAT, GL_FALSE, 0, vertexCoords);
    if (texcoords_from_rect) {
        float w, h, x, y, cx, cy, cw, ch;
        w = item->boundingRect().width();
        h = item->boundingRect().height();
        x = item->boundingRect().x();
        y = item->boundingRect().y();

        cx = (drawRect.x() - x) / w;
        cy = (drawRect.y() - y) / h;
        cw = drawRect.width() / w;
        ch = drawRect.height() / h;
        GLfloat texCoords[8];
        if (inverted_texture) {
            texCoords[0] = cx;      texCoords[1] = cy;
            texCoords[2] = cx;      texCoords[3] = ch + cy;
            texCoords[4] = cx + cw; texCoords[5] = ch + cy;
            texCoords[6] = cx + cw; texCoords[7] = cy;
        } else {
            texCoords[0] = cx;      texCoords[1] = ch + cy;
            texCoords[2] = cx;      texCoords[3] = cy;
            texCoords[4] = cx + cw; texCoords[5] = cy;
            texCoords[6] = cx + cw; texCoords[7] = ch + cy;
        }
        glVertexAttribPointer(D_TEXTURE_COORDS, 2, GL_FLOAT, GL_FALSE, 0,
                              texCoords);
    }
    else if (inverted_texture)
        glVertexAttribPointer(D_TEXTURE_COORDS, 2, GL_FLOAT, GL_FALSE, 0,
                              glresource->texCoordsInv);
    else
        glVertexAttribPointer(D_TEXTURE_COORDS, 2, GL_FLOAT, GL_FALSE, 0,
                              glresource->texCoords);
    
    if (current_effect)
        current_effect->setUniforms(glresource->currentShader);
    else if (item->blurred())
        glresource->currentShader->setBlurStep((GLfloat) 0.5);
    glresource->currentShader->setOpacity((GLfloat) opacity);
    glresource->currentShader->setTexture(0);
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);

    glDisableVertexAttribArray(D_VERTEX_COORDS);
    glDisableVertexAttribArray(D_TEXTURE_COORDS);

    glwidget->paintEngine()->syncState();
    glActiveTexture(GL_TEXTURE0);
}

void MTexturePixmapPrivate::installEffect(MCompositeWindowShaderEffect* effect)
{
    if (effect == prev_effect)
        return;

    if (prev_effect) {
        disconnect(prev_effect, SIGNAL(enabledChanged(bool)), this,
                   SLOT(activateEffect(bool)));
        disconnect(prev_effect, SIGNAL(destroyed()), this,
                   SLOT(removeEffect()));
    }
    current_effect = effect;
    prev_effect = effect;
    if (effect) {
        connect(effect, SIGNAL(enabledChanged(bool)),
                SLOT(activateEffect(bool)), Qt::UniqueConnection);
        connect(effect, SIGNAL(destroyed()), SLOT(removeEffect()),
                Qt::UniqueConnection);
    }
}

void MTexturePixmapPrivate::removeEffect()
{
    MCompositeWindowShaderEffect* e= (MCompositeWindowShaderEffect* ) sender();
    if (e == prev_effect)
        prev_effect = 0;
    for (int i=0; i < e->fragmentIds().size(); ++i) {
        GLuint id = e->fragmentIds()[i];
        QGLShaderProgram* frag = glresource->customShaders.value(id, 0);
        if (frag)
            delete frag;
        glresource->customShaders.remove(id);
    }    
}

GLuint MTexturePixmapPrivate::installPixelShader(const QByteArray& code)
{
    if (!glwidget) {
        MCompositeManager *m = (MCompositeManager*)qApp;
        glwidget = m->glWidget();
    }
    if (!glresource) {
        glresource = new MGLResourceManager(glwidget);
        glresource->initVertices(glwidget);
    }
    if (glresource)
        return glresource->installPixelShader(code);

    return 0;
}

void MTexturePixmapPrivate::activateEffect(bool enabled)
{
    if (enabled)
        current_effect = (MCompositeWindowShaderEffect* ) sender();
    else
        current_effect = 0;
}

void MTexturePixmapPrivate::init()
{
    if (!item->is_valid)
        return;

    if (!glresource) {
        glresource = new MGLResourceManager(glwidget);
        glresource->initVertices(glwidget);
    }

    resize(item->propertyCache()->realGeometry().width(),
           item->propertyCache()->realGeometry().height());
    item->setPos(item->propertyCache()->realGeometry().x(),
                 item->propertyCache()->realGeometry().y());
}

MTexturePixmapPrivate::MTexturePixmapPrivate(Qt::HANDLE window,
                                             MTexturePixmapItem *p)
    : window(window),
      windowp(0),
#ifdef GLES2_VERSION
      egl_image(EGL_NO_IMAGE_KHR),
#else
      glpixmap(0),
#endif
      textureId(0),
      ctextureId(0),
      custom_tfp(false),
      direct_fb_render(false), // root's children start redirected
      angle(0),
      item(p),
      prev_effect(0),
      pastDamages(0)
{
    if (!glwidget) {
        MCompositeManager *m = (MCompositeManager*)qApp;
        glwidget = m->glWidget();
    }
    if (!ctx)
        ctx = const_cast<QGLContext *>(glwidget->context());
    if (item->propertyCache())
        item->propertyCache()->damageTracking(true);
    init();
}

MTexturePixmapPrivate::~MTexturePixmapPrivate()
{
    if (item->propertyCache())
        item->propertyCache()->damageTracking(false);

    if (windowp)
        XFreePixmap(QX11Info::display(), windowp);

    if (pastDamages)
        delete pastDamages;
}

void MTexturePixmapPrivate::saveBackingStore()
{
    if ((item->propertyCache()->is_valid && !item->propertyCache()->isMapped())
        || item->propertyCache()->isInputOnly()
        || !window)
        return;

    if (windowp)
        XFreePixmap(QX11Info::display(), windowp);
    windowp = XCompositeNameWindowPixmap(QX11Info::display(), item->window());
    item->rebindPixmap(); // windowp == 0 is also handled here
}

void MTexturePixmapPrivate::resize(int w, int h)
{
    if (!window)
        return;
    
    if (!brect.isEmpty() && !item->isDirectRendered() && (brect.width() != w || brect.height() != h)) {
        item->saveBackingStore();
        item->updateWindowPixmap();
    }
    brect.setWidth(w);
    brect.setHeight(h);
}
