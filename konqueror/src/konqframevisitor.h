/*
   This file is part of the KDE project
   Copyright (C) 2007 David Faure <faure@kde.org>

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public
   License as published by the Free Software Foundation; either
   version 2 of the License, or (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; see the file COPYING.  If not, write to
   the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
   Boston, MA 02110-1301, USA.
*/

#ifndef KONQ_FRAMEVISITOR_H
#define KONQ_FRAMEVISITOR_H

#include <QList>
#include "konqprivate_export.h"

class KonqFrameBase;
class KonqView;
class KonqFrame;
class KonqFrameContainer;
class KonqFrameTabs;
class KonqMainWindow;

class KonqFrameVisitor
{
public:
    enum VisitorBehavior { VisitAllTabs = 1, VisitCurrentTabOnly = 2 };
    KonqFrameVisitor(VisitorBehavior behavior = VisitAllTabs) : m_behavior(behavior) {}
    virtual ~KonqFrameVisitor() {}
    virtual bool visit(KonqFrame*) { return true; }
    virtual bool visit(KonqFrameContainer*) { return true; }
    virtual bool visit(KonqFrameTabs*) { return true; }
    virtual bool visit(KonqMainWindow*) { return true; }

    virtual bool endVisit(KonqFrameContainer*) { return true; }
    virtual bool endVisit(KonqFrameTabs*) { return true; }
    virtual bool endVisit(KonqMainWindow*) { return true; }

    bool visitAllTabs() const { return m_behavior & VisitAllTabs; }
private:
    VisitorBehavior m_behavior;
};

/**
 * Collects all views, recursively.
 */
class KONQ_TESTS_EXPORT KonqViewCollector : public KonqFrameVisitor
{
public:
    static QList<KonqView *> collect(KonqFrameBase* topLevel);
    virtual bool visit(KonqFrame* frame);
    virtual bool visit(KonqFrameContainer*) { return true; }
    virtual bool visit(KonqFrameTabs*) { return true; }
    virtual bool visit(KonqMainWindow*) { return true; }
private:
    QList<KonqView *> m_views;
};

/**
 * Collects all views that can currently be linked; this excludes invisible tabs (#116714).
 */
class KonqLinkableViewsCollector : public KonqFrameVisitor
{
public:
    static QList<KonqView *> collect(KonqFrameBase* topLevel);
    virtual bool visit(KonqFrame* frame);
    virtual bool visit(KonqFrameContainer*) { return true; }
    virtual bool visit(KonqFrameTabs*) { return true; }
    virtual bool visit(KonqMainWindow*) { return true; }
private:
    KonqLinkableViewsCollector() : KonqFrameVisitor(VisitCurrentTabOnly) {}
    QList<KonqView *> m_views;
};

/**
 * Returns the list of views that have modified data in them,
 * for the warning-before-closing-a-tab.
 */
class KonqModifiedViewsCollector : public KonqFrameVisitor
{
public:
    static QList<KonqView *> collect(KonqFrameBase* topLevel);
    virtual bool visit(KonqFrame* frame);
    virtual bool visit(KonqFrameContainer*) { return true; }
    virtual bool visit(KonqFrameTabs*) { return true; }
    virtual bool visit(KonqMainWindow*) { return true; }
private:
    QList<KonqView *> m_views;
};

#endif /* KONQ_FRAMEVISITOR_H */

