/*
   Copyright (C) 2005-2006,2008-2010 Tom Albers <toma@kde.org>
   Copyright (C) 2005-2006 Bram Schoenmakers <bramschoenmakers@kde.nl>
   Copyright (C) 2010 Juan Luis Baptiste <juan.baptiste@gmail.com>

   The parts for idle detection is based on
   kdepim's karm idletimedetector.cpp/.h

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

#include "rsitimer.h"

#include <QDebug>
#include <QThread>
#include <QTimer>

#include <kconfig.h>
#include <kconfiggroup.h>
#include <kidletime.h>
#include <ksharedconfig.h>

#include "rsistats.h"

RSITimer::RSITimer(QObject *parent) : QThread( parent )
        , m_idleTimeInstance( new RSIIdleTimeImpl() )
        , m_intervals( RSIGlobals::instance()->intervals() )
        , m_state ( TimerState::Monitoring )
        , m_bigBreakCounter( nullptr )
        , m_tinyBreakCounter( nullptr )
        , m_pauseCounter( nullptr )
        , m_popupCounter( nullptr )
{
    updateConfig(true);
}

RSITimer::RSITimer( RSIIdleTime* _idleTime, const QVector<int> _intervals, const bool _usePopup, const bool _useIdleTimers ) : QThread( 0 )
        , m_idleTimeInstance( _idleTime )
        , m_usePopup( _usePopup )
        , m_useIdleTimers( _useIdleTimers )
        , m_intervals( _intervals )
        , m_state( TimerState::Monitoring )
        , m_bigBreakCounter( nullptr )
        , m_tinyBreakCounter( nullptr )
        , m_pauseCounter(nullptr)
        , m_popupCounter(nullptr)
{
    createTimers();
}


RSITimer::~RSITimer()
{
    for (RSITimerCounter* counter : {m_bigBreakCounter, m_tinyBreakCounter, m_popupCounter, m_pauseCounter}) {
        if (counter != nullptr) {
            delete counter;
        }
    }
    delete m_idleTimeInstance;
}

void RSITimer::run()
{
    QTimer timer(nullptr);
    connect(&timer, &QTimer::timeout, this, &RSITimer::timeout);
    timer.setTimerType( Qt::TimerType::CoarseTimer );
    timer.start( 1000 );
    exec(); // start event loop to make timers work.
}

void RSITimer::hibernationDetector(const int totalIdle)
{
    // poor mans hibernation detector....
    static QDateTime last = QDateTime::currentDateTime();
    QDateTime current = QDateTime::currentDateTime();
    if ( last.secsTo( current ) > 60 ) {
        qDebug() << "Not been checking idleTime for more than 60 seconds, "
                 << "assuming the computer hibernated, resetting timers"
                 << "Last: " << last
                 << "Current: " << current
                 << "Idle, s: " << totalIdle;
        resetAfterBreak();
    }
    last = current;
}

int RSITimer::idleTime()
{
    int totalIdle = m_idleTimeInstance->getIdleTime() / 1000;
    hibernationDetector(totalIdle);

//    qDebug() << "Idle: " << totalIdle;

    // TODO Find a modern-desktop way to check if the screensaver is inhibited
    // and disable the timer because we assume you're doing for example a presentation and
    // don't want rsibreak to annoy you

    return totalIdle;
}

void RSITimer::doBreakNow(const int breakTime, const bool nextBreakIsBig)
{
    m_state = TimerState::Resting;
    stopPauseCounters();
    m_pauseCounter = new RSITimerCounter(breakTime, breakTime, INT_MAX);
    RSIGlobals::instance()->NotifyBreak(true, nextBreakIsBig);
    emit updateWidget(breakTime);
    emit breakNow();
}

void RSITimer::stopPauseCounters() {
    for (RSITimerCounter** counter : {&m_pauseCounter, &m_popupCounter}) {
        if (*counter != nullptr) {
            delete *counter;
            *counter = nullptr;
        }
    }
}

void RSITimer::resetAfterBreak()
{
    m_state = TimerState::Monitoring;
    stopPauseCounters();
    defaultUpdateToolTip();
    emit updateIdleAvg( 0.0 );
    emit relax(-1, false);
    emit minimize();
}

// -------------------------- SLOTS ------------------------//

void RSITimer::slotStart()
{
    emit updateIdleAvg( 0.0 );
    m_state = TimerState::Monitoring;
}

void RSITimer::slotStop()
{
    m_state = TimerState::Suspended;
    emit updateIdleAvg( 0.0 );
    emit updateToolTip(0, 0);
}

void RSITimer::slotSuspended( bool suspend )
{
    suspend ? slotStop() : slotStart();
}

void RSITimer::slotRestart()
{
    for (RSITimerCounter *counter : {m_tinyBreakCounter, m_bigBreakCounter}) {
        counter->reset();
    }
    resetAfterBreak();
}

void RSITimer::skipBreak()
{
    if (m_bigBreakCounter->isReset()) {
        RSIGlobals::instance()->stats()->increaseStat( BIG_BREAKS_SKIPPED );
        emit bigBreakSkipped();
    }
    if (m_tinyBreakCounter->isReset()) {
        RSIGlobals::instance()->stats()->increaseStat( TINY_BREAKS_SKIPPED );
        emit tinyBreakSkipped();
    }
    resetAfterBreak();
    emit minimize();
    slotStart();
}

void RSITimer::postponeBreak()
{
    for (RSITimerCounter *counter : {m_tinyBreakCounter, m_bigBreakCounter}) {
        counter->postpone(m_intervals[POSTPONE_BREAK_INTERVAL]);
    }
    stopPauseCounters();

    if (m_bigBreakCounter->isReset()) {
       RSIGlobals::instance()->stats()->increaseStat( BIG_BREAKS_POSTPONED );
    }
    if (m_tinyBreakCounter->isReset()) {
       RSIGlobals::instance()->stats()->increaseStat( TINY_BREAKS_POSTPONED );
    }
    defaultUpdateToolTip();
    emit relax(-1, false);
    emit minimize();
}

void RSITimer::updateConfig(bool doRestart)
{
    KConfigGroup popupConfig = KSharedConfig::openConfig()->group( "Popup Settings" );
    m_usePopup = popupConfig.readEntry( "UsePopup", true );

    bool oldUseIdleTimers = m_useIdleTimers;
    KConfigGroup generalConfig = KSharedConfig::openConfig()->group( "General Settings" );
    m_useIdleTimers = !(generalConfig.readEntry( "UseNoIdleTimer", false ));
    doRestart = doRestart || (oldUseIdleTimers != m_useIdleTimers);

    const QVector<int> oldIntervals = m_intervals;
    m_intervals = RSIGlobals::instance()->intervals();
    doRestart = doRestart || (m_intervals != oldIntervals);

    if (doRestart) {
        qDebug() << "Timeout parameters have changed, counters were reset.";
        createTimers();
    }
}

// ----------------------------- EVENTS -----------------------//

void RSITimer::timeout() {
    // Don't change the tray icon when suspended, or evaluate a possible break.
    if (m_state == TimerState::Suspended) {
        return;
    }

    const int idleSeconds = idleTime(); // idleSeconds == 0 means activity

    RSIGlobals::instance()->stats()->increaseStat(TOTAL_TIME);
    RSIGlobals::instance()->stats()->setStat(CURRENT_IDLE_TIME, idleSeconds);
    if (idleSeconds == 0) {
        RSIGlobals::instance()->stats()->increaseStat(ACTIVITY);
    } else {
        RSIGlobals::instance()->stats()->setStat(MAX_IDLENESS, idleSeconds, true);
    }

    switch (m_state) {
        case TimerState::Monitoring: {
            // This is a weird thing to track as now when user was away, they will get back to zero counters,
            // not to an arbitrary time elapsed since last "idleness-skip-break".
            bool bigWasReset = m_bigBreakCounter->isReset();
            bool tinyWasReset = m_tinyBreakCounter->isReset();

            int breakTime = std::max(m_bigBreakCounter->tick(idleSeconds), m_tinyBreakCounter->tick(idleSeconds));
            if (breakTime > 0) {
                suggestBreak(breakTime);
            } else {
                // Not a time for break yet, but if one of the counters got reset, that means we were idle enough to skip.
                if (!bigWasReset && m_bigBreakCounter->isReset()) {
                    RSIGlobals::instance()->stats()->increaseStat(BIG_BREAKS);
                    RSIGlobals::instance()->stats()->increaseStat(IDLENESS_CAUSED_SKIP_BIG);
                }
                if (!tinyWasReset && m_tinyBreakCounter->isReset()) {
                    RSIGlobals::instance()->stats()->increaseStat(TINY_BREAKS);
                    RSIGlobals::instance()->stats()->increaseStat(IDLENESS_CAUSED_SKIP_TINY);
                }
            }
            const double value =
                    100.0 - ((m_tinyBreakCounter->counterLeft() / (double) m_intervals[TINY_BREAK_INTERVAL]) * 100.0);
            emit updateIdleAvg(value);
            break;
        }
        case TimerState::Suggesting: {
            // Using popupCounter to count down our patience here.
            int breakTime = m_popupCounter->tick(idleSeconds);
            if (breakTime > 0) {
                // User kept working throw the suggestion timeout. Well, their loss.
                emit relax(-1, false);
                breakTime = m_pauseCounter->counterLeft();
                doBreakNow(breakTime, false);
                break;
            }

            int inverseTick = (idleSeconds == 0) ? 1 : 0; // inverting as we account idle seconds here.
            breakTime = m_pauseCounter->tick(inverseTick);
            if (breakTime > 0) {
                // User has waited out the pause, back to monitoring.
                resetAfterBreak();
                break;
            }
            emit relax(m_pauseCounter->counterLeft(), false);
            emit updateWidget(m_pauseCounter->counterLeft());
            break;
        }
        case TimerState::Resting: {
            int inverseTick = (idleSeconds == 0) ? 1 : 0; // inverting as we account idle seconds here.
            int breakTime = m_pauseCounter->tick(inverseTick);
            if (breakTime > 0) {
                resetAfterBreak();
            } else {
                emit updateWidget(m_pauseCounter->counterLeft());
            }
            break;
        }
        default:
            qDebug() << "Reached unexpected state";
    }
    defaultUpdateToolTip();
}

void RSITimer::suggestBreak(const int breakTime)
{
    if (m_bigBreakCounter->isReset()) {
        qDebug() << "Big break triggered";
        RSIGlobals::instance()->stats()->increaseStat(BIG_BREAKS);
        RSIGlobals::instance()->stats()->setStat( LAST_BIG_BREAK, QVariant( QDateTime::currentDateTime() ) );
    } else {
        qDebug() << "Tiny break triggered";
        RSIGlobals::instance()->stats()->increaseStat(TINY_BREAKS);
        RSIGlobals::instance()->stats()->setStat( LAST_TINY_BREAK, QVariant( QDateTime::currentDateTime() ) );
    }

    bool nextOneIsBig = m_bigBreakCounter->counterLeft() <= m_tinyBreakCounter->getDelayTicks();
    if (!m_usePopup) {
        doBreakNow(breakTime, nextOneIsBig);
        return;
    }

    m_state = TimerState::Suggesting;
    stopPauseCounters();

    // When pause is longer than patience, we need to reset patience timer so that we don't flip to break now in
    // mid-pause. Patience / 2 is a good alternative to it by extending patience if user was idle long enough.
    m_popupCounter = new RSITimerCounter(m_intervals[PATIENCE_INTERVAL], breakTime, m_intervals[PATIENCE_INTERVAL] / 2);
    // Threshold of one means the timer is reset on every non-zero tick.
    m_pauseCounter = new RSITimerCounter(breakTime, breakTime, 1);

    emit relax(breakTime, nextOneIsBig);
}

void RSITimer::defaultUpdateToolTip()
{
    emit updateToolTip(m_tinyBreakCounter->counterLeft(), m_bigBreakCounter->counterLeft());
}

void RSITimer::createTimers() {
    stopPauseCounters();
    if (m_bigBreakCounter != nullptr) {
        delete m_bigBreakCounter;
    }
    if (m_tinyBreakCounter != nullptr) {
        delete m_tinyBreakCounter;
    }

    int bigThreshold = m_useIdleTimers ? m_intervals[BIG_BREAK_THRESHOLD] : INT_MAX;
    int tinyThreshold = m_useIdleTimers ? m_intervals[TINY_BREAK_THRESHOLD] : INT_MAX;

    m_bigBreakCounter = new RSITimerCounter(
            m_intervals[BIG_BREAK_INTERVAL], m_intervals[BIG_BREAK_DURATION], bigThreshold);
    m_tinyBreakCounter = new RSITimerCounter(
            m_intervals[TINY_BREAK_INTERVAL], m_intervals[TINY_BREAK_DURATION], tinyThreshold);
//            30, 60, 180);
}
