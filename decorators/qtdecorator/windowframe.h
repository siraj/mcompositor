#ifndef QT_FRAME_WINDOW_H
#define QT_FRAME_WINDOW_H

#include <QWidget>
#include "qtdecorator.h"
#include <QStatusBar>

//#ifdef HAVE_SHAPECONST
//#include <X11/extensions/shapeconst.h>
//#else
#include <X11/extensions/shape.h>
//#endif

class QtWindowFrame : public QWidget
{
    Q_OBJECT
public:
        QtWindowFrame(QWidget *parent = 0) ;
        virtual ~QtWindowFrame();
	void setOnlyStatusbar(bool mode, bool temporary = false);
	void setInputRegion();
	const QRect availableClientRect() const;
	void showQueryDialog(bool visible);
        bool x11Event(XEvent *e);
//protected:
//	virtual void closeEvent(QCloseEvent * event);
private:
	void setSceneSize();
	void setQtDecoratorWindowPropoerty();

        QtDeco *mDeco;
	QStatusBar *statusBar;
	bool onlyStatusBar, requestOnlyStatusbar;
	QRect availableRect; // available area for the managed window

        Q_DISABLE_COPY(QtWindowFrame)
};
#endif // QT_FRAME_WINDOW_H
