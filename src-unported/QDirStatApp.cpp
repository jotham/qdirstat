/*
 *   File name:	QDirStatApp.cpp
 *   Summary:	The QDirStat application - menu bar, tool bar, ...
 *   License:   GPL V2 - See file LICENSE for details.
 *
 *   Author:	Stefan Hundhammer <Stefan.Hundhammer@gmx.de>
 */


#include <qclipboard.h>
#include <qpopupmenu.h>
#include <qsplitter.h>

#include "Logger.h"
#include <kaccel.h>
#include <kaction.h>
#include <kapp.h>
#include <kconfig.h>
#include <kfiledialog.h>
#include <kiconloader.h>
#include <kmenubar.h>
#include <kmessagebox.h>
#include <krun.h>
#include <kstatusbar.h>
#include <kstdaction.h>
#include <kurlrequesterdlg.h>

#include "QDirStatApp.h"
#include "CleanupCollection.h"
#include "DirTree.h"
#include "kpacman.h"
#include "KTreeMapView.h"
#include "KTreeMapTile.h"
#include "CleanupCollection.h"
#include "kactivitytracker.h"
#include "DirTreeView.h"
#include "QDirStatSettings.h"
#include "DirTreeCache.h"
#include "ExcludeRules.h"


#define	USER_CLEANUPS	10	// Number of user cleanup actions

#define ID_STATUS_MSG	1
#define ID_PACMAN	42
#define PACMAN_WIDTH	350
#define PACMAN_INTERVAL	75	// millisec

#define INITIAL_FEEDBACK_REMINDER	2000L
#define FEEDBACK_REMINDER_INTERVAL	1000L


using namespace QDirStat;


QDirStatApp::QDirStatApp( QWidget* , const char* name )
    : KMainWindow( 0, name )
{
    // Simple inits

    _activityTracker	= 0;	// Might or might not be needed


    // Those will be created delayed, only when needed

    _settingsDialog 	= 0;
    _feedbackDialog 	= 0;
    _treemapView	= 0;
    _pacMan		= 0;
    _pacManDelimiter	= 0;


    // Set up internal (mainWin -> mainWin) connections

    connect( this,	SIGNAL( readConfig       ( void ) ),
	     this,	SLOT  ( readMainWinConfig( void ) ) );

    connect( this,	SIGNAL( saveConfig       ( void ) ),
	     this,	SLOT  ( saveMainWinConfig( void ) ) );


    // Create main window

    _splitter = new QSplitter( QSplitter::Vertical, this );
    setCentralWidget( _splitter );

    _treeView = new DirTreeView( _splitter );

    connect( _treeView, SIGNAL( progressInfo( const QString & ) ),
	     this,      SLOT  ( statusMsg   ( const QString & ) ) );

    connect( _treeView, SIGNAL( selectionChanged( FileInfo * ) ),
	     this,      SLOT  ( selectionChanged( FileInfo * ) ) );

    connect( _treeView, SIGNAL( contextMenu( DirTreeViewItem *, const QPoint & ) ),
	     this,      SLOT  ( contextMenu( DirTreeViewItem *, const QPoint & ) ) );

    connect( this,	SIGNAL( readConfig() 		), _treeView,	SLOT  ( readConfig() ) );
    connect( this,	SIGNAL( saveConfig() 		), _treeView,	SLOT  ( saveConfig() ) );

    connect( _treeView, SIGNAL( finished()		), this, SLOT( createTreemapViewDelayed() ) );
    connect( _treeView, SIGNAL( aborted()		), this, SLOT( createTreemapViewDelayed() ) );
    connect( _treeView, SIGNAL( startingReading()	), this, SLOT( deleteTreemapView() ) );

    connect( _treeView, SIGNAL( startingReading()	), this, SLOT( updateActions() ) );
    connect( _treeView, SIGNAL( finished()        	), this, SLOT( updateActions() ) );
    connect( _treeView, SIGNAL( aborted()         	), this, SLOT( updateActions() ) );

    // Call inits to invoke all other construction parts

    initStatusBar();
    initActions();
    initCleanups();
    createGUI();
    initActivityTracker();

    _treeViewContextMenu = (QPopupMenu *) factory()->container( "treeViewContextMenu", this );
    _treemapContextMenu	 = (QPopupMenu *) factory()->container( "treemapContextMenu",  this );

    readMainWinConfig();

    // Disable certain actions at startup

    _editCopy->setEnabled( false );
    _reportMailToOwner->setEnabled( false );
    _fileRefreshAll->setEnabled( false );
    _fileRefreshSelected->setEnabled( false );
    updateActions();
}


QDirStatApp::~QDirStatApp()
{
    delete _cleanupCollection;
}



void
QDirStatApp::initActions()
{
    _fileAskOpenDir	= KStdAction::open		( this, SLOT( fileAskOpenDir() ), 		actionCollection() );

    _fileAskOpenUrl	= new KAction( i18n( "Open &URL..." ), "konqueror", 0,
				       this, SLOT( fileAskOpenUrl() ),
				       actionCollection(), "file_open_url" );

    _fileOpenRecent	= KStdAction::openRecent	( this, SLOT( fileOpenRecent( const KURL& ) ),	actionCollection() );
    _fileCloseDir	= KStdAction::close		( this, SLOT( fileCloseDir() ), 		actionCollection() );

    _fileRefreshAll		= new KAction( i18n( "Refresh &All" ), "reload", 0,
					       this, SLOT( refreshAll() ),
					       actionCollection(), "file_refresh_all" );

    _fileRefreshSelected	= new KAction( i18n( "Refresh &Selected" ), 0,
					       this, SLOT( refreshSelected() ),
					       actionCollection(), "file_refresh_selected" );

    _fileReadExcludedDir	= new KAction( i18n( "Read &Excluded Directory" ), 0,
					       this, SLOT( refreshSelected() ),
					       actionCollection(), "file_read_excluded_dir" );

    _fileContinueReadingAtMountPoint = new KAction( i18n( "Continue Reading at &Mount Point" ), "hdd_mount", 0,
						    this, SLOT( refreshSelected() ), actionCollection(),
						    "file_continue_reading_at_mount_point" );

    _fileStopReading	= new KAction( i18n( "Stop Rea&ding" ), "stop", 0,
				       this, SLOT( stopReading() ), actionCollection(),
				       "file_stop_reading" );

    _fileAskWriteCache	= new KAction( i18n( "&Write to Cache File..." ), "fileexport", 0,
				       this, SLOT( askWriteCache() ), actionCollection(),
				       "file_ask_write_cache" );

    _fileAskReadCache	= new KAction( i18n( "&Read Cache File..." ), "fileimport", 0,
				       this, SLOT( askReadCache() ), actionCollection(),
				       "file_ask_read_cache" );

    _fileQuit		= KStdAction::quit		( kapp, SLOT( quit()  		), actionCollection() );
    _editCopy		= KStdAction::copy		( this, SLOT( editCopy() 	), actionCollection() );

    _cleanupOpenWith	= new KAction( i18n( "Open With" ), 0,
				       this, SLOT( cleanupOpenWith() ),
				       actionCollection(), "cleanup_open_with" );

    _treemapZoomIn 	= new KAction( i18n( "Zoom in" ), "viewmag+", Key_Plus,
				       this, SLOT( treemapZoomIn() ),
				       actionCollection(), "treemap_zoom_in" );

    _treemapZoomOut 	= new KAction( i18n( "Zoom out" ), "viewmag-", Key_Minus,
				       this, SLOT( treemapZoomOut() ),
				       actionCollection(), "treemap_zoom_out" );

    _treemapSelectParent= new KAction( i18n( "Select Parent" ), "up", Key_Asterisk,
				       this, SLOT( treemapSelectParent() ),
				       actionCollection(), "treemap_select_parent" );

    _treemapRebuild 	= new KAction( i18n( "Rebuild Treemap" ), 0,
				       this, SLOT( treemapRebuild() ),
				       actionCollection(), "treemap_rebuild" );

    _showTreemapView	= new KToggleAction( i18n( "Show Treemap" ), Key_F9,
					     this, SLOT( toggleTreemapView() ),
					     actionCollection(), "options_show_treemap" );

    new KAction( i18n( "Help about Treemaps" ), "help", 0,
		 this, SLOT( treemapHelp() ),
		 actionCollection(), "treemap_help" );

    KAction * pref	= KStdAction::preferences( this, SLOT( preferences()	), actionCollection() );

    _reportMailToOwner	= new KAction( i18n( "Send &Mail to Owner" ), "mail_generic", 0,
				       _treeView, SLOT( sendMailToOwner() ),
				       actionCollection(), "report_mail_to_owner" );

    _helpSendFeedbackMail = new KAction( i18n( "Send &Feedback Mail..." ), 0,
					 this, SLOT( sendFeedbackMail() ),
					 actionCollection(), "help_send_feedback_mail" );


    _fileAskOpenDir->setStatusText	( i18n( "Opens a directory"	 		) );
    _fileAskOpenUrl->setStatusText	( i18n( "Opens a (possibly remote) directory"	) );
    _fileOpenRecent->setStatusText	( i18n( "Opens a recently used directory"	) );
    _fileCloseDir->setStatusText	( i18n( "Closes the current directory" 		) );
    _fileRefreshAll->setStatusText	( i18n( "Re-reads the entire directory tree"	) );
    _fileRefreshSelected->setStatusText	( i18n( "Re-reads the selected subtree"		) );
    _fileReadExcludedDir->setStatusText ( i18n( "Scan directory tree that was previously excluded" ) );
    _fileContinueReadingAtMountPoint->setStatusText( i18n( "Scan mounted file systems"	) );
    _fileStopReading->setStatusText	( i18n( "Stops directory reading"		) );
    _fileAskWriteCache->setStatusText	( i18n( "Writes the current directory tree to a cache file that can be loaded much faster" ) );
    _fileAskReadCache->setStatusText	( i18n( "Reads a directory tree from a cache file" ) );
    _fileQuit->setStatusText		( i18n( "Quits the application" 		) );
    _editCopy->setStatusText		( i18n( "Copies the URL of the selected item to the clipboard" ) );
    _cleanupOpenWith->setStatusText	( i18n( "Open file or directory with arbitrary application" ) );
    _showTreemapView->setStatusText	( i18n( "Enables/disables the treemap view" 	) );
    _treemapZoomIn->setStatusText	( i18n( "Zoom treemap in"		 	) );
    _treemapZoomOut->setStatusText	( i18n( "Zoom treemap out"		 	) );
    _treemapSelectParent->setStatusText	( i18n( "Select parent"			 	) );
    _treemapRebuild->setStatusText	( i18n( "Rebuild treemap to fit into available space" ) );
    pref->setStatusText			( i18n( "Opens the preferences dialog"		) );
    _reportMailToOwner->setStatusText	( i18n( "Sends a mail to the owner of the selected subtree" ) );
}


void
QDirStatApp::initCleanups()
{
    _cleanupCollection = new CleanupCollection( actionCollection() );
    CHECK_PTR( _cleanupCollection );
    _cleanupCollection->addStdCleanups();
    _cleanupCollection->addUserCleanups( USER_CLEANUPS );
    _cleanupCollection->slotReadConfig();

    connect( _treeView,          SIGNAL( selectionChanged( FileInfo * ) ),
	     _cleanupCollection, SIGNAL( selectionChanged( FileInfo * ) ) );

    connect( this,               SIGNAL( readConfig( void ) ),
	     _cleanupCollection, SIGNAL( readConfig( void ) ) );

    connect( this,               SIGNAL( saveConfig( void ) ),
	     _cleanupCollection, SIGNAL( saveConfig( void ) ) );
}


void
QDirStatApp::revertCleanupsToDefaults()
{
    CleanupCollection defaultCollection;
    defaultCollection.addStdCleanups();
    defaultCollection.addUserCleanups( USER_CLEANUPS );
    *_cleanupCollection = defaultCollection;
}


void
QDirStatApp::initPacMan( bool enablePacMan )
{
    if ( enablePacMan )
    {
	if ( ! _pacMan )
	{
	    _pacMan = new KPacMan( toolBar(), 16, false, "kde toolbar widget" );
	    _pacMan->setInterval( PACMAN_INTERVAL );	// millisec
	    int id = ID_PACMAN;
	    toolBar()->insertWidget( id, PACMAN_WIDTH, _pacMan );
	    toolBar()->setItemAutoSized( id, false );

	    _pacManDelimiter = new QWidget( toolBar() );
	    toolBar()->insertWidget( ++id, 1, _pacManDelimiter );

	    connect( _treeView, SIGNAL( startingReading() ), _pacMan, SLOT( start() ) );
	    connect( _treeView, SIGNAL( finished()        ), _pacMan, SLOT( stop () ) );
	    connect( _treeView, SIGNAL( aborted()         ), _pacMan, SLOT( stop () ) );
	}
    }
    else
    {
	if ( _pacMan )
	{
	    delete _pacMan;
	    _pacMan = 0;
	}

	if ( _pacManDelimiter )
	{
	    delete _pacManDelimiter;
	    _pacManDelimiter = 0;
	}
    }
}


void
QDirStatApp::initStatusBar()
{
    statusBar()->insertItem( i18n( "Ready." ), ID_STATUS_MSG );
}


void
QDirStatApp::initActivityTracker()
{
    if ( ! doFeedbackReminder() )
	return;

    _activityTracker = new KActivityTracker( this, "Feedback",
					     INITIAL_FEEDBACK_REMINDER );

    connect( _activityTracker,  SIGNAL( thresholdReached() ),
	     this,		SLOT  ( askForFeedback() ) );

    connect( _treeView,		SIGNAL( userActivity( int ) ),
	     _activityTracker,	SLOT  ( trackActivity( int ) ) );

    connect( _cleanupCollection, SIGNAL( userActivity( int ) ),
	     _activityTracker,   SLOT  ( trackActivity( int ) ) );
}


void
QDirStatApp::openURL( const KURL& url )
{
    statusMsg( i18n( "Opening directory..." ) );

    _treeView->openURL( url );
    _fileOpenRecent->addURL( url );
    _fileRefreshAll->setEnabled( true );
    setCaption( url.fileName(), false );

    statusMsg( i18n( "Ready." ) );
}


void QDirStatApp::readMainWinConfig()
{

    KConfig * config = kapp->config();
    config->setGroup( "General Options" );

    // Status settings of the various bars and views

    _showTreemapView->setChecked( config->readBoolEntry( "Show Treemap", true ) );
    toggleTreemapView();


    // Position settings of the various bars

    KToolBar::BarPosition toolBarPos;
    toolBarPos = ( KToolBar::BarPosition ) config->readNumEntry( "ToolBarPos", KToolBar::Top );
    toolBar( "mainToolBar" )->setBarPos( toolBarPos );

    _treemapViewHeight = config->readNumEntry( "TreemapViewHeight", 250 );

    // initialize the recent file list
    _fileOpenRecent->loadEntries( config,"Recent Files" );

    QSize size = config->readSizeEntry( "Geometry" );

    if( ! size.isEmpty() )
	resize( size );

    config->setGroup( "Animation" );
    initPacMan( config->readBoolEntry( "ToolbarPacMan", true ) );
    _treeView->enablePacManAnimation( config->readBoolEntry( "DirTreePacMan", false ) );

    config->setGroup( "Exclude" );
    QStringList excludeRules = config->readListEntry ( "ExcludeRules" );
    ExcludeRules::excludeRules()->clear();
    
    for ( QStringList::Iterator it = excludeRules.begin(); it != excludeRules.end(); ++it )
    {
	QString ruleText = *it;
	ExcludeRules::excludeRules()->add( new ExcludeRule( QRegExp( ruleText ) ) );
	logDebug() << "Adding exclude rule: " << ruleText << endl;
    }

    if ( excludeRules.size() == 0 )
	logDebug() << "No exclude rules defined" << endl;
}


void
QDirStatApp::saveMainWinConfig()
{
    KConfig * config = kapp->config();

    config->setGroup( "General Options" );

    config->writeEntry( "Geometry", 		size() );
    config->writeEntry( "Show Treemap",		_showTreemapView->isChecked() );
    config->writeEntry( "ToolBarPos", 		(int) toolBar( "mainToolBar" )->barPos() );

    if ( _treemapView )
	config->writeEntry( "TreemapViewHeight", _treemapView->height() );

    _fileOpenRecent->saveEntries( config,"Recent Files" );
}


void
QDirStatApp::saveProperties( KConfig *config )
{
    (void) config;
    // TODO
}


void
QDirStatApp::readProperties( KConfig *config )
{
    (void) config;
    // TODO
}


bool
QDirStatApp::queryClose()
{
    return true;
}

bool
QDirStatApp::queryExit()
{
    emit saveConfig();

    return true;
}


//============================================================================
//				     Slots
//============================================================================


void
QDirStatApp::fileAskOpenDir()
{
    statusMsg( i18n( "Opening directory..." ) );

    KURL url = KFileDialog::getExistingDirectory( QString::null, this, i18n( "Open Directory..." ) );

    if( ! url.isEmpty() )
	openURL( fixedUrl( url.url() ) );

    statusMsg( i18n( "Ready." ) );
}


void
QDirStatApp::fileAskOpenUrl()
{
    statusMsg( i18n( "Opening URL..." ) );

    KURL url = KURLRequesterDlg::getURL( QString::null,	// startDir
					 this, i18n( "Open URL..." ) );

    if( ! url.isEmpty() )
	openURL( fixedUrl( url.url() ) );

    statusMsg( i18n( "Ready." ) );
}


void
QDirStatApp::fileOpenRecent( const KURL& url )
{
    statusMsg( i18n( "Opening directory..." ) );

    if( ! url.isEmpty() )
	openURL( fixedUrl( url.url() ) );

    statusMsg( i18n( "Ready." ) );
}


void
QDirStatApp::fileCloseDir()
{
    statusMsg( i18n( "Closing directory..." ) );

    _treeView->clear();
    _fileRefreshAll->setEnabled( false );
    close();

    statusMsg( i18n( "Ready." ) );
}


void
QDirStatApp::refreshAll()
{
    statusMsg( i18n( "Refreshing directory tree..." ) );
    _treeView->refreshAll();
    statusMsg( i18n( "Ready." ) );
}


void
QDirStatApp::refreshSelected()
{
    if ( ! _treeView->selection() )
	return;

    statusMsg( i18n( "Refreshing selected subtree..." ) );
    _treeView->refreshSelected();
    statusMsg( i18n( "Ready." ) );
}


void
QDirStatApp::stopReading()
{
    _treeView->abortReading();
}


void
QDirStatApp::askWriteCache()
{
    QString file_name;

    do
    {
	file_name =
	    KFileDialog::getSaveFileName( DEFAULT_CACHE_NAME, 			// startDir
					  QString::null,			// filter
					  this,					// parent
					  i18n( "Write to Cache File" ) );	// caption

	if ( file_name.isEmpty() )		// user hit "cancel"
	    return;

	if ( access( file_name, F_OK ) == 0 )	// file exists
	{
	    int button =
		KMessageBox::questionYesNoCancel( this,
						  i18n( "File %1 exists. Overwrite?" ).arg( file_name ),
						  i18n( "Overwrite?" ) );	// caption

	    if ( button == KMessageBox::Cancel 	)	return;
	    if ( button == KMessageBox::No	)	file_name = "";
	}
    } while ( file_name.isEmpty() );

    statusMsg( i18n( "Writing cache file..." ) );

    if ( ! _treeView || ! _treeView->writeCache( file_name ) )
    {
	QString errMsg = i18n( "Error writing cache file %1" ).arg( file_name );
	statusMsg( errMsg );
	KMessageBox::sorry( this, errMsg,
			    i18n( "Write Error" ) );			// caption
    }

    statusMsg( i18n( "Wrote cache file %1" ).arg( file_name ) );
}


void
QDirStatApp::askReadCache()
{
    QString file_name =
	KFileDialog::getOpenFileName( DEFAULT_CACHE_NAME,		// startDir
				      QString::null,			// filter
				      this,				// parent
				      i18n( "Read Cache File" ) );	// caption

    statusMsg( i18n( "Reading cache file..." ) );

    if ( _treeView )
    {
	_fileRefreshAll->setEnabled( true );
	_treeView->readCache( file_name );
    }
}



void
QDirStatApp::editCopy()
{
    if ( _treeView->selection() )
	kapp->clipboard()->setText( QString::fromLocal8Bit(_treeView->selection()->orig()->url()) );

#if 0
#warning debug
    if ( _activityTracker )
	_activityTracker->trackActivity( 800 );
#endif
}


void
QDirStatApp::cleanupOpenWith()
{
    if ( ! _treeView->selection() )
	return;

    FileInfo * sel = _treeView->selection()->orig();

    if ( sel->isDotEntry() )
	return;

    KURL::List urlList( KURL( sel->url()  ) );
    KRun::displayOpenWithDialog( urlList, false );
}


void
QDirStatApp::selectionChanged( FileInfo *selection )
{
    if ( selection )
    {
	_editCopy->setEnabled( true );
	_reportMailToOwner->setEnabled( true );
	_fileRefreshSelected->setEnabled( ! selection->isDotEntry() );
	_cleanupOpenWith->setEnabled( ! selection->isDotEntry() );
	_fileReadExcludedDir->setEnabled( selection->isExcluded() );

	if ( selection->isMountPoint() &&
	     selection->readState() == KDirOnRequestOnly )
	{
	    _fileContinueReadingAtMountPoint->setEnabled( true );
	}
	else
	    _fileContinueReadingAtMountPoint->setEnabled( false );

	statusMsg( QString::fromLocal8Bit(selection->url()) );
    }
    else
    {
	_editCopy->setEnabled( false );
	_reportMailToOwner->setEnabled( false );
	_fileRefreshSelected->setEnabled( false );
	_fileContinueReadingAtMountPoint->setEnabled( false );
	_cleanupOpenWith->setEnabled( false );
	statusMsg( "" );
    }

    updateActions();
}


void
QDirStatApp::updateActions()
{
    _treemapZoomIn->setEnabled ( _treemapView && _treemapView->canZoomIn() );
    _treemapZoomOut->setEnabled( _treemapView && _treemapView->canZoomOut() );
    _treemapRebuild->setEnabled( _treemapView && _treemapView->rootTile() );
    _treemapSelectParent->setEnabled( _treemapView && _treemapView->canSelectParent() );

    if ( _treeView->tree() && _treeView->tree()->isBusy() )
	_fileStopReading->setEnabled( true );
    else
	_fileStopReading->setEnabled( false );
}


void
QDirStatApp::treemapZoomIn()
{
    if ( _treemapView )
    {
	_treemapView->zoomIn();
	updateActions();
    }
}


void
QDirStatApp::treemapZoomOut()
{
    if ( _treemapView )
    {
	_treemapView->zoomOut();
	updateActions();
    }
}


void
QDirStatApp::treemapSelectParent()
{
    if ( _treemapView )
    {
	_treemapView->selectParent();
	updateActions();
    }
}


void
QDirStatApp::treemapRebuild()
{
    if ( _treemapView )
    {
	_treemapView->rebuildTreemap();
	updateActions();
    }
}


void
QDirStatApp::treemapHelp()
{
    kapp->invokeHelp( "treemap_intro" );
}


void
QDirStatApp::toggleTreemapView()
{
    if   ( _showTreemapView->isChecked() )
    {
	if ( ! _treemapView )
	    createTreemapView();
    }
    else
    {
	if ( _treemapView )
	    deleteTreemapView();
    }
}


void
QDirStatApp::preferences()
{
    if ( ! _settingsDialog )
    {
	_settingsDialog = new QDirStat::SettingsDialog( this );
	CHECK_PTR( _settingsDialog );
    }

    if ( ! _settingsDialog->isVisible() )
	_settingsDialog->show();
}


void
QDirStatApp::askForFeedback()
{
    if ( ! doFeedbackReminder() )
	return;

    KConfig * config = kapp->config();

    switch ( KMessageBox::warningYesNoCancel( this,
					      i18n( "Now that you know this program for some time,\n"
						    "wouldn't you like to tell the authors your opinion about it?\n"
						    "\n"
						    "Open Source software depends on user feedback.\n"
						    "Your opinion can help us make the software better." ),
					      i18n( "Please tell us your opinion!" ),	// caption
					      i18n( "Open &Feedback Form..." ),		// yesButton
					      i18n( "&No, and don't ask again!" )	// noButton
					      )
	     )
    {
	case KMessageBox::Yes:
	    sendFeedbackMail();
	    break;

	case KMessageBox::No:	// ...and don't ask again
	    config->setGroup( "Feedback" );
	    config->writeEntry( "dontAsk", true );
	    config->sync();	// make sure this doesn't get lost even if the app is killed or crashes
	    break;

	case KMessageBox::Cancel:
	    break;
    }

    config->setGroup( "Feedback" );
    int  remindersCount = config->readNumEntry ( "remindersCount", 0 );
    config->writeEntry( "remindersCount", ++remindersCount );

    if ( _activityTracker )
    {
	_activityTracker->setThreshold( _activityTracker->threshold()
					+ FEEDBACK_REMINDER_INTERVAL );
    }
}


void
QDirStatApp::feedbackMailSent()
{
    KConfig * config = kapp->config();
    config->setGroup( "Feedback" );
    config->writeEntry( "mailSent", true );
    config->sync();
}


bool
QDirStatApp::doFeedbackReminder()
{
    KConfig * config = kapp->config();
    config->setGroup( "Feedback" );

    bool mailSent	= config->readBoolEntry( "mailSent", false );
    bool dontAsk	= config->readBoolEntry( "dontAsk", false );
    int  remindersCount = config->readNumEntry ( "remindersCount", 0 );

    return !mailSent && !dontAsk && remindersCount < 5;
}


void
QDirStatApp::statusMsg( const QString &text )
{
    // Change status message permanently

    statusBar()->clear();
    statusBar()->changeItem( text, ID_STATUS_MSG );
}


void
QDirStatApp::contextMenu( DirTreeViewItem * item, const QPoint &pos )
{
    NOT_USED( item );

    if ( _treeViewContextMenu )
	_treeViewContextMenu->popup( pos );
}


void
QDirStatApp::contextMenu( KTreemapTile * tile, const QPoint &pos )
{
    NOT_USED( tile );

    if ( _treemapContextMenu )
	_treemapContextMenu->popup( pos );
}


void
QDirStatApp::createTreemapViewDelayed()
{
    QTimer::singleShot( 0, this, SLOT( createTreemapView() ) );
}


void
QDirStatApp::createTreemapView()
{
    if ( ! _showTreemapView->isChecked() || ! _treeView->tree() )
	return;

    if ( _treemapView )
	delete _treemapView;

    // logDebug() << "Creating KTreemapView" << endl;
    _treemapView = new KTreemapView( _treeView->tree(), _splitter,
				     QSize( _splitter->width(), _treemapViewHeight ) );
    CHECK_PTR( _treemapView );

    connect( _treemapView,	SIGNAL( contextMenu( KTreemapTile *, const QPoint & ) ),
	     this,		SLOT  ( contextMenu( KTreemapTile *, const QPoint & ) ) );

    connect( _treemapView,	SIGNAL( treemapChanged()	),
	     this,		SLOT  ( updateActions()	) 	);

    connect( _treemapView,	SIGNAL( selectionChanged( FileInfo * ) ),
	     this,      	SLOT  ( selectionChanged( FileInfo * ) ) );

    if ( _activityTracker )
    {
	connect( _treemapView,		SIGNAL( userActivity ( int )	),
		 _activityTracker,	SLOT  ( trackActivity( int ) 	) );
    }

    _treemapView->show(); // QSplitter needs explicit show() for new children
    updateActions();
}


void
QDirStatApp::deleteTreemapView()
{
    if ( _treemapView )
    {
	// logDebug() << "Deleting KTreemapView" << endl;
	_treemapViewHeight = _treemapView->height();

	delete _treemapView;
	_treemapView = 0;
    }

    updateActions();
}



// EOF
