#include "windowframe.h"

#include <QApplication>
#include <QDesktopWidget>
#include <QX11Info>
#include <QGLFormat>
#include <QGLWidget>
#include <QVector>
#include <QGraphicsLinearLayout>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/Xmd.h>
#include <X11/extensions/Xfixes.h>
#include <X11/extensions/shape.h>


QtWindowFrame::QtWindowFrame(QWidget *parent) :
    QWidget(parent)
{
  mDeco = new QtDeco(this);
  statusBar = new QStatusBar();
  setOnlyStatusbar(false);
  requestOnlyStatusbar = false;	 
  setSceneSize();
  setInputRegion();
}

QtWindowFrame::~QtWindowFrame()
{
}

bool QtWindowFrame::x11Event(XEvent *event)
{

}

void QtWindowFrame::setOnlyStatusbar(bool mode, bool temporary)
{
	Q_UNUSED(mode);
}

void QtWindowFrame::setInputRegion()
{
	const QRegion fs(QApplication::desktop()->screenGeometry());
	static QRegion previousRegion;
	QRegion region;

	region = fs;
	if(previousRegion != region){
		previousRegion = region;
		const QVector<QRect> rects = region.rects();
		int nxrects = rects.count();
		XRectangle *xrects = new XRectangle[nxrects];
		for(int i = 0; i<nxrects; ++i)
		{
			xrects[i].x = rects[i].x();
			xrects[i].y = rects[i].y();
			xrects[i].width = rects[i].width();
			xrects[i].height = rects[i].height();
		}
		Display *dpy = QX11Info::display();
		XserverRegion shapeRegion = XFixesCreateRegion(dpy, xrects, nxrects);
		XFixesSetWindowShapeRegion(dpy, winId(), ShapeInput, 
					   0, 0, shapeRegion);
		XFixesSetWindowShapeRegion(dpy, winId(),ShapeBounding, 
					   0, 0, shapeRegion);

		XFixesDestroyRegion(dpy,shapeRegion);
		delete[] xrects;
	}
	availableRect = (fs - region).boundingRect();
}

const QRect QtWindowFrame::availableClientRect() const
{
	return availableRect;
}

void QtWindowFrame::setSceneSize()
{
	Display *dpy = QX11Info::display();
	int xres = ScreenOfDisplay(dpy, DefaultScreen(dpy))->width;
	int yres = ScreenOfDisplay(dpy, DefaultScreen(dpy))->height;

	setMaximumSize(xres,yres);
	setMinimumSize(xres,yres);
}

void QtWindowFrame::setQtDecoratorWindowPropoerty()
{
	long on = 1;
	
	XChangePropoerty(QX11Info::display(), winId(),
			 XInternAtom(QX11Info::display(),"PLEXY_DECORATOR_WINDOW",False),
			 XA_CARDINAL,
			 32, PropModeReplace,
			 (unsigned char *) &on, 1);
}
