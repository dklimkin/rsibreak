/* This file is part of the KDE project
   Copyright (C) 2005-2006 Tom Albers <tomalbers@kde.nl>
   Copyright (C) 2006 Bram Schoenmakers <bramschoenmakers@kde.nl>

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public
   License as published by the Free Software Foundation; either
   version 2 of the License, or (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
*/

#include "rsiwidget.h"
#include "graywidget.h"
#include "slideshow.h"

#include <qpushbutton.h>
#include <qdesktopwidget.h>
#include <qlayout.h>
#include <qtimer.h>
#include <qdatetime.h>
#include <qlineedit.h>
#include <qimage.h>
#include <qdir.h>
#include <qstringlist.h>
#include <qfileinfo.h>
#include <qcursor.h>
#include <qpainter.h>
#include <qbitmap.h>
//Added by qt3to4:
#include <QKeyEvent>
#include <QLabel>
#include <Q3CString>
#include <QPixmap>
#include <QHideEvent>
#include <QMouseEvent>
#include <QCloseEvent>
#include <QPixmap>
#include <config.h>

#include <kwindowsystem.h>
#include <klocale.h>
#include <kapplication.h>
#include <kdebug.h>
#include <kconfig.h>
#include <kmessagebox.h>
#include <kiconloader.h>
#include <kimageeffect.h>
#include <ksystemtrayicon.h>
#include <KTemporaryFile>
#include <kpixmapeffect.h>
#include <kglobal.h>

#include <stdlib.h>
#include <time.h>
#include <math.h>

#include "rsitimer.h"
#include "rsidock.h"
#include "rsirelaxpopup.h"
#include "rsitooltip.h"
#include "rsiglobals.h"

RSIWidget::RSIWidget( QWidget *parent )
    : QWidget( parent, Qt::Popup), m_currentY( 0 ), m_useImages( false )
{
    setAttribute( Qt::WA_NoSystemBackground );

    // Keep these 3 lines _above_ the messagebox, so the text actually is right.
    m_tray = new RSIDock(this);
    m_tray->setIcon( KSystemTrayIcon::loadIcon( "rsibreak0" ) );
    m_tray->show();

    QRect rect = QApplication::desktop()->screenGeometry(
                        QApplication::desktop()->primaryScreen() );
    setGeometry( rect );

    m_tooltip = new RSIToolTip( this, m_tray  );
    connect( m_tray, SIGNAL( showToolTip() ), m_tooltip, SLOT( showToolTip() ) );

    m_relaxpopup = new RSIRelaxPopup(this, m_tray);
    m_relaxpopup->show();
    connect( m_relaxpopup, SIGNAL( lock() ), SLOT( slotLock() ) );

    connect( m_tray, SIGNAL( quitSelected() ), kapp, SLOT( quit() ) );
    connect( m_tray, SIGNAL( configChanged( bool ) ), SLOT( readConfig() ) );
    connect( m_tray, SIGNAL( configChanged( bool ) ), RSIGlobals::instance(),
             SLOT( slotReadConfig() ) );
    connect( m_tray, SIGNAL( configChanged( bool ) ), m_relaxpopup,
             SLOT( slotReadConfig() ) );
    connect( m_tray, SIGNAL( suspend( bool ) ), m_tooltip,
             SLOT( setSuspended( bool ) ) );
    connect( m_tray, SIGNAL( suspend( bool ) ), m_relaxpopup, SLOT( hide() ) );

    srand ( time(NULL) );

    m_grayWidget = new GrayWidget(this); // create before readConfig.
    m_slideShow = new SlideShow(this); // create before readConfig.

    readConfig();

    // m_timer is only available after readConfig.
    connect(m_grayWidget, SIGNAL( skip() ), m_timer, SLOT( skipBreak() ) );
    connect(m_grayWidget, SIGNAL( lock() ), this, SLOT( slotLock() ) );

    setIcon( 0 );

    QTimer::singleShot(0,this,SLOT(slotWelcome()));
}

RSIWidget::~RSIWidget()
{
    delete RSIGlobals::instance();
}

void RSIWidget::slotWelcome()
{
    if (KMessageBox::shouldBeShownContinue("dont_show_welcome_again_for_001"))
    {
      QString tempfile = takeScreenshotOfTrayIcon();
      KMessageBox::information(this, i18n("<p>Welcome to RSIBreak<p><p>"
        "In your tray you can now see RSIBreak: ")
        + "<p><center><img source=\"" + tempfile + "\"></center></p><p>"
        + i18n("When you right-click on that you will see a menu, from which "
        "you can go to the configuration for example.<p>When you want to "
        "know when the next break is, hover over the icon.<p>Use RSIBreak "
        "wisely."), i18n("Welcome"), "dont_show_welcome_again_for_001");
    }
}

void RSIWidget::slotShowWhereIAm()
{
      QString tempfile = takeScreenshotOfTrayIcon();
      KMessageBox::information(this,
           i18n("<p>RSIBreak is already running<p><p>It is located here:")
           + "<p><center><img source=\"" + tempfile +"\"></center></p><p>",
           i18n("Already Running"));
}

QString RSIWidget::takeScreenshotOfTrayIcon()
{
        // Process the events else the icon will not be there and the screenie will fail!
        kapp->processEvents();

        // ********************************************************************************
        // This block is copied from Konversation - KonversationMainWindow::queryClose()
        // The part about the border is copied from  KSystemTray::displayCloseMessage()
        //
        // Compute size and position of the pixmap to be grabbed:

        QPoint g = m_tray->geometry().topLeft();
        int desktopWidth  = kapp->desktop()->width();
        int desktopHeight = kapp->desktop()->height();
        int tw = m_tray->geometry().width();
        int th = m_tray->geometry().height();
        int w = desktopWidth / 4;
        int h = desktopHeight / 9;
        int x = g.x() + tw/2 - w/2; // Center the rectange in the systray icon
        int y = g.y() + th/2 - h/2;
        if ( x < 0 ) x = 0;  // Move the rectangle to stay in the desktop limits
        if ( y < 0 ) y = 0;
        if ( x + w > desktopWidth )  x = desktopWidth - w;
        if ( y + h > desktopHeight ) y = desktopHeight - h;

        // Grab the desktop and draw a circle around the icon:
        QPixmap shot = QPixmap::grabWindow( QX11Info::appRootWindow(),  x,  y,  w,  h );
        QPainter painter( &shot );
        const int MARGINS = 6;
        const int WIDTH   = 5;
        int ax = g.x() - x - 2*MARGINS -1;
        int ay = g.y() - y - 2*MARGINS -1;
        painter.setPen(  QPen( Qt::red,  WIDTH ) );
        painter.drawArc( ax,  ay,  tw + 4*MARGINS,  th + 4*MARGINS,  0,  16*360 );
        painter.end();

        // Then, we add a border around the image to make it more visible:
        QPixmap finalShot(w + 2, h + 2);
        finalShot.fill(KApplication::palette().color(QPalette::Active,
                                                     QPalette::WindowText));
        painter.begin(&finalShot);
        painter.drawPixmap(1, 1, shot);
        painter.end();

       // End copied block
       // *****************************************************************

        QString filename;
        KTemporaryFile* tmpfile = new KTemporaryFile;
        tmpfile->setAutoRemove(false);
        if (tmpfile->open())
        {
                filename = tmpfile->fileName();
                finalShot.save( tmpfile, "png");
                tmpfile->close();
        }
        return filename;
}

void RSIWidget::minimize( bool newImage )
{
    m_grayWidget->hide();
    m_grayWidget->reset();
    m_slideShow->stop();
    if (newImage)
       m_slideShow->loadImage();
}

void RSIWidget::maximize()
{
    // If there are no images found, we gray the screen and wait....
    if (!m_slideShow->hasImages() || !m_useImages)
    {
      m_grayWidget->show(); // Keep it above the KWindowSystem calls.
      KWindowSystem::forceActiveWindow(m_grayWidget->winId());
      KWindowSystem::setOnAllDesktops(m_grayWidget->winId(),true);
      KWindowSystem::setState(m_grayWidget->winId(), NET::KeepAbove);
      KWindowSystem::setState(m_grayWidget->winId(), NET::FullScreen);
      m_currentY=0;
      QTimer::singleShot( 10, m_grayWidget, SLOT( slotGrayEffect() ) );
    }
    else
    {
      m_slideShow->show(); // Keep it above the KWindowSystem calls.
      KWindowSystem::forceActiveWindow(m_slideShow->winId());
      KWindowSystem::setOnAllDesktops(m_slideShow->winId(),true);
      KWindowSystem::setState(m_slideShow->winId(), NET::KeepAbove);
      KWindowSystem::setState(m_slideShow->winId(), NET::FullScreen);
      m_slideShow->start();
    }
}

void RSIWidget::slotLock()
{
    m_slideShow->stop();

    /* TODO PORT
    Q3CString appname( "kdesktop" );
    int rsibreak_screen = qt_xscreen();
    if ( rsibreak_screen )
        appname.sprintf("kdesktop-screen-%d", rsibreak_screen );
    kapp->dcopClient()->send(appname, "KScreensaverIface", "lock()", "");
    */
}

void RSIWidget::setCounters( int timeleft )
{
    if (timeleft > 0)
    {
        int minutes = (int)floor(timeleft/60);
        int seconds  = timeleft-(minutes*60);
        QString cdString;

        if (minutes > 0 && seconds > 0)
        {
            cdString = i18nc("minutes:seconds","%1:%2")
                    .arg( QString::number( minutes ))
                    .arg( QString::number( seconds).rightJustified(2,'0'));
        }
        else if ( minutes == 0 && seconds > 0 )
        {
            cdString = QString::number( seconds );
        }
        else if ( minutes >0 && seconds == 0 )
        {
            cdString = i18nc("minutes:seconds","%1:00")
                    .arg( minutes );
        }

        m_grayWidget->setLabel( cdString );
    }
    else if ( m_timer->isSuspended() )
    {
        m_grayWidget->setLabel( i18n("Suspended") );
    }
    else
    {
        m_grayWidget->setLabel( QString() );
    }
}

void RSIWidget::updateIdleAvg( double idleAvg )
{
    if ( idleAvg == 0.0 )
        setIcon( 0 );
    else if ( idleAvg >0 && idleAvg<30 )
        setIcon( 1 );
    else if ( idleAvg >=30 && idleAvg<60 )
        setIcon( 2 );
    else if ( idleAvg >=60 && idleAvg<90 )
        setIcon( 3 );
    else
        setIcon( 4 );
}

void RSIWidget::setIcon(int level)
{
    static QString currentIcon;
    QString newIcon = "rsibreak" +
                      ( m_timer->isSuspended() ? QString("x") : QString::number(level) );

    if (newIcon != currentIcon)
    {
        QIcon dockIcon = KSystemTrayIcon::loadIcon( newIcon );
        m_tray->setIcon( dockIcon );

        QPixmap toolPixmap = KIconLoader::global()->loadIcon( newIcon, K3Icon::Desktop );
        currentIcon = newIcon;
        m_tooltip->setPixmap( toolPixmap );
    }
}

// ------------------- Popup for skipping break ------------- //

void RSIWidget::tinyBreakSkipped()
{
    if (!m_showTimerReset)
        return;

    m_tooltip->setText( i18n("Timer for the short break has now been reset"));
    breakSkipped();
}

void RSIWidget::bigBreakSkipped()
{
    if (!m_showTimerReset)
        return;

    m_tooltip->setText( i18n("The timers have now been reset"));
    breakSkipped();
}

void RSIWidget::breakSkipped()
{
    disconnect( m_timer, SIGNAL( updateToolTip( int, int ) ),
                m_tooltip, SLOT( setCounters( int, int ) ) );

    m_tooltip->setPixmap( KIconLoader::global()->loadIcon( "rsibreak0", K3Icon::Desktop ) );
    m_tooltip->setTimeout(0); // autoDelete is false, but after the ->show() it still
                              // gets hidden after the timeout. Setting to 0 helps.
    m_tooltip->show();
}

void RSIWidget::skipBreakEnded()
{
    if (!m_showTimerReset)
        return;

    connect( m_timer, SIGNAL( updateToolTip( int, int ) ),
             m_tooltip, SLOT( setCounters( int, int ) ) );
    m_tooltip->hide();
}

//--------------------------- CONFIG ----------------------------//

void RSIWidget::startTimer( bool idle)
{

    static bool first = true;

    if (!first)
    {
      kDebug() << "Current Timer: " << m_timer->metaObject()->className()
                << " wanted: " << (idle ? "RSITimer" : "RSITimerNoIdle") << endl;

        if (!idle && m_timer->metaObject()->className() == "RSITimerNoIdle")
            return;

        if (idle && m_timer->metaObject()->className() == "RSITimer")
            return;

        kDebug() << "Switching timers" << endl;

        m_timer->deleteLater();
//        m_accel->remove("minimize");
    }

    if (idle)
        m_timer = new RSITimer(this);
    else
        m_timer = new RSITimerNoIdle(this);

    connect( m_timer, SIGNAL( breakNow() ), SLOT( maximize() ) );
    connect( m_timer, SIGNAL( updateWidget( int ) ),
             SLOT( setCounters( int ) ) );
    connect( m_timer, SIGNAL( updateToolTip( int, int ) ),
             m_tooltip, SLOT( setCounters( int, int ) ) );
    connect( m_timer, SIGNAL( updateIdleAvg( double ) ), SLOT( updateIdleAvg( double ) ) );
    connect( m_timer, SIGNAL( minimize( bool ) ), SLOT( minimize( bool ) ) );
    connect( m_timer, SIGNAL( relax( int, bool ) ), m_relaxpopup, SLOT( relax( int, bool ) ) );
    connect( m_timer, SIGNAL( relax( int, bool ) ), m_tooltip, SLOT( hide() ) );
    connect( m_timer, SIGNAL( tinyBreakSkipped() ), SLOT( tinyBreakSkipped() ) );
    connect( m_timer, SIGNAL( bigBreakSkipped() ), SLOT( bigBreakSkipped() ) );
    connect( m_timer, SIGNAL( skipBreakEnded() ), SLOT( skipBreakEnded() ) );

    connect( m_tray, SIGNAL( configChanged( bool ) ), m_timer, SLOT( slotReadConfig( bool ) ) );
    connect( m_tray, SIGNAL( dialogEntered() ), m_timer, SLOT( slotStopNoImage( ) ) );
    connect( m_tray, SIGNAL( dialogLeft() ), m_timer, SLOT( slotStartNoImage() ) );
    connect( m_tray, SIGNAL( breakRequest() ), m_timer, SLOT( slotRequestBreak() ) );
    connect( m_tray, SIGNAL( debugRequest() ), m_timer, SLOT( slotRequestDebug() ) );
    connect( m_tray, SIGNAL( suspend( bool ) ), m_timer, SLOT( slotSuspended( bool ) ) );

    connect( m_relaxpopup, SIGNAL( skip() ), m_timer, SLOT( skipBreak() ) );

    first = false;
}

void RSIWidget::readConfig()
{
  static QString oldPath;
  static bool oldRecursive;

    KConfigGroup config = KGlobal::config()->group("General Settings");
    m_showTimerReset = config.readEntry("ShowTimerReset", false);
    // TODO:  QColor color = config.readEntry("CounterColor", QColor( Qt::black ) );

    m_grayWidget->showMinimize(!config.readEntry("HideMinimizeButton", false));

    m_relaxpopup->setSkipButtonHidden(
            config.readEntry("HideMinimizeButton", false));
    // TODO: m_countDown->setHidden( config.readEntry("HideCounter", false));
    // m_countDown->setFont( config.readEntry("CounterFont",
    //                    QFont( QApplication::font().family(), 40, 75, true ) ) );

    m_useImages = config.readEntry("ShowImages", false);
    int slideInterval = config.readEntry("SlideInterval", 10);
    bool recursive =
            config.readEntry("SearchRecursiveCheck", false);
    QString path = config.readEntry("ImageFolder");

    bool timertype = config.readEntry("UseNoIdleTimer", false);
    startTimer(!timertype);

    // Hook in the shortcut after the timer initialisation.
    m_grayWidget->disableShortcut( config.readEntry("DisableAccel", false) );

    if ((oldPath != path || oldRecursive != recursive) && m_useImages)
          m_slideShow->reset( path, recursive, slideInterval);

    oldPath = path;
    oldRecursive = recursive;
}

#include "rsiwidget.moc"
