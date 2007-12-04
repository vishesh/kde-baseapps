/***************************************************************************
 *   Copyright (C) 2006 by Peter Penz <peter.penz@gmx.at>                  *
 *   Copyright (C) 2006 by Stefan Monov <logixoul@gmail.com>               *
 *   Copyright (C) 2006 by Cvetoslav Ludmiloff <ludmiloff@gmail.com>       *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA            *
 ***************************************************************************/

#include "dolphinmainwindow.h"

#include <config-nepomuk.h>

#include "dolphinapplication.h"
#include "dolphinnewmenu.h"
#include "dolphinsettings.h"
#include "dolphinsettingsdialog.h"
#include "dolphinstatusbar.h"
#include "dolphinviewcontainer.h"
#include "infosidebarpage.h"
#include "metadatawidget.h"
#include "mainwindowadaptor.h"
#include "terminalsidebarpage.h"
#include "treeviewsidebarpage.h"
#include "kurlnavigator.h"
#include "viewpropertiesdialog.h"
#include "viewproperties.h"
#include "kfileplacesmodel.h"
#include "kfileplacesview.h"

#include "dolphin_generalsettings.h"
#include "dolphin_iconsmodesettings.h"

#include <kaction.h>
#include <kactioncollection.h>
#include <kconfig.h>
#include <kdesktopfile.h>
#include <kdeversion.h>
#include <kfiledialog.h>
#include <kglobal.h>
#include <kicon.h>
#include <kiconloader.h>
#include <kio/netaccess.h>
#include <kinputdialog.h>
#include <klocale.h>
#include <kmenu.h>
#include <kmenubar.h>
#include <kmessagebox.h>
#include <konqmimedata.h>
#include <konq_operations.h>
#include <kpropertiesdialog.h>
#include <kprotocolinfo.h>
#include <ktoggleaction.h>
#include <krun.h>
#include <kshell.h>
#include <kstandarddirs.h>
#include <kstatusbar.h>
#include <kstandardaction.h>
#include <kurl.h>

#include <QtGui/QKeyEvent>
#include <QtGui/QClipboard>
#include <QtGui/QSplitter>
#include <QtGui/QDockWidget>

DolphinMainWindow::DolphinMainWindow(int id) :
    KXmlGuiWindow(0),
    m_newMenu(0),
    m_showMenuBar(0),
    m_splitter(0),
    m_activeViewContainer(0),
    m_id(id)
{
    setObjectName("Dolphin");
    m_viewContainer[PrimaryView] = 0;
    m_viewContainer[SecondaryView] = 0;

    new MainWindowAdaptor(this);
    QDBusConnection::sessionBus().registerObject(QString("/dolphin/MainWindow%1").arg(m_id), this);

    KonqFileUndoManager::incRef();

    KonqFileUndoManager* undoManager = KonqFileUndoManager::self();
    undoManager->setUiInterface(new UndoUiInterface(this));

    connect(undoManager, SIGNAL(undoAvailable(bool)),
            this, SLOT(slotUndoAvailable(bool)));
    connect(undoManager, SIGNAL(undoTextChanged(const QString&)),
            this, SLOT(slotUndoTextChanged(const QString&)));
    connect(DolphinSettings::instance().placesModel(), SIGNAL(errorMessage(const QString&)),
            this, SLOT(slotHandlePlacesError(const QString&)));
}

DolphinMainWindow::~DolphinMainWindow()
{
    KonqFileUndoManager::decRef();
    DolphinApplication::app()->removeMainWindow(this);
}

void DolphinMainWindow::toggleViews()
{
    if (m_viewContainer[SecondaryView] == 0) {
        return;
    }

    // move secondary view from the last position of the splitter
    // to the first position
    m_splitter->insertWidget(0, m_viewContainer[SecondaryView]);

    DolphinViewContainer* container = m_viewContainer[PrimaryView];
    m_viewContainer[PrimaryView] = m_viewContainer[SecondaryView];
    m_viewContainer[SecondaryView] = container;
}

void DolphinMainWindow::slotDoingOperation(KonqFileUndoManager::CommandType commandType)
{
    clearStatusBar();
    m_undoCommandTypes.append(commandType);
}

void DolphinMainWindow::refreshViews()
{
    Q_ASSERT(m_viewContainer[PrimaryView] != 0);

    // remember the current active view, as because of
    // the refreshing the active view might change to
    // the secondary view
    DolphinViewContainer* activeViewContainer = m_activeViewContainer;

    m_viewContainer[PrimaryView]->view()->refresh();
    if (m_viewContainer[SecondaryView] != 0) {
        m_viewContainer[SecondaryView]->view()->refresh();
    }

    setActiveViewContainer(activeViewContainer);
}

void DolphinMainWindow::dropUrls(const KUrl::List& urls,
                                 const KUrl& destination)
{
    kDebug() << "Source" << urls;
    kDebug() << "Destination:" << destination;

    Qt::DropAction action = Qt::CopyAction;

    Qt::KeyboardModifiers modifier = QApplication::keyboardModifiers();
    const bool shiftPressed   = modifier & Qt::ShiftModifier;
    const bool controlPressed = modifier & Qt::ControlModifier;
    if (shiftPressed && controlPressed) {
        // shortcut for 'Link Here' is used
        action = Qt::LinkAction;
    } else if (shiftPressed) {
        // shortcut for 'Move Here' is used
        action = Qt::MoveAction;
    } else if (controlPressed) {
        // shortcut for 'Copy Here' is used
        action = Qt::CopyAction;
    } else {
        // open a context menu which offers the following actions:
        // - Move Here
        // - Copy Here
        // - Link Here
        // - Cancel

        KMenu popup(this);

        QString seq = QKeySequence(Qt::ShiftModifier).toString();
        seq.chop(1); // chop superfluous '+'
        QAction* moveAction = popup.addAction(KIcon("go-jump"),
                                              i18nc("@action:inmenu",
                                                    "&Move Here\t<shortcut>%1</shortcut>", seq));

        seq = QKeySequence(Qt::ControlModifier).toString();
        seq.chop(1);
        QAction* copyAction = popup.addAction(KIcon("edit-copy"),
                                              i18nc("@action:inmenu",
                                                    "&Copy Here\t<shortcut>%1</shortcut>", seq));

        seq = QKeySequence(Qt::ControlModifier + Qt::ShiftModifier).toString();
        seq.chop(1);
        QAction* linkAction = popup.addAction(KIcon("insert-link"),
                                              i18nc("@action:inmenu",
                                                    "&Link Here\t<shortcut>%1</shortcut>", seq));

        popup.addSeparator();
        popup.addAction(KIcon("process-stop"), i18nc("@action:inmenu", "Cancel"));

        QAction* activatedAction = popup.exec(QCursor::pos());
        if (activatedAction == moveAction) {
            action = Qt::MoveAction;
        } else if (activatedAction == copyAction) {
            action = Qt::CopyAction;
        } else if (activatedAction == linkAction) {
            action = Qt::LinkAction;
        } else {
            return;
        }
    }

    switch (action) {
    case Qt::MoveAction:
        KonqOperations::copy(this, KonqOperations::MOVE, urls, destination);
        m_undoCommandTypes.append(KonqFileUndoManager::MOVE);
        break;

    case Qt::CopyAction:
        KonqOperations::copy(this, KonqOperations::COPY, urls, destination);
        m_undoCommandTypes.append(KonqFileUndoManager::COPY);
        break;

    case Qt::LinkAction:
        KonqOperations::copy(this, KonqOperations::LINK, urls, destination);
        m_undoCommandTypes.append(KonqFileUndoManager::LINK);
        break;

    default:
        break;
    }
}

void DolphinMainWindow::changeUrl(const KUrl& url)
{
    DolphinViewContainer* view = activeViewContainer();
    if (view != 0) {
        view->setUrl(url);
        updateEditActions();
        updateViewActions();
        updateGoActions();
        setCaption(url.fileName());
        emit urlChanged(url);
    }
}

void DolphinMainWindow::changeSelection(const KFileItemList& selection)
{
    activeViewContainer()->view()->changeSelection(selection);
}

void DolphinMainWindow::slotViewModeChanged()
{
    updateViewActions();
}

void DolphinMainWindow::slotShowPreviewChanged()
{
    // It is not enough to update the 'Show Preview' action, also
    // the 'Zoom In' and 'Zoom Out' actions must be adapted.
    updateViewActions();
}

void DolphinMainWindow::slotShowHiddenFilesChanged()
{
    KToggleAction* showHiddenFilesAction =
        static_cast<KToggleAction*>(actionCollection()->action("show_hidden_files"));
    const DolphinView* view = m_activeViewContainer->view();
    showHiddenFilesAction->setChecked(view->showHiddenFiles());
}

void DolphinMainWindow::slotCategorizedSortingChanged()
{
    KToggleAction* showInGroupsAction =
        static_cast<KToggleAction*>(actionCollection()->action("show_in_groups"));
    const DolphinView* view = m_activeViewContainer->view();
    showInGroupsAction->setChecked(view->categorizedSorting());
    showInGroupsAction->setEnabled(view->supportsCategorizedSorting());
}

void DolphinMainWindow::slotSortingChanged(DolphinView::Sorting sorting)
{
    QAction* action = 0;
    switch (sorting) {
    case DolphinView::SortByName:
        action = actionCollection()->action("sort_by_name");
        break;
    case DolphinView::SortBySize:
        action = actionCollection()->action("sort_by_size");
        break;
    case DolphinView::SortByDate:
        action = actionCollection()->action("sort_by_date");
        break;
    case DolphinView::SortByPermissions:
        action = actionCollection()->action("sort_by_permissions");
        break;
    case DolphinView::SortByOwner:
        action = actionCollection()->action("sort_by_owner");
        break;
    case DolphinView::SortByGroup:
        action = actionCollection()->action("sort_by_group");
        break;
    case DolphinView::SortByType:
        action = actionCollection()->action("sort_by_type");
        break;
#ifdef HAVE_NEPOMUK
    case DolphinView::SortByRating:
        action = actionCollection()->action("sort_by_rating");
        break;
    case DolphinView::SortByTags:
        action = actionCollection()->action("sort_by_tags");
        break;
#endif
    default:
        break;
    }

    if (action != 0) {
        KToggleAction* toggleAction = static_cast<KToggleAction*>(action);
        toggleAction->setChecked(true);
    }
}

void DolphinMainWindow::slotSortOrderChanged(Qt::SortOrder order)
{
    KToggleAction* descending = static_cast<KToggleAction*>(actionCollection()->action("descending"));
    const bool sortDescending = (order == Qt::DescendingOrder);
    descending->setChecked(sortDescending);
}

void DolphinMainWindow::slotAdditionalInfoChanged(KFileItemDelegate::InformationList list)
{
    QAction* showSizeInfo = actionCollection()->action("show_size_info");
    QAction* showDateInfo = actionCollection()->action("show_date_info");
    QAction* showPermissionsInfo = actionCollection()->action("show_permissions_info");
    QAction* showOwnerInfo = actionCollection()->action("show_owner_info");
    QAction* showGroupInfo = actionCollection()->action("show_group_info");
    QAction* showMimeInfo = actionCollection()->action("show_mime_info");

    showSizeInfo->setChecked(false);
    showDateInfo->setChecked(false);
    showPermissionsInfo->setChecked(false);
    showOwnerInfo->setChecked(false);
    showGroupInfo->setChecked(false);
    showMimeInfo->setChecked(false);

    const DolphinView* view = m_activeViewContainer->view();

    const IconsModeSettings* settings = DolphinSettings::instance().iconsModeSettings();
    const bool enable = (view->mode() == DolphinView::DetailsView) ||
                         ((view->mode() == DolphinView::IconsView) && settings->showAdditionalInfo());

    showSizeInfo->setEnabled(enable);
    showDateInfo->setEnabled(enable);
    showPermissionsInfo->setEnabled(enable);
    showOwnerInfo->setEnabled(enable);
    showGroupInfo->setEnabled(enable);
    showMimeInfo->setEnabled(enable);

    foreach (KFileItemDelegate::Information info, list) {
        switch (info) {
        case KFileItemDelegate::Size:
            showSizeInfo->setChecked(true);
            break;
        case KFileItemDelegate::ModificationTime:
            showDateInfo->setChecked(true);
            break;
        case KFileItemDelegate::Permissions:
            showPermissionsInfo->setChecked(true);
            break;
        case KFileItemDelegate::Owner:
            showOwnerInfo->setChecked(true);
            break;
        case KFileItemDelegate::OwnerAndGroup:
            showGroupInfo->setChecked(true);
            break;
        case KFileItemDelegate::FriendlyMimeType:
            showMimeInfo->setChecked(true);
            break;
        default:
            break;
        }
    }
}

void DolphinMainWindow::slotSelectionChanged(const KFileItemList& selection)
{
    updateEditActions();

    Q_ASSERT(m_viewContainer[PrimaryView] != 0);
    int selectedUrlsCount = m_viewContainer[PrimaryView]->view()->selectedUrls().count();
    if (m_viewContainer[SecondaryView] != 0) {
        selectedUrlsCount += m_viewContainer[SecondaryView]->view()->selectedUrls().count();
    }

    QAction* compareFilesAction = actionCollection()->action("compare_files");
    compareFilesAction->setEnabled(selectedUrlsCount == 2);

    m_activeViewContainer->updateStatusBar();

    emit selectionChanged(selection);
}

void DolphinMainWindow::slotRequestItemInfo(const KFileItem& item)
{
    emit requestItemInfo(item);
}

void DolphinMainWindow::slotHistoryChanged()
{
    updateHistory();
}

void DolphinMainWindow::updateFilterBarAction(bool show)
{
    KToggleAction* showFilterBarAction =
        static_cast<KToggleAction*>(actionCollection()->action("show_filter_bar"));
    showFilterBarAction->setChecked(show);
}

void DolphinMainWindow::openNewMainWindow()
{
    DolphinApplication::app()->createMainWindow()->show();
}

void DolphinMainWindow::toggleActiveView()
{
    if (m_viewContainer[SecondaryView] == 0) {
        // only one view is available
        return;
    }

    Q_ASSERT(m_activeViewContainer != 0);
    Q_ASSERT(m_viewContainer[PrimaryView] != 0);

    DolphinViewContainer* left  = m_viewContainer[PrimaryView];
    DolphinViewContainer* right = m_viewContainer[SecondaryView];
    setActiveViewContainer(m_activeViewContainer == right ? left : right);
}

void DolphinMainWindow::closeEvent(QCloseEvent* event)
{
    DolphinSettings& settings = DolphinSettings::instance();
    GeneralSettings* generalSettings = settings.generalSettings();
    generalSettings->setFirstRun(false);

    settings.save();

    KXmlGuiWindow::closeEvent(event);
}

void DolphinMainWindow::saveProperties(KConfigGroup& group)
{
    DolphinViewContainer* cont = m_viewContainer[PrimaryView];
    group.writeEntry("Primary Url", cont->url().url());
    group.writeEntry("Primary Editable Url", cont->isUrlEditable());

    cont = m_viewContainer[SecondaryView];
    if (cont != 0) {
        group.writeEntry("Secondary Url", cont->url().url());
        group.writeEntry("Secondary Editable Url", cont->isUrlEditable());
    }
}

void DolphinMainWindow::readProperties(const KConfigGroup& group)
{
    DolphinViewContainer* cont = m_viewContainer[PrimaryView];

    cont->setUrl(group.readEntry("Primary Url"));
    bool editable = group.readEntry("Primary Editable Url", false);
    cont->urlNavigator()->setUrlEditable(editable);

    cont = m_viewContainer[SecondaryView];
    const QString secondaryUrl = group.readEntry("Secondary Url");
    if (!secondaryUrl.isEmpty()) {
        if (cont == 0) {
            // a secondary view should be shown, but no one is available
            // currently -> create a new view
            toggleSplitView();
            cont = m_viewContainer[SecondaryView];
            Q_ASSERT(cont != 0);
        }

        cont->setUrl(secondaryUrl);
        bool editable = group.readEntry("Secondary Editable Url", false);
        cont->urlNavigator()->setUrlEditable(editable);
    } else if (cont != 0) {
        // no secondary view should be shown, but the default setting shows
        // one already -> close the view
        toggleSplitView();
    }
}

void DolphinMainWindow::updateNewMenu()
{
    m_newMenu->slotCheckUpToDate();
    m_newMenu->setPopupFiles(activeViewContainer()->url());
}

void DolphinMainWindow::rename()
{
    clearStatusBar();
    m_activeViewContainer->view()->renameSelectedItems();
}

void DolphinMainWindow::moveToTrash()
{
    clearStatusBar();

    DolphinView* view = m_activeViewContainer->view();

    if (QApplication::keyboardModifiers() & Qt::ShiftModifier) {
        view->deleteSelectedItems();
    } else {
        view->trashSelectedItems();
    }
}

void DolphinMainWindow::deleteItems()
{
    clearStatusBar();
    m_activeViewContainer->view()->deleteSelectedItems();
}

void DolphinMainWindow::properties()
{
    const KFileItemList list = m_activeViewContainer->view()->selectedItems();

    KPropertiesDialog *dialog = new KPropertiesDialog(list, this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->show();
    dialog->raise();
    dialog->activateWindow();
}

void DolphinMainWindow::quit()
{
    close();
}

void DolphinMainWindow::slotHandlePlacesError(const QString &message)
{
    if (!message.isEmpty()) {
        DolphinStatusBar* statusBar = m_activeViewContainer->statusBar();
        statusBar->setMessage(message, DolphinStatusBar::Error);
    }
}

void DolphinMainWindow::slotUndoAvailable(bool available)
{
    QAction* undoAction = actionCollection()->action(KStandardAction::stdName(KStandardAction::Undo));
    if (undoAction != 0) {
        undoAction->setEnabled(available);
    }

    if (available && (m_undoCommandTypes.count() > 0)) {
        const KonqFileUndoManager::CommandType command = m_undoCommandTypes.takeFirst();
        DolphinStatusBar* statusBar = m_activeViewContainer->statusBar();
        switch (command) {
        case KonqFileUndoManager::COPY:
            statusBar->setMessage(i18nc("@info:status", "Copy operation completed."),
                                  DolphinStatusBar::OperationCompleted);
            break;
        case KonqFileUndoManager::MOVE:
            statusBar->setMessage(i18nc("@info:status", "Move operation completed."),
                                  DolphinStatusBar::OperationCompleted);
            break;
        case KonqFileUndoManager::LINK:
            statusBar->setMessage(i18nc("@info:status", "Link operation completed."),
                                  DolphinStatusBar::OperationCompleted);
            break;
        case KonqFileUndoManager::TRASH:
            statusBar->setMessage(i18nc("@info:status", "Move to trash operation completed."),
                                  DolphinStatusBar::OperationCompleted);
            break;
        case KonqFileUndoManager::RENAME:
            statusBar->setMessage(i18nc("@info:status", "Renaming operation completed."),
                                  DolphinStatusBar::OperationCompleted);
            break;

        case KonqFileUndoManager::MKDIR:
            statusBar->setMessage(i18nc("@info:status", "Created folder."),
                                  DolphinStatusBar::OperationCompleted);
            break;

        default:
            break;
        }

    }
}

void DolphinMainWindow::slotUndoTextChanged(const QString& text)
{
    QAction* undoAction = actionCollection()->action(KStandardAction::stdName(KStandardAction::Undo));
    if (undoAction != 0) {
        undoAction->setText(text);
    }
}

void DolphinMainWindow::undo()
{
    clearStatusBar();
    KonqFileUndoManager::self()->undo();
}

void DolphinMainWindow::cut()
{
    m_activeViewContainer->view()->cutSelectedItems();
}

void DolphinMainWindow::copy()
{
    m_activeViewContainer->view()->copySelectedItems();
}

void DolphinMainWindow::paste()
{
    m_activeViewContainer->view()->paste();
}

void DolphinMainWindow::updatePasteAction()
{
    QAction* pasteAction = actionCollection()->action(KStandardAction::stdName(KStandardAction::Paste));
    if (pasteAction == 0) {
        return;
    }

    QPair<bool, QString> pasteInfo = m_activeViewContainer->view()->pasteInfo();
    pasteAction->setEnabled(pasteInfo.first);
    pasteAction->setText(pasteInfo.second);
}

void DolphinMainWindow::selectAll()
{
    clearStatusBar();
    m_activeViewContainer->view()->selectAll();
}

void DolphinMainWindow::invertSelection()
{
    clearStatusBar();
    m_activeViewContainer->view()->invertSelection();
}

void DolphinMainWindow::setViewMode(QAction* action)
{
    const DolphinView::Mode mode = action->data().value<DolphinView::Mode>();
    m_activeViewContainer->view()->setMode(mode);
}

void DolphinMainWindow::sortByName()
{
    m_activeViewContainer->view()->setSorting(DolphinView::SortByName);
}

void DolphinMainWindow::sortBySize()
{
    m_activeViewContainer->view()->setSorting(DolphinView::SortBySize);
}

void DolphinMainWindow::sortByDate()
{
    m_activeViewContainer->view()->setSorting(DolphinView::SortByDate);
}

void DolphinMainWindow::sortByPermissions()
{
    m_activeViewContainer->view()->setSorting(DolphinView::SortByPermissions);
}

void DolphinMainWindow::sortByOwner()
{
    m_activeViewContainer->view()->setSorting(DolphinView::SortByOwner);
}

void DolphinMainWindow::sortByGroup()
{
    m_activeViewContainer->view()->setSorting(DolphinView::SortByGroup);
}

void DolphinMainWindow::sortByType()
{
    m_activeViewContainer->view()->setSorting(DolphinView::SortByType);
}

void DolphinMainWindow::sortByRating()
{
#ifdef HAVE_NEPOMUK
    m_activeViewContainer->view()->setSorting(DolphinView::SortByRating);
#endif
}

void DolphinMainWindow::sortByTags()
{
#ifdef HAVE_NEPOMUK
    m_activeViewContainer->view()->setSorting(DolphinView::SortByTags);
#endif
}

void DolphinMainWindow::toggleSortOrder()
{
    DolphinView* view = m_activeViewContainer->view();
    const Qt::SortOrder order = (view->sortOrder() == Qt::AscendingOrder) ?
                                Qt::DescendingOrder :
                                Qt::AscendingOrder;
    view->setSortOrder(order);
}

void DolphinMainWindow::toggleSortCategorization()
{
    DolphinView* view = m_activeViewContainer->view();
    const bool categorizedSorting = view->categorizedSorting();
    view->setCategorizedSorting(!categorizedSorting);
}

void DolphinMainWindow::toggleSizeInfo()
{
    toggleAdditionalInfo("show_size_info", KFileItemDelegate::Size);
}

void DolphinMainWindow::toggleDateInfo()
{
    toggleAdditionalInfo("show_date_info", KFileItemDelegate::ModificationTime);
}

void DolphinMainWindow::togglePermissionsInfo()
{
    toggleAdditionalInfo("show_permissions_info", KFileItemDelegate::Permissions);
}

void DolphinMainWindow::toggleOwnerInfo()
{
    toggleAdditionalInfo("show_owner_info", KFileItemDelegate::Owner);
}

void DolphinMainWindow::toggleGroupInfo()
{
    toggleAdditionalInfo("show_group_info", KFileItemDelegate::OwnerAndGroup);
}

void DolphinMainWindow::toggleMimeInfo()
{
    toggleAdditionalInfo("show_mime_info", KFileItemDelegate::FriendlyMimeType);
}

void DolphinMainWindow::toggleSplitView()
{
    if (m_viewContainer[SecondaryView] == 0) {
        // create a secondary view
        const int newWidth = (m_viewContainer[PrimaryView]->width() - m_splitter->handleWidth()) / 2;

        const DolphinView* view = m_viewContainer[PrimaryView]->view();
        m_viewContainer[SecondaryView] = new DolphinViewContainer(this, 0, view->rootUrl());
        connectViewSignals(SecondaryView);
        m_splitter->addWidget(m_viewContainer[SecondaryView]);
        m_splitter->setSizes(QList<int>() << newWidth << newWidth);
        m_viewContainer[SecondaryView]->view()->reload();
        m_viewContainer[SecondaryView]->setActive(false);
        m_viewContainer[SecondaryView]->show();
    } else if (m_activeViewContainer == m_viewContainer[PrimaryView]) {
        // remove secondary view
        m_viewContainer[SecondaryView]->close();
        m_viewContainer[SecondaryView]->deleteLater();
        m_viewContainer[SecondaryView] = 0;
    } else {
        // The secondary view is active, hence from a users point of view
        // the content of the secondary view should be moved to the primary view.
        // From an implementation point of view it is more efficient to close
        // the primary view and exchange the internal pointers afterwards.
        m_viewContainer[PrimaryView]->close();
        m_viewContainer[PrimaryView]->deleteLater();
        m_viewContainer[PrimaryView] = m_viewContainer[SecondaryView];
        m_viewContainer[SecondaryView] = 0;
    }

    setActiveViewContainer(m_viewContainer[PrimaryView]);
    updateViewActions();
    emit activeViewChanged();
}

void DolphinMainWindow::reloadView()
{
    clearStatusBar();
    m_activeViewContainer->view()->reload();
}

void DolphinMainWindow::stopLoading()
{
}

void DolphinMainWindow::togglePreview()
{
    clearStatusBar();

    const KToggleAction* showPreviewAction =
        static_cast<KToggleAction*>(actionCollection()->action("show_preview"));
    const bool show = showPreviewAction->isChecked();
    m_activeViewContainer->view()->setShowPreview(show);
}

void DolphinMainWindow::toggleShowHiddenFiles()
{
    clearStatusBar();

    const KToggleAction* showHiddenFilesAction =
        static_cast<KToggleAction*>(actionCollection()->action("show_hidden_files"));
    const bool show = showHiddenFilesAction->isChecked();
    m_activeViewContainer->view()->setShowHiddenFiles(show);
}

void DolphinMainWindow::toggleFilterBarVisibility()
{
    const KToggleAction* showFilterBarAction =
        static_cast<KToggleAction*>(actionCollection()->action("show_filter_bar"));
    const bool show = showFilterBarAction->isChecked();
    m_activeViewContainer->showFilterBar(show);
}

void DolphinMainWindow::zoomIn()
{
    m_activeViewContainer->view()->zoomIn();
    updateViewActions();
}

void DolphinMainWindow::zoomOut()
{
    m_activeViewContainer->view()->zoomOut();
    updateViewActions();
}

void DolphinMainWindow::toggleEditLocation()
{
    clearStatusBar();

    KToggleAction* action = static_cast<KToggleAction*>(actionCollection()->action("editable_location"));

    bool editOrBrowse = action->isChecked();
    KUrlNavigator* urlNavigator = m_activeViewContainer->urlNavigator();
    urlNavigator->setUrlEditable(editOrBrowse);
}

void DolphinMainWindow::editLocation()
{
    KUrlNavigator* navigator = m_activeViewContainer->urlNavigator();
    navigator->setUrlEditable(true);
    navigator->setFocus();
}

void DolphinMainWindow::adjustViewProperties()
{
    clearStatusBar();
    ViewPropertiesDialog dlg(m_activeViewContainer->view());
    dlg.exec();
}

void DolphinMainWindow::goBack()
{
    clearStatusBar();
    m_activeViewContainer->urlNavigator()->goBack();
}

void DolphinMainWindow::goForward()
{
    clearStatusBar();
    m_activeViewContainer->urlNavigator()->goForward();
}

void DolphinMainWindow::goUp()
{
    clearStatusBar();
    m_activeViewContainer->urlNavigator()->goUp();
}

void DolphinMainWindow::goHome()
{
    clearStatusBar();
    m_activeViewContainer->urlNavigator()->goHome();
}

void DolphinMainWindow::findFile()
{
    KRun::run("kfind", m_activeViewContainer->url(), this);
}

void DolphinMainWindow::compareFiles()
{
    // The method is only invoked if exactly 2 files have
    // been selected. The selected files may be:
    // - both in the primary view
    // - both in the secondary view
    // - one in the primary view and the other in the secondary
    //   view
    Q_ASSERT(m_viewContainer[PrimaryView] != 0);

    KUrl urlA;
    KUrl urlB;
    KUrl::List urls = m_viewContainer[PrimaryView]->view()->selectedUrls();

    switch (urls.count()) {
    case 0: {
        Q_ASSERT(m_viewContainer[SecondaryView] != 0);
        urls = m_viewContainer[SecondaryView]->view()->selectedUrls();
        Q_ASSERT(urls.count() == 2);
        urlA = urls[0];
        urlB = urls[1];
        break;
    }

    case 1: {
        urlA = urls[0];
        Q_ASSERT(m_viewContainer[SecondaryView] != 0);
        urls = m_viewContainer[SecondaryView]->view()->selectedUrls();
        Q_ASSERT(urls.count() == 1);
        urlB = urls[0];
        break;
    }

    case 2: {
        urlA = urls[0];
        urlB = urls[1];
        break;
    }

    default: {
        // may not happen: compareFiles may only get invoked if 2
        // files are selected
        Q_ASSERT(false);
    }
    }

    QString command("kompare -c \"");
    command.append(urlA.pathOrUrl());
    command.append("\" \"");
    command.append(urlB.pathOrUrl());
    command.append('\"');
    KRun::runCommand(command, "Kompare", "kompare", this);

}

void DolphinMainWindow::toggleShowMenuBar()
{
    const bool visible = menuBar()->isVisible();
    menuBar()->setVisible(!visible);
}

void DolphinMainWindow::editSettings()
{
    DolphinSettingsDialog dialog(this);
    dialog.exec();
}

void DolphinMainWindow::init()
{
    // Check whether Dolphin runs the first time. If yes then
    // a proper default window size is given at the end of DolphinMainWindow::init().
    GeneralSettings* generalSettings = DolphinSettings::instance().generalSettings();
    const bool firstRun = generalSettings->firstRun();
    if (firstRun) {
        generalSettings->setViewPropsTimestamp(QDateTime::currentDateTime());
    }

    setAcceptDrops(true);

    m_splitter = new QSplitter(this);

    DolphinSettings& settings = DolphinSettings::instance();

    setupActions();

    const KUrl& homeUrl = settings.generalSettings()->homeUrl();
    setCaption(homeUrl.fileName());
    ViewProperties props(homeUrl);
    m_viewContainer[PrimaryView] = new DolphinViewContainer(this,
                                                            m_splitter,
                                                            homeUrl);

    m_activeViewContainer = m_viewContainer[PrimaryView];
    connectViewSignals(PrimaryView);
    m_viewContainer[PrimaryView]->view()->reload();
    m_viewContainer[PrimaryView]->show();

    setCentralWidget(m_splitter);
    setupDockWidgets();

    setupGUI(Keys | Save | Create | ToolBar);
    createGUI();

    stateChanged("new_file");
    setAutoSaveSettings();

    QClipboard* clipboard = QApplication::clipboard();
    connect(clipboard, SIGNAL(dataChanged()),
            this, SLOT(updatePasteAction()));
    updatePasteAction();
    updateGoActions();

    if (generalSettings->splitView()) {
        toggleSplitView();
    }
    updateViewActions();

    if (firstRun) {
        // assure a proper default size if Dolphin runs the first time
        resize(700, 500);
    }

    emit urlChanged(homeUrl);
}

void DolphinMainWindow::setActiveViewContainer(DolphinViewContainer* view)
{
    Q_ASSERT(view != 0);
    Q_ASSERT((view == m_viewContainer[PrimaryView]) || (view == m_viewContainer[SecondaryView]));
    if (m_activeViewContainer == view) {
        return;
    }

    m_activeViewContainer->setActive(false);
    m_activeViewContainer = view;
    m_activeViewContainer->setActive(true);

    updateHistory();
    updateEditActions();
    updateViewActions();
    updateGoActions();

    const KUrl& url = m_activeViewContainer->url();
    setCaption(url.fileName());

    emit activeViewChanged();
    emit urlChanged(url);
}

void DolphinMainWindow::setupActions()
{
    // setup 'File' menu
    m_newMenu = new DolphinNewMenu(this);
    KMenu* menu = m_newMenu->menu();
    menu->setTitle(i18nc("@title:menu", "Create New"));
    menu->setIcon(KIcon("document-new"));
    connect(menu, SIGNAL(aboutToShow()),
            this, SLOT(updateNewMenu()));

    KAction* newWindow = actionCollection()->addAction("new_window");
    newWindow->setIcon(KIcon("window-new"));
    newWindow->setText(i18nc("@action:inmenu File", "New &Window"));
    newWindow->setShortcut(Qt::CTRL | Qt::Key_N);
    connect(newWindow, SIGNAL(triggered()), this, SLOT(openNewMainWindow()));

    KAction* rename = DolphinView::createRenameAction(actionCollection());
    connect(rename, SIGNAL(triggered()), this, SLOT(rename()));

    KAction* moveToTrash = DolphinView::createMoveToTrashAction(actionCollection());
    connect(moveToTrash, SIGNAL(triggered()), this, SLOT(moveToTrash()));

    KAction* deleteAction = DolphinView::createDeleteAction(actionCollection());
    connect(deleteAction, SIGNAL(triggered()), this, SLOT(deleteItems()));

    KAction* properties = actionCollection()->addAction("properties");
    properties->setText(i18nc("@action:inmenu File", "Properties"));
    properties->setShortcut(Qt::ALT | Qt::Key_Return);
    connect(properties, SIGNAL(triggered()), this, SLOT(properties()));

    KStandardAction::quit(this, SLOT(quit()), actionCollection());

    // setup 'Edit' menu
    KStandardAction::undo(this,
                          SLOT(undo()),
                          actionCollection());

    //Need to remove shift+del from cut action, else the shortcut for deletejob
    //doesn't work
    KAction* cut = KStandardAction::cut(this, SLOT(cut()), actionCollection());
    KShortcut cutShortcut = cut->shortcut();
    cutShortcut.remove(Qt::SHIFT + Qt::Key_Delete, KShortcut::KeepEmpty);
    cut->setShortcut(cutShortcut);
    KStandardAction::copy(this, SLOT(copy()), actionCollection());
    KStandardAction::paste(this, SLOT(paste()), actionCollection());

    KAction* selectAll = actionCollection()->addAction("select_all");
    selectAll->setText(i18nc("@action:inmenu Edit", "Select All"));
    selectAll->setShortcut(Qt::CTRL + Qt::Key_A);
    connect(selectAll, SIGNAL(triggered()), this, SLOT(selectAll()));

    KAction* invertSelection = actionCollection()->addAction("invert_selection");
    invertSelection->setText(i18nc("@action:inmenu Edit", "Invert Selection"));
    invertSelection->setShortcut(Qt::CTRL | Qt::SHIFT | Qt::Key_A);
    connect(invertSelection, SIGNAL(triggered()), this, SLOT(invertSelection()));

    // setup 'View' menu
    KStandardAction::zoomIn(this,
                            SLOT(zoomIn()),
                            actionCollection());

    KStandardAction::zoomOut(this,
                             SLOT(zoomOut()),
                             actionCollection());


    KToggleAction* iconsView = DolphinView::iconsModeAction(actionCollection());
    KToggleAction* detailsView = DolphinView::detailsModeAction(actionCollection());
    KToggleAction* columnView = DolphinView::columnsModeAction(actionCollection());

    QActionGroup* viewModeGroup = new QActionGroup(this);
    viewModeGroup->addAction(iconsView);
    viewModeGroup->addAction(detailsView);
    viewModeGroup->addAction(columnView);
    connect(viewModeGroup, SIGNAL(triggered(QAction*)), this, SLOT(setViewMode(QAction*)));

    KToggleAction* sortByName = actionCollection()->add<KToggleAction>("sort_by_name");
    sortByName->setText(i18nc("@action:inmenu Sort By", "Name"));
    connect(sortByName, SIGNAL(triggered()), this, SLOT(sortByName()));

    KToggleAction* sortBySize = actionCollection()->add<KToggleAction>("sort_by_size");
    sortBySize->setText(i18nc("@action:inmenu Sort By", "Size"));
    connect(sortBySize, SIGNAL(triggered()), this, SLOT(sortBySize()));

    KToggleAction* sortByDate = actionCollection()->add<KToggleAction>("sort_by_date");
    sortByDate->setText(i18nc("@action:inmenu Sort By", "Date"));
    connect(sortByDate, SIGNAL(triggered()), this, SLOT(sortByDate()));

    KToggleAction* sortByPermissions = actionCollection()->add<KToggleAction>("sort_by_permissions");
    sortByPermissions->setText(i18nc("@action:inmenu Sort By", "Permissions"));
    connect(sortByPermissions, SIGNAL(triggered()), this, SLOT(sortByPermissions()));

    KToggleAction* sortByOwner = actionCollection()->add<KToggleAction>("sort_by_owner");
    sortByOwner->setText(i18nc("@action:inmenu Sort By", "Owner"));
    connect(sortByOwner, SIGNAL(triggered()), this, SLOT(sortByOwner()));

    KToggleAction* sortByGroup = actionCollection()->add<KToggleAction>("sort_by_group");
    sortByGroup->setText(i18nc("@action:inmenu Sort By", "Group"));
    connect(sortByGroup, SIGNAL(triggered()), this, SLOT(sortByGroup()));

    KToggleAction* sortByType = actionCollection()->add<KToggleAction>("sort_by_type");
    sortByType->setText(i18nc("@action:inmenu Sort By", "Type"));
    connect(sortByType, SIGNAL(triggered()), this, SLOT(sortByType()));

    // TODO: Hided "sort by rating" and "sort by tags" as without caching the performance
    // is too slow currently (Nepomuk will support caching in future releases).
    //
    // KToggleAction* sortByRating = actionCollection()->add<KToggleAction>("sort_by_rating");
    // sortByRating->setText(i18nc("@action:inmenu Sort By", "Rating"));
    //
    // KToggleAction* sortByTags = actionCollection()->add<KToggleAction>("sort_by_tags");
    // sortByTags->setText(i18nc("@action:inmenu Sort By", "Tags"));
    //
#ifdef HAVE_NEPOMUK
    // if (MetaDataWidget::metaDataAvailable()) {
    //     connect(sortByRating, SIGNAL(triggered()), this, SLOT(sortByRating()));
    //     connect(sortByTags, SIGNAL(triggered()), this, SLOT(sortByTags()));
    // }
    // else {
    //     sortByRating->setEnabled(false);
    //     sortByTags->setEnabled(false);
    // }
#else
    // sortByRating->setEnabled(false);
    // sortByTags->setEnabled(false);
#endif

    QActionGroup* sortGroup = new QActionGroup(this);
    sortGroup->addAction(sortByName);
    sortGroup->addAction(sortBySize);
    sortGroup->addAction(sortByDate);
    sortGroup->addAction(sortByPermissions);
    sortGroup->addAction(sortByOwner);
    sortGroup->addAction(sortByGroup);
    sortGroup->addAction(sortByType);

    // TODO: Hided "sort by rating" and "sort by tags" as without caching the performance
    // is too slow currently (Nepomuk will support caching in future releases).
    //sortGroup->addAction(sortByRating);
    //sortGroup->addAction(sortByTags);

    KToggleAction* sortDescending = actionCollection()->add<KToggleAction>("descending");
    sortDescending->setText(i18nc("@action:inmenu Sort", "Descending"));
    connect(sortDescending, SIGNAL(triggered()), this, SLOT(toggleSortOrder()));

    KToggleAction* showInGroups = actionCollection()->add<KToggleAction>("show_in_groups");
    showInGroups->setText(i18nc("@action:inmenu View", "Show in Groups"));
    connect(showInGroups, SIGNAL(triggered()), this, SLOT(toggleSortCategorization()));

    KToggleAction* showSizeInfo = actionCollection()->add<KToggleAction>("show_size_info");
    showSizeInfo->setText(i18nc("@action:inmenu Additional information", "Size"));
    connect(showSizeInfo, SIGNAL(triggered()), this, SLOT(toggleSizeInfo()));

    KToggleAction* showDateInfo = actionCollection()->add<KToggleAction>("show_date_info");
    showDateInfo->setText(i18nc("@action:inmenu Additional information", "Date"));
    connect(showDateInfo, SIGNAL(triggered()), this, SLOT(toggleDateInfo()));

    KToggleAction* showPermissionsInfo = actionCollection()->add<KToggleAction>("show_permissions_info");
    showPermissionsInfo->setText(i18nc("@action:inmenu Additional information", "Permissions"));
    connect(showPermissionsInfo, SIGNAL(triggered()), this, SLOT(togglePermissionsInfo()));

    KToggleAction* showOwnerInfo = actionCollection()->add<KToggleAction>("show_owner_info");
    showOwnerInfo->setText(i18nc("@action:inmenu Additional information", "Owner"));
    connect(showOwnerInfo, SIGNAL(triggered()), this, SLOT(toggleOwnerInfo()));

    KToggleAction* showGroupInfo = actionCollection()->add<KToggleAction>("show_group_info");
    showGroupInfo->setText(i18nc("@action:inmenu Additional information", "Group"));
    connect(showGroupInfo, SIGNAL(triggered()), this, SLOT(toggleGroupInfo()));

    KToggleAction* showMimeInfo = actionCollection()->add<KToggleAction>("show_mime_info");
    showMimeInfo->setText(i18nc("@action:inmenu Additional information", "Type"));
    connect(showMimeInfo, SIGNAL(triggered()), this, SLOT(toggleMimeInfo()));

    KToggleAction* showPreview = actionCollection()->add<KToggleAction>("show_preview");
    showPreview->setText(i18nc("@action:intoolbar", "Preview"));
    showPreview->setIcon(KIcon("fileview-preview"));
    connect(showPreview, SIGNAL(triggered()), this, SLOT(togglePreview()));

    KToggleAction* showHiddenFiles = actionCollection()->add<KToggleAction>("show_hidden_files");
    showHiddenFiles->setText(i18nc("@action:inmenu View", "Show Hidden Files"));
    showHiddenFiles->setShortcut(Qt::ALT | Qt::Key_Period);
    connect(showHiddenFiles, SIGNAL(triggered()), this, SLOT(toggleShowHiddenFiles()));

    KAction* split = actionCollection()->addAction("split_view");
    split->setShortcut(Qt::Key_F10);
    updateSplitAction();
    connect(split, SIGNAL(triggered()), this, SLOT(toggleSplitView()));

    KAction* reload = actionCollection()->addAction("reload");
    reload->setText(i18nc("@action:inmenu View", "Reload"));
    reload->setShortcut(Qt::Key_F5);
    reload->setIcon(KIcon("view-refresh"));
    connect(reload, SIGNAL(triggered()), this, SLOT(reloadView()));

    KAction* stop = actionCollection()->addAction("stop");
    stop->setText(i18nc("@action:inmenu View", "Stop"));
    stop->setIcon(KIcon("process-stop"));
    connect(stop, SIGNAL(triggered()), this, SLOT(stopLoading()));

    // TODO: the URL navigator must emit a signal if the editable state has been
    // changed, so that the corresponding showFullLocation action is updated. Also
    // the naming "Show full Location" is currently confusing...
    KToggleAction* showFullLocation = actionCollection()->add<KToggleAction>("editable_location");
    showFullLocation->setText(i18nc("@action:inmenu Navigation Bar", "Show Full Location"));
    showFullLocation->setShortcut(Qt::CTRL | Qt::Key_L);
    connect(showFullLocation, SIGNAL(triggered()), this, SLOT(toggleEditLocation()));

    KAction* editLocation = actionCollection()->addAction("edit_location");
    editLocation->setText(i18nc("@action:inmenu Navigation Bar", "Edit Location"));
    editLocation->setShortcut(Qt::Key_F6);
    connect(editLocation, SIGNAL(triggered()), this, SLOT(editLocation()));

    KAction* adjustViewProps = actionCollection()->addAction("view_properties");
    adjustViewProps->setText(i18nc("@action:inmenu View", "Adjust View Properties..."));
    connect(adjustViewProps, SIGNAL(triggered()), this, SLOT(adjustViewProperties()));

    // setup 'Go' menu
    KAction* backAction = KStandardAction::back(this, SLOT(goBack()), actionCollection());
    KShortcut backShortcut = backAction->shortcut();
    backShortcut.setAlternate(Qt::Key_Backspace);
    backAction->setShortcut(backShortcut);

    KStandardAction::forward(this, SLOT(goForward()), actionCollection());
    KStandardAction::up(this, SLOT(goUp()), actionCollection());
    KStandardAction::home(this, SLOT(goHome()), actionCollection());

    // setup 'Tools' menu
    QAction* findFile = actionCollection()->addAction("find_file");
    findFile->setText(i18nc("@action:inmenu Tools", "Find File..."));
    findFile->setShortcut(Qt::CTRL | Qt::Key_F);
    findFile->setIcon(KIcon("edit-find"));
    connect(findFile, SIGNAL(triggered()), this, SLOT(findFile()));

    KToggleAction* showFilterBar = actionCollection()->add<KToggleAction>("show_filter_bar");
    showFilterBar->setText(i18nc("@action:inmenu Tools", "Show Filter Bar"));
    showFilterBar->setShortcut(Qt::CTRL | Qt::Key_I);
    connect(showFilterBar, SIGNAL(triggered()), this, SLOT(toggleFilterBarVisibility()));

    KAction* compareFiles = actionCollection()->addAction("compare_files");
    compareFiles->setText(i18nc("@action:inmenu Tools", "Compare Files"));
    compareFiles->setIcon(KIcon("kompare"));
    compareFiles->setEnabled(false);
    connect(compareFiles, SIGNAL(triggered()), this, SLOT(compareFiles()));

    // setup 'Settings' menu
    m_showMenuBar = KStandardAction::showMenubar(this, SLOT(toggleShowMenuBar()), actionCollection());
    KStandardAction::preferences(this, SLOT(editSettings()), actionCollection());
}

void DolphinMainWindow::setupDockWidgets()
{
    // setup "Information"
    QDockWidget* infoDock = new QDockWidget(i18nc("@title:window", "Information"));
    infoDock->setObjectName("infoDock");
    infoDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    SidebarPage* infoWidget = new InfoSidebarPage(infoDock);
    infoDock->setWidget(infoWidget);

    infoDock->toggleViewAction()->setText(i18nc("@title:window", "Information"));
    infoDock->toggleViewAction()->setShortcut(Qt::Key_F11);
    actionCollection()->addAction("show_info_panel", infoDock->toggleViewAction());

    addDockWidget(Qt::RightDockWidgetArea, infoDock);
    connect(this, SIGNAL(urlChanged(KUrl)),
            infoWidget, SLOT(setUrl(KUrl)));
    connect(this, SIGNAL(selectionChanged(KFileItemList)),
            infoWidget, SLOT(setSelection(KFileItemList)));
    connect(this, SIGNAL(requestItemInfo(KFileItem)),
            infoWidget, SLOT(requestDelayedItemInfo(KFileItem)));

    // setup "Tree View"
    QDockWidget* treeViewDock = new QDockWidget(i18nc("@title:window", "Folders"));
    treeViewDock->setObjectName("treeViewDock");
    treeViewDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    TreeViewSidebarPage* treeWidget = new TreeViewSidebarPage(treeViewDock);
    treeViewDock->setWidget(treeWidget);

    treeViewDock->toggleViewAction()->setText(i18nc("@title:window", "Folders"));
    treeViewDock->toggleViewAction()->setShortcut(Qt::Key_F7);
    actionCollection()->addAction("show_folders_panel", treeViewDock->toggleViewAction());

    addDockWidget(Qt::LeftDockWidgetArea, treeViewDock);
    connect(this, SIGNAL(urlChanged(KUrl)),
            treeWidget, SLOT(setUrl(KUrl)));
    connect(treeWidget, SIGNAL(changeUrl(KUrl)),
            this, SLOT(changeUrl(KUrl)));
    connect(treeWidget, SIGNAL(changeSelection(KFileItemList)),
            this, SLOT(changeSelection(KFileItemList)));
    connect(treeWidget, SIGNAL(urlsDropped(KUrl::List, KUrl)),
            this, SLOT(dropUrls(KUrl::List, KUrl)));

    // setup "Terminal"
    QDockWidget* terminalDock = new QDockWidget(i18nc("@title:window", "Terminal"));
    terminalDock->setObjectName("terminalDock");
    terminalDock->setAllowedAreas(Qt::TopDockWidgetArea | Qt::BottomDockWidgetArea);
    SidebarPage* terminalWidget = new TerminalSidebarPage(terminalDock);
    terminalDock->setWidget(terminalWidget);

    terminalDock->toggleViewAction()->setText(i18nc("@title:window", "Terminal"));
    terminalDock->toggleViewAction()->setShortcut(Qt::Key_F4);
    actionCollection()->addAction("show_terminal_panel", terminalDock->toggleViewAction());

    addDockWidget(Qt::BottomDockWidgetArea, terminalDock);
    connect(this, SIGNAL(urlChanged(KUrl)),
            terminalWidget, SLOT(setUrl(KUrl)));

    const bool firstRun = DolphinSettings::instance().generalSettings()->firstRun();
    if (firstRun) {
        infoDock->hide();
        treeViewDock->hide();
        terminalDock->hide();
    }

    QDockWidget* placesDock = new QDockWidget(i18nc("@title:window", "Places"));
    placesDock->setObjectName("placesDock");
    placesDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    KFilePlacesView* placesView = new KFilePlacesView(placesDock);
    placesDock->setWidget(placesView);
    placesView->setModel(DolphinSettings::instance().placesModel());
    placesView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    placesDock->toggleViewAction()->setText(i18nc("@title:window", "Places"));
    placesDock->toggleViewAction()->setShortcut(Qt::Key_F9);
    actionCollection()->addAction("show_places_panel", placesDock->toggleViewAction());

    addDockWidget(Qt::LeftDockWidgetArea, placesDock);
    connect(placesView, SIGNAL(urlChanged(KUrl)),
            this, SLOT(changeUrl(KUrl)));
    connect(this, SIGNAL(urlChanged(KUrl)),
            placesView, SLOT(setUrl(KUrl)));
}

void DolphinMainWindow::updateHistory()
{
    const KUrlNavigator* urlNavigator = m_activeViewContainer->urlNavigator();
    const int index = urlNavigator->historyIndex();

    QAction* backAction = actionCollection()->action("go_back");
    if (backAction != 0) {
        backAction->setEnabled(index < urlNavigator->historySize() - 1);
    }

    QAction* forwardAction = actionCollection()->action("go_forward");
    if (forwardAction != 0) {
        forwardAction->setEnabled(index > 0);
    }
}

void DolphinMainWindow::updateEditActions()
{
    const KFileItemList list = m_activeViewContainer->view()->selectedItems();
    if (list.isEmpty()) {
        stateChanged("has_no_selection");
    } else {
        stateChanged("has_selection");

        QAction* renameAction = actionCollection()->action("rename");
        if (renameAction != 0) {
            renameAction->setEnabled(true);
        }

        bool enableMoveToTrash = true;

        KFileItemList::const_iterator it = list.begin();
        const KFileItemList::const_iterator end = list.end();
        while (it != end) {
            const KUrl& url = (*it).url();
            // only enable the 'Move to Trash' action for local files
            if (!url.isLocalFile()) {
                enableMoveToTrash = false;
            }
            ++it;
        }

        QAction* moveToTrashAction = actionCollection()->action("move_to_trash");
        moveToTrashAction->setEnabled(enableMoveToTrash);
    }
    updatePasteAction();
}

void DolphinMainWindow::updateViewActions()
{
    const DolphinView* view = m_activeViewContainer->view();
    QAction* zoomInAction = actionCollection()->action(KStandardAction::stdName(KStandardAction::ZoomIn));
    if (zoomInAction != 0) {
        zoomInAction->setEnabled(view->isZoomInPossible());
    }

    QAction* zoomOutAction = actionCollection()->action(KStandardAction::stdName(KStandardAction::ZoomOut));
    if (zoomOutAction != 0) {
        zoomOutAction->setEnabled(view->isZoomOutPossible());
    }

    QAction* action = actionCollection()->action(view->currentViewModeActionName());
    if (action != 0) {
        KToggleAction* toggleAction = static_cast<KToggleAction*>(action);
        toggleAction->setChecked(true);
    }

    slotSortingChanged(view->sorting());
    slotSortOrderChanged(view->sortOrder());
    slotCategorizedSortingChanged();
    slotAdditionalInfoChanged(view->additionalInfo());

    KToggleAction* showFilterBarAction =
        static_cast<KToggleAction*>(actionCollection()->action("show_filter_bar"));
    showFilterBarAction->setChecked(m_activeViewContainer->isFilterBarVisible());

    KToggleAction* showPreviewAction =
        static_cast<KToggleAction*>(actionCollection()->action("show_preview"));
    showPreviewAction->setChecked(view->showPreview());

    KToggleAction* showHiddenFilesAction =
        static_cast<KToggleAction*>(actionCollection()->action("show_hidden_files"));
    showHiddenFilesAction->setChecked(view->showHiddenFiles());

    updateSplitAction();

    KToggleAction* editableLocactionAction =
        static_cast<KToggleAction*>(actionCollection()->action("editable_location"));
    const KUrlNavigator* urlNavigator = m_activeViewContainer->urlNavigator();
    editableLocactionAction->setChecked(urlNavigator->isUrlEditable());
}

void DolphinMainWindow::updateGoActions()
{
    QAction* goUpAction = actionCollection()->action(KStandardAction::stdName(KStandardAction::Up));
    const KUrl& currentUrl = m_activeViewContainer->url();
    goUpAction->setEnabled(currentUrl.upUrl() != currentUrl);
}

void DolphinMainWindow::clearStatusBar()
{
    m_activeViewContainer->statusBar()->clear();
}

void DolphinMainWindow::connectViewSignals(int viewIndex)
{
    DolphinViewContainer* container = m_viewContainer[viewIndex];
    connect(container, SIGNAL(showFilterBarChanged(bool)),
            this, SLOT(updateFilterBarAction(bool)));

    DolphinView* view = container->view();
    connect(view, SIGNAL(modeChanged()),
            this, SLOT(slotViewModeChanged()));
    connect(view, SIGNAL(showPreviewChanged()),
            this, SLOT(slotShowPreviewChanged()));
    connect(view, SIGNAL(showHiddenFilesChanged()),
            this, SLOT(slotShowHiddenFilesChanged()));
    connect(view, SIGNAL(categorizedSortingChanged()),
            this, SLOT(slotCategorizedSortingChanged()));
    connect(view, SIGNAL(sortingChanged(DolphinView::Sorting)),
            this, SLOT(slotSortingChanged(DolphinView::Sorting)));
    connect(view, SIGNAL(sortOrderChanged(Qt::SortOrder)),
            this, SLOT(slotSortOrderChanged(Qt::SortOrder)));
    connect(view, SIGNAL(additionalInfoChanged(KFileItemDelegate::InformationList)),
            this, SLOT(slotAdditionalInfoChanged(KFileItemDelegate::InformationList)));
    connect(view, SIGNAL(selectionChanged(KFileItemList)),
            this, SLOT(slotSelectionChanged(KFileItemList)));
    connect(view, SIGNAL(requestItemInfo(KFileItem)),
            this, SLOT(slotRequestItemInfo(KFileItem)));
    connect(view, SIGNAL(activated()),
            this, SLOT(toggleActiveView()));
    connect(view, SIGNAL(doingOperation(KonqFileUndoManager::CommandType)),
            this, SLOT(slotDoingOperation(KonqFileUndoManager::CommandType)));

    const KUrlNavigator* navigator = container->urlNavigator();
    connect(navigator, SIGNAL(urlChanged(const KUrl&)),
            this, SLOT(changeUrl(const KUrl&)));
    connect(navigator, SIGNAL(historyChanged()),
            this, SLOT(slotHistoryChanged()));
}

void DolphinMainWindow::updateSplitAction()
{
    QAction* splitAction = actionCollection()->action("split_view");
    if (m_viewContainer[SecondaryView] != 0) {
        if (m_activeViewContainer == m_viewContainer[PrimaryView]) {
            splitAction->setText(i18nc("@action:intoolbar Close right view", "Close"));
            splitAction->setIcon(KIcon("fileview-close-right"));
        } else {
            splitAction->setText(i18nc("@action:intoolbar Close left view", "Close"));
            splitAction->setIcon(KIcon("fileview-close-left"));
        }
    } else {
        splitAction->setText(i18nc("@action:intoolbar Split view", "Split"));
        splitAction->setIcon(KIcon("fileview-split"));
    }
}

void DolphinMainWindow::toggleAdditionalInfo(const char* actionName,
                                             KFileItemDelegate::Information info)
{
    clearStatusBar();

    DolphinView* view = m_activeViewContainer->view();
    KFileItemDelegate::InformationList list = view->additionalInfo();

    const bool show = actionCollection()->action(actionName)->isChecked();

    const int index = list.indexOf(info);
    const bool containsInfo = (index >= 0);
    if (show && !containsInfo) {
        list.append(info);
        view->setAdditionalInfo(list);
    } else if (!show && containsInfo) {
        list.removeAt(index);
        view->setAdditionalInfo(list);
        Q_ASSERT(list.indexOf(info) < 0);
    }
}

DolphinMainWindow::UndoUiInterface::UndoUiInterface(DolphinMainWindow* mainWin) :
    KonqFileUndoManager::UiInterface(mainWin),
    m_mainWin(mainWin)
{
    Q_ASSERT(m_mainWin != 0);
}

DolphinMainWindow::UndoUiInterface::~UndoUiInterface()
{
}

void DolphinMainWindow::UndoUiInterface::jobError(KIO::Job* job)
{
    DolphinStatusBar* statusBar = m_mainWin->activeViewContainer()->statusBar();
    statusBar->setMessage(job->errorString(), DolphinStatusBar::Error);
}

#include "dolphinmainwindow.moc"
