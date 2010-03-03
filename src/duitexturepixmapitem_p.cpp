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

#ifdef DESKTOP_VERSION
#define GL_GLEXT_PROTOTYPES 1
#endif
#include "duitexturepixmapitem.h"
#include "texturepixmapshaders.h"

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

#include "duitexturepixmapitem_p.h"

bool DuiTexturePixmapPrivate::inverted_texture = true;
DuiGLResourceManager* DuiTexturePixmapPrivate::glresource = 0;

static const GLuint D_VERTEX_COORDS = 0;
static const GLuint D_TEXTURE_COORDS = 1;

#ifdef QT_OPENGL_LIB

static void bindAttribLocation(QGLShaderProgram* p, const char* attrib, int location)
{
    p->bindAttributeLocation(attrib,location);
}

// OpenGL ES 2.0 / OpenGL 2.0 - compatible texture painter
class DuiGLResourceManager: public QObject
{
public:
    
    /* add more values here as we add more effects */
    enum ShaderType {
        NormalShader = 0,
        BlurShader,
        ShaderTotal
    };

    DuiGLResourceManager(QGLWidget* glwidget)
        :QObject(glwidget),
         currentShader(0)
    {
        QGLShader* sharedVertexShader = new QGLShader(QGLShader::Vertex, 
                                                      glwidget->context(), this);
        if (!sharedVertexShader->compileSourceCode(QLatin1String(TexpVertShaderSource)))
            qWarning("vertex shader failed to compile");
        
        QGLShaderProgram* normalShader = new QGLShaderProgram(glwidget->context(),this);
        shader[NormalShader] = normalShader;
        normalShader->addShader(sharedVertexShader);
        if (!normalShader->addShaderFromSourceCode(QGLShader::Fragment,
                                                   QLatin1String(TexpFragShaderSource)))
            qWarning("normal fragment shader failed to compile");
            
        QGLShaderProgram* blurShader = new QGLShaderProgram(glwidget->context(),this);
        shader[BlurShader] = blurShader;
        blurShader->addShader(sharedVertexShader);
        if (!blurShader->addShaderFromSourceCode(QGLShader::Fragment,
                                                 QLatin1String(blurshader)))
            qWarning("blur fragment shader failed to compile");
        
        bindAttribLocation(normalShader,"inputVertex", D_VERTEX_COORDS);
        bindAttribLocation(normalShader, "textureCoord", D_TEXTURE_COORDS);
        bindAttribLocation(blurShader, "inputVertex", D_VERTEX_COORDS);
        bindAttribLocation(blurShader, "textureCoord", D_TEXTURE_COORDS);

        normalShader->link();
        blurShader->link();
    }

    void initVertices(QGLWidget* glwidget)
    {
        texCoords[0] = 0.0f; texCoords[1] = 1.0f;
        texCoords[2] = 0.0f; texCoords[3] = 0.0f;
        texCoords[4] = 1.0f; texCoords[5] = 0.0f;
        texCoords[6] = 1.0f; texCoords[7] = 1.0f;
        texCoordsInv[0] = 0.0f; texCoordsInv[1] = 0.0f;
        texCoordsInv[2] = 0.0f; texCoordsInv[3] = 1.0f;
        texCoordsInv[4] = 1.0f; texCoordsInv[5] = 1.0f;
        texCoordsInv[6] = 1.0f; texCoordsInv[7] = 0.0f;

        projMatrix[0][0] =  2.0/glwidget->width(); projMatrix[1][0] =  0.0;   
        projMatrix[2][0] =  0.0;                   projMatrix[3][0] = -1.0;
        projMatrix[0][1] =  0.0;                   projMatrix[1][1] = -2.0/glwidget->height(); 
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
        glViewport(0,0,width,height);

        for(int i = 0; i < ShaderTotal; i++) {
            shader[i]->bind();
            shader[i]->setUniformValue("matProj", projMatrix);
        }        
    }

    void updateVertices(const QTransform& t, ShaderType type)
    {
        if (shader[type] != currentShader) 
            currentShader = shader[type];
        
        worldMatrix[0][0] = t.m11();
        worldMatrix[0][1] = t.m12();
        worldMatrix[0][3] = t.m13();
        worldMatrix[1][0] = t.m21();
        worldMatrix[1][1] = t.m22();
        worldMatrix[1][3] = t.m23();
        worldMatrix[3][0] = t.dx();
        worldMatrix[3][1] = t.dy();
        worldMatrix[3][3] = t.m33();
        currentShader->bind();
        currentShader->setUniformValue("matWorld", worldMatrix);
    }
    
private:    
    static QGLShaderProgram* shader[ShaderTotal];    
    GLfloat projMatrix[4][4];
    GLfloat worldMatrix[4][4];
    GLfloat vertCoords[8];
    GLfloat texCoords[8];
    GLfloat texCoordsInv[8];
    QGLShaderProgram* currentShader;
    int width;
    int height;

    friend class DuiTexturePixmapPrivate;
};

QGLShaderProgram* DuiGLResourceManager::shader[ShaderTotal];
#endif

void DuiTexturePixmapPrivate::drawTexture(const QTransform& transform, const QRectF& drawRect, qreal opacity)
{
    glwidget->makeCurrent();
    // TODO only update if matrix is dirty
    glresource->updateVertices(transform, item->blurred() ? 
                               DuiGLResourceManager::BlurShader : 
                               DuiGLResourceManager::NormalShader);  
    GLfloat vertexCoords[] = {
        drawRect.left(),  drawRect.top(),
        drawRect.left(),  drawRect.bottom(),
        drawRect.right(), drawRect.bottom(),
        drawRect.right(), drawRect.top()
    };
    glEnableVertexAttribArray(D_VERTEX_COORDS);
    glEnableVertexAttribArray(D_TEXTURE_COORDS);
    glVertexAttribPointer(D_VERTEX_COORDS, 2, GL_FLOAT, GL_FALSE, 0, vertexCoords);
    if(inverted_texture)
        glVertexAttribPointer(D_TEXTURE_COORDS, 2, GL_FLOAT, GL_FALSE, 0, 
                              glresource->texCoordsInv);
    else
        glVertexAttribPointer(D_TEXTURE_COORDS, 2, GL_FLOAT, GL_FALSE, 0, 
                              glresource->texCoords);
    if(item->blurred())
        glresource->currentShader->setUniformValue("blurstep", (GLfloat) 0.5);
    glresource->currentShader->setUniformValue("opacity", (GLfloat) opacity);
    glresource->currentShader->setUniformValue("texture", 0);
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
    
    glDisableVertexAttribArray(D_VERTEX_COORDS);
    glDisableVertexAttribArray(D_TEXTURE_COORDS); 
    
    glwidget->paintEngine()->syncState();
    glActiveTexture(GL_TEXTURE0);
}

void DuiTexturePixmapPrivate::init()
{
    if(!glresource) {
        glresource = new DuiGLResourceManager(glwidget);
        glresource->initVertices(glwidget);
    }
    
    XWindowAttributes a;
    XGetWindowAttributes(QX11Info::display(), item->window(), &a);

    XRenderPictFormat *format = XRenderFindVisualFormat(QX11Info::display(), a.visual);
    has_alpha = (format && format->type == PictTypeDirect && format->direct.alphaMask);

    if (a.map_state != IsViewable)
        viewable = false;

    override_redirect = a.override_redirect ? true : false;
    resize(a.width, a.height);
    item->setPos(a.x, a.y);
}

DuiTexturePixmapPrivate::DuiTexturePixmapPrivate(Qt::HANDLE window, QGLWidget *w, DuiTexturePixmapItem *p)
    : ctx(0),
      glwidget(w),
      windowp(0),
      custom_tfp(false),
      has_alpha(false),
      direct_fb_render(false),
      override_redirect(false),
      viewable(true),
      angle(0),
      item(p)
{
    damage_object = XDamageCreate(QX11Info::display(), window, XDamageReportNonEmpty);    
    init();
}

DuiTexturePixmapPrivate::~DuiTexturePixmapPrivate()
{
    XDamageDestroy(QX11Info::display(), damage_object);
}

void DuiTexturePixmapPrivate::saveBackingStore(bool renew)
{
    XWindowAttributes a;
    XGetWindowAttributes(QX11Info::display(), item->window(), &a);
    if (a.map_state != IsViewable)
        return;

    if (renew && windowp)
        XFreePixmap(QX11Info::display(), windowp);
    Pixmap px = XCompositeNameWindowPixmap(QX11Info::display(), item->window());
    windowp = px;
    if (renew)
        item->rebindPixmap();

    XSync(QX11Info::display(), FALSE);
}

void DuiTexturePixmapPrivate::windowRaised()
{
    XRaiseWindow(QX11Info::display(), item->window());
}

void DuiTexturePixmapPrivate::resize(int w, int h)
{
    if (!brect.isEmpty() && !item->isDirectRendered() && (brect.width() != w || brect.height() != h)) {
        item->saveBackingStore(true);
        item->updateWindowPixmap();
    }
    brect.setWidth(w);
    brect.setHeight(h);
}

bool DuiTexturePixmapPrivate::hasAlpha() const
{
    return has_alpha;
}

bool DuiTexturePixmapPrivate::isOverrideRedirect() const
{
    return override_redirect;
}

