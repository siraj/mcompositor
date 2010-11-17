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
  \class MCompositeWindowGroup
  \brief MCompositeWindowGroup allows a collection of windows to be rendered
  as a single texture.
  
  This class is useful for rendering a list of windows that needs to 
  be treated as one item with transformations applied only to a single texture.
  Unlike QGraphicsItemGroup where each item is rendered separately for each
  frame, MCompositeWindowGroup can pre-render all items to an off-screen buffer 
  before a frame starts and can render the resulting texture afterwards as a 
  single quad which reduces GPU load and helps performance especially in panning
  and scaling transformations. 
  
  Use this class to render a main window with its transient windows. Another
  use case is for a window that needs to have similar transformations to a
  main window. The addChildWindow() function adds windows that need to 
  animate synchronously  with the main window. The removeChildWindow() function
  removes a window from the group.  

  Implementation is hw-dependent. It relies on framebuffer objects on GLES2 
  and the GL_EXT_framebuffer_object extension on the desktop.
*/

#include <QtOpenGL> 
#include <QList>
#include <mcompositewindowgroup.h>

#include <mtexturepixmapitem.h>
#include <mcompositemanager.h>

#ifdef GLES2_VERSION
#define FORMAT GL_RGBA
#define DEPTH GL_DEPTH_COMPONENT16
#else
#define FORMAT GL_RGBA8
#define DEPTH GL_DEPTH_COMPONENT
#endif

class MCompositeWindowGroupPrivate
{
public:
    MCompositeWindowGroupPrivate(MTexturePixmapItem* mainWindow)
        :main_window(mainWindow),
         texture(0),
         fbo(0),
         depth_buffer(0),
         valid(false),
         renderer(new MTexturePixmapPrivate(0, mainWindow))            
    {       
    }
    MTexturePixmapItem* main_window;
    GLuint texture;
    GLuint fbo;
    GLuint depth_buffer;
    
    bool valid;
    QList<MTexturePixmapItem*> item_list;
    MTexturePixmapPrivate* renderer;
};

/*!
  Creates a window group object. Specify the main window
  with \a mainWindow
 */
MCompositeWindowGroup::MCompositeWindowGroup(MTexturePixmapItem* mainWindow)
    :MCompositeWindow(0, new MWindowPropertyCache(0)),
     d_ptr(new MCompositeWindowGroupPrivate(mainWindow))
{
    MCompositeManager *p = (MCompositeManager *) qApp;
    p->scene()->addItem(this);
    
    mainWindow->d->current_window_group = this;
    connect(mainWindow, SIGNAL(destroyed()), SLOT(deleteLater()));
    init();
    updateWindowPixmap();
    setZValue(mainWindow->zValue());
    stackBefore(mainWindow);
}

/*!
  Destroys this window group and frees resources. All windows that are within 
  this group revert back to directly rendering its own texture.
 */
MCompositeWindowGroup::~MCompositeWindowGroup()
{
    Q_D(MCompositeWindowGroup);
    
    if (!QGLContext::currentContext()) {
        qWarning("MCompositeWindowGroup::%s(): no current GL context",
                 __func__);
        return;
    }
    
    GLuint texture = d->texture;
    glDeleteTextures(1, &texture);
    GLuint depth_buffer = d->depth_buffer;
    glDeleteRenderbuffers(1, &depth_buffer);
    GLuint fbo = d->fbo;
    glDeleteFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    update();
}

void MCompositeWindowGroup::init()
{
    Q_D(MCompositeWindowGroup);
    
    if (!QGLContext::currentContext()) {
        qWarning("MCompositeWindowGroup::%s(): no current GL context",
                 __func__);
        d->valid = false;
        return;
    }
    d->renderer->current_window_group = this;
    
    glGenFramebuffers(1, &d->fbo);
    glGenRenderbuffers(1, &d->depth_buffer);
    glBindFramebuffer(GL_RENDERBUFFER, d->fbo);
    
    glGenTextures(1, &d->texture);
    glBindTexture(GL_TEXTURE_2D, d->texture);
    
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    
    glBindRenderbuffer(GL_RENDERBUFFER, d->depth_buffer);
    glRenderbufferStorage(GL_RENDERBUFFER, DEPTH, 
                          d->main_window->boundingRect().width(), 
                          d->main_window->boundingRect().height());

    glBindFramebuffer(GL_FRAMEBUFFER, d->fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, d->texture, 0);

    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                              GL_RENDERBUFFER, d->depth_buffer);
    
    glBindTexture(GL_TEXTURE_2D, d->texture);
    glTexImage2D(GL_TEXTURE_2D, 0, FORMAT, 
                 d->main_window->boundingRect().width(), 
                 d->main_window->boundingRect().height(), 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    
    GLenum ret = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (ret == GL_FRAMEBUFFER_COMPLETE)
        d->valid = true;
    else         
        qWarning("MCompositeWindowGroup::%s(): incomplete FBO attachment 0x%x",
                 __func__, ret);           

    glBindTexture(GL_TEXTURE_2D, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);    
}

static bool behindCompare(MTexturePixmapItem* a, MTexturePixmapItem* b)
{
    return a->indexInStack() < b->indexInStack();
}

/*!
  Adds the given \a window to this group. The window's transformations will
  remain unmodified
 */
void MCompositeWindowGroup::addChildWindow(MTexturePixmapItem* window)
{
    Q_D(MCompositeWindowGroup);
    window->d->current_window_group = this;
    connect(window, SIGNAL(destroyed()), SLOT(q_removeWindow()));
    d->item_list.append(window);
    
    // ensure group windows are already stacked in proper order in advance
    // for back to front rendering. Could use depth buffer attachment at some 
    // point so this might be unecessary
    qSort(d->item_list.begin(), d->item_list.end(), behindCompare);
    updateWindowPixmap();
}

/*!
  Removes the given \a window to from group. 
 */
void MCompositeWindowGroup::removeChildWindow(MTexturePixmapItem* window)
{
    Q_D(MCompositeWindowGroup);
    window->d->current_window_group = 0;
    d->item_list.removeAll(window);
}

void MCompositeWindowGroup::q_removeWindow()
{
    Q_D(MCompositeWindowGroup);
    MTexturePixmapItem* w = qobject_cast<MTexturePixmapItem*>(sender());
    if (w) 
        d->item_list.removeAll(w);
}

void MCompositeWindowGroup::saveBackingStore() {}

void MCompositeWindowGroup::resize(int , int) {}

void MCompositeWindowGroup::clearTexture() {}

bool MCompositeWindowGroup::isDirectRendered() const { return false; }

QRectF MCompositeWindowGroup::boundingRect() const 
{
    Q_D(const MCompositeWindowGroup);
    return d->main_window->boundingRect();
}

void MCompositeWindowGroup::paint(QPainter* painter, 
                                  const QStyleOptionGraphicsItem* options, 
                                  QWidget* widget) 
{    
    Q_D(MCompositeWindowGroup);
    Q_UNUSED(options)
    Q_UNUSED(widget)

#if QT_VERSION < 0x040600
    if (painter->paintEngine()->type() != QPaintEngine::OpenGL)
        return;
#else
    if (painter->paintEngine()->type() != QPaintEngine::OpenGL2) {
        return;
    }
#endif
    
    glBindTexture(GL_TEXTURE_2D, d->texture);    
    if (d->main_window->propertyCache()->hasAlpha() || 
        (opacity() < 1.0f && !dimmedEffect()) ) {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    }
    d->renderer->drawTexture(painter->combinedTransform(), boundingRect(), 
                             opacity());    
    glBlendFunc(GL_ONE, GL_ZERO);
    glDisable(GL_BLEND);
}

void MCompositeWindowGroup::windowRaised()
{
}

void MCompositeWindowGroup::updateWindowPixmap(XRectangle *rects, int num,
                                               Time t)
{
    Q_UNUSED(rects)
    Q_UNUSED(num)
    Q_UNUSED(t)
    Q_D(MCompositeWindowGroup);
    
    if (!d->valid) {
        qDebug() << "invalid fbo";
        return;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, d->fbo);
    GLenum ret = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (ret == GL_FRAMEBUFFER_COMPLETE)
        d->valid = true;
    else {
        qWarning("MCompositeWindowGroup::%s(): incomplete FBO attachment 0x%x",
                 __func__, ret); 
        d->valid = false;
    }
    d->main_window->d->inverted_texture = false;
    d->main_window->renderTexture(d->main_window->sceneTransform());
    d->main_window->d->inverted_texture = true;
    for (int i = 0; i < d->item_list.size(); ++i) {
        MTexturePixmapItem* item = d->item_list[i];
        item->d->inverted_texture = false;
        item->renderTexture(item->sceneTransform());
        item->d->inverted_texture = true;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

// internal re-implementation from MCompositeWindow
MTexturePixmapPrivate* MCompositeWindowGroup::renderer() const
{
    Q_D(const MCompositeWindowGroup);
    return d->renderer;
}

/*!
  \return Returns the texture used by the off-screen buffer
*/
GLuint MCompositeWindowGroup::texture()
{
    Q_D(MCompositeWindowGroup);
    return d->texture;
}


