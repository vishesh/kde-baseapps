/***************************************************************************
 *   Copyright (C) 2009 by Peter Penz <peter.penz@gmx.at>                  *
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

#ifndef DOLPHINNEWMENUOBSERVER_H
#define DOLPHINNEWMENUOBSERVER_H

#include <QObject>

#include "libdolphin_export.h"

class KNewMenu;
class KUrl;

/**
 * @brief Allows to observe new file items that have been created
 *        by a DolphinNewMenu instance.
 *
 * As soon as a DolphinNewMenu instance created a new item,
 * the observer will emit the signal itemCreated().
 */
class LIBDOLPHINPRIVATE_EXPORT DolphinNewMenuObserver : public QObject
{
    Q_OBJECT

public:
    static DolphinNewMenuObserver& instance();
    void attach(const KNewMenu* menu);
    void detach(const KNewMenu* menu);

signals:
    void itemCreated(const KUrl& url);

private:
    DolphinNewMenuObserver();
    virtual ~DolphinNewMenuObserver();

    friend class DolphinNewMenuObserverSingleton;
};

#endif
