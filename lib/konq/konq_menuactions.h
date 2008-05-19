/* This file is part of the KDE project
   Copyright (C) 1998-2007 David Faure <faure@kde.org>

   This library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Library General Public
   License as published by the Free Software Foundation; either
   version 2 of the License, or (at your option) any later version.

   This library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Library General Public License for more details.

   You should have received a copy of the GNU Library General Public License
   along with this library; see the file COPYING.LIB.  If not, write to
   the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
   Boston, MA 02110-1301, USA.
*/

#ifndef KONQ_MENUACTIONS_H
#define KONQ_MENUACTIONS_H

#include <kfileitem.h>
#include <libkonq_export.h>

class KonqPopupMenuInformation;
class QMenu;
class KonqMenuActionsPrivate;

/**
 * This class handles the user-defined actions for a url in a popupmenu.
 * User-defined actions include:
 * - builtin services like mount/unmount for old-style device desktop files
 * - user-defined actions for a .desktop file, defined in the file itself (see the desktop entry standard)
 * - servicemenus actions, defined in .desktop files and selected based on the mimetype of the url
 *
 * In addition it can also add "open with" actions.
 */
class LIBKONQ_EXPORT KonqMenuActions
{
public:
    /**
     * Creates a KonqMenuActions instance.
     * Note that this instance must stay alive for at least as long as the popupmenu;
     * it has the slots for the actions created by addActionsTo.
     */
    KonqMenuActions();

    /**
     * Destructor
     */
    ~KonqMenuActions();

    /**
     * Sets all the data for the next instance of the popupmenu.
     * @see KonqPopupMenuInformation
     */
    void setPopupMenuInfo(const KonqPopupMenuInformation& info);

    /**
     * Generate the user-defined actions and submenus, and adds them to the @p menu.
     * User-defined actions include:
     * - builtin services like mount/unmount for old-style device desktop files
     * - user-defined actions for a .desktop file, defined in the file itself (see the desktop entry standard)
     * - servicemenus actions, defined in .desktop files and selected based on the mimetype of the url
     *
     * When KonqPopupMenuInformation::readOnly() is true, actions that modify the files are not shown.
     * This is controlled by Require=Write in the servicemenu desktop files.
     *
     * All actions are created as children of the menu.
     * @return the number of actions added
     */
    int addActionsTo(QMenu* menu); // TODO rename to addUserDefinedActionsTo ?

    /**
     * Generate the "Open With <Application>" actions, and adds them to the @p menu.
     * All actions are created as children of the menu.
     * @param menu the QMenu where to add actions
     * @param traderConstraint this constraint allows to exclude the current application
     * from the "open with" list. Example: "DesktopEntryName != 'kfmclient'".
     */
    void addOpenWithActionsTo(QMenu* menu, const QString& traderConstraint);

private:
    KonqMenuActionsPrivate* const d;
};

#endif /* KONQ_MENUACTIONS_H */
