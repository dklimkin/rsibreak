/* This file is part of the KDE project
   Copyright (C) 2006 Bram Schoenmakers <bramschoenmakers@kde.nl>
   Copyright (C) 2006 Tom Albers <tomalbers@kde.nl>

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

#ifndef RSISTATITEM_H
#define RSISTATITEM_H

#include <q3valuelist.h>
#include <qvariant.h>
//Added by qt3to4:
#include <QLabel>

#include "rsiglobals.h"

class QLabel;

/**
 * This class represents one statistic.
 * It consists of a value, a description and a list
 * of items which have this statistic as a dependency.
 *
 * @author Bram Schoenmakers <bramschoenmakers@kde.nl>
 */
class RSIStatItem
{
  public:
    /**
     * Constructor. Pass a @p description along to give the
     * statistic a useful description. It will be visible in the
     * statistics widget.
     * @param description A i18n()'d text representing this statistic's meaning.
     * @param init The initial value of this statistic. Default value is an
     * integer zero.
     */
    explicit RSIStatItem( const QString &description = QString(), const QVariant &init = QVariant(0) );

    /** Default destructor. */
    virtual ~RSIStatItem();

    /** Retrieve the item's description in QLabel format. */
    QLabel *getDescription() const { return m_description; }

    /** Retrieve the item's value in QVariant format. */
    QVariant getValue()      const { return m_value; }

    /**
     * Sets the value of this item.
     * @param v The new value of this statistic item.
     *
     * @see QVariant documentation for supported types of values.
     */
    void setValue( QVariant v ) { m_value = v; }

    /**
     * When other statistics depend on this statistic item, it should
     * be added to this list. When this statistic is updated, it will
     * iterate through the list of derived statistics and update them.
     */
    void addDerivedItem( RSIStat stat );

    /**
     * Returns the list of derived statistics.
     */
    Q3ValueList<RSIStat> getDerivedItems() const { return m_derived; }

    /**
     * Resets current value to initial value, passed along with the
     * constructor.
     */
    virtual void reset();

    //virtual void setActivity() {};
    //virtual void setIdle() {};

  protected:
    QVariant m_value;
    QVariant m_init;

  private:
    QLabel *m_description;

    /** Contains a list of RSIStats which depend on *this* item. */
    Q3ValueList< RSIStat > m_derived;
};



/**
 * This is a more extended statistic item.
 * It uses a part of the bit array defined in RSIGlobals, which keeps track per
 * second when the user was active or idle (max. 24 hours).
 * The amount of time recorded by this item is specified with the size
 * attribute in the constructor.
 *
 * @author Bram Schoenmakers <bramschoenmakers@kde.nl>
 * @see RSIGlobals
 */
class RSIStatBitArrayItem : public RSIStatItem
{
  public:
    /**
     * Constructor of a bit array item.
     * @param description A i18n()'d text representing this statistic's meaning.
     * @param init The initial value of this statistic. Default value is an
     * integer zero.
     * @param size The amount of time this item keeps track of in seconds. Default
     * it keeps track of 24 hours of usage. This value should be never higher than
     * 86400 seconds.
     */
    explicit RSIStatBitArrayItem( const QString &description = QString(), const QVariant &init = QVariant(0), int size = 86400 );

    /**
     * Destructor.
     */
    ~RSIStatBitArrayItem();

    /**
     * Resets the value of this item and the complete usage array
     * in RSIGlobals.
     */
    void reset();

    /**
     * Updates the value of this item when activity has occurred.
     */
    void setActivity();

    /**
     * Updates the value of this item when the user was idle.
     */
    void setIdle();

  private:
    int m_size;
    int m_counter;
    int m_begin;
    int m_end;
};

#endif
