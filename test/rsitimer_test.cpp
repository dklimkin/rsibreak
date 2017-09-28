/*
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

#include "rsitimer_test.h"

#include "rsitimer.h"

static constexpr int RELAX_ENDED_MAGIC_VALUE = -1;

RSITimerTest::RSITimerTest(void)
{
    m_intervals.resize(INTERVAL_COUNT);
    m_intervals[TINY_BREAK_INTERVAL] = 15 * 60;
    m_intervals[TINY_BREAK_DURATION] = 20;
    m_intervals[TINY_BREAK_THRESHOLD] = 60;
    m_intervals[BIG_BREAK_INTERVAL] = 60 * 60;
    m_intervals[BIG_BREAK_DURATION] = 60;
    m_intervals[BIG_BREAK_THRESHOLD] = 5 * 60;
    m_intervals[POSTPONE_BREAK_INTERVAL] = 3 * 60;
    m_intervals[PATIENCE_INTERVAL] = 30;
}

void RSITimerTest::triggerSimpleTinyBreak()
{
    RSIIdleTimeFake* idleTime = new RSIIdleTimeFake();
    RSITimer timer( idleTime, m_intervals, true, true );

    // Part one, no idleness till small break.
    QSignalSpy spy1Relax(&timer, SIGNAL(relax(int, bool)));
    QSignalSpy spy1UpdateIdleAvg(&timer, SIGNAL(updateIdleAvg(double)));

    idleTime->setIdleTime(0);
    for (int i = 0; i < m_intervals[TINY_BREAK_INTERVAL]; i++) {
        QCOMPARE(timer.m_state, RSITimer::TimerState::Monitoring);
        timer.timeout();
    }

    QCOMPARE(timer.m_state, RSITimer::TimerState::Suggesting);

    QCOMPARE(spy1Relax.count(), 1);
    QList<QVariant> spy1RelaxSignals = spy1Relax.takeFirst();
    QCOMPARE(spy1RelaxSignals.at(0).toInt(), m_intervals[TINY_BREAK_DURATION]);
    QCOMPARE(spy1RelaxSignals.at(1).toBool(), false);

    QCOMPARE(spy1UpdateIdleAvg.count(), m_intervals[TINY_BREAK_INTERVAL]);
    double lastAvg = 0;
    for (int i = 0; i < m_intervals[TINY_BREAK_INTERVAL]; i++) {
        QList<QVariant> spy1UpdateIdleAvgSignals = spy1UpdateIdleAvg.takeFirst();
        bool ok;
        double newAvg = spy1UpdateIdleAvgSignals.at(0).toDouble(&ok);
        QVERIFY2(ok && (newAvg >= lastAvg) && (newAvg <= 100.0),
                 QString("Unexpected newAvg value: %1, lastAvg: %2").arg(newAvg).arg(lastAvg).toLatin1());
    }

    // Part two, obeying and idle as suggested.
    QSignalSpy spy2Relax(&timer, SIGNAL(relax(int, bool)));
    QSignalSpy spy2UpdateIdleAvg(&timer, SIGNAL(updateIdleAvg(double)));
    QSignalSpy spy2Minimize(&timer, SIGNAL(minimize()));

    for (int i = 0; i < m_intervals[TINY_BREAK_DURATION]; i++) {
        QCOMPARE(timer.m_state, RSITimer::TimerState::Suggesting);
        idleTime->setIdleTime( (i + 1) * 1000 );
        timer.timeout();
    }
    QCOMPARE(timer.m_state, RSITimer::TimerState::Monitoring);
    QCOMPARE(spy2Minimize.count(), 1);
    QCOMPARE(spy2Relax.count(), m_intervals[TINY_BREAK_DURATION]);
    for (int i = 1; i < m_intervals[TINY_BREAK_DURATION]; i++) {
        QList<QVariant> spy2RelaxSignals = spy2Relax.takeFirst();
        QCOMPARE(spy2RelaxSignals.at(0).toInt(), m_intervals[TINY_BREAK_DURATION] - i);
    }
    QList<QVariant> spy2RelaxSignals = spy2Relax.takeFirst(); // The last one is special.
    QCOMPARE(spy2RelaxSignals.at(0).toInt(), RELAX_ENDED_MAGIC_VALUE);

    // For now, RSITimer owns idleTime, so not deleting it.
}

void RSITimerTest::triggerComplexTinyBreak()
{
    RSIIdleTimeFake* idleTime = new RSIIdleTimeFake();
    RSITimer timer( idleTime, m_intervals, true, true );

    int part1 = 10;         // Non-idle
    int part2 = 40;         // Idle
    int part3 = m_intervals[TINY_BREAK_INTERVAL] - part1 - part2;   // The rest non-idle.

    // Part 1, no idleness.
    QSignalSpy spy1Relax(&timer, SIGNAL(relax(int, bool)));
    QSignalSpy spy1UpdateIdleAvg(&timer, SIGNAL(updateIdleAvg(double)));
    idleTime->setIdleTime(0);
    for (int i = 0; i < part1; i++) {
        timer.timeout();
        QCOMPARE(timer.m_state, RSITimer::TimerState::Monitoring);
    }
    QCOMPARE(spy1Relax.count(), 0);
    QCOMPARE(spy1UpdateIdleAvg.count(), part1);

    // Part 2, idle for a while.
    QSignalSpy spy2Relax(&timer, SIGNAL(relax(int, bool)));
    QSignalSpy spy2UpdateIdleAvg(&timer, SIGNAL(updateIdleAvg(double)));
    for (int i = 0; i < part2; i++) {
        idleTime->setIdleTime( (i + 1) * 1000 );
        timer.timeout();
        QCOMPARE(timer.m_state, RSITimer::TimerState::Monitoring);
    }
    QCOMPARE(spy2Relax.count(), 0);
    QCOMPARE(spy2UpdateIdleAvg.count(), part2);

    // Part 3, non-idle till break.
    QSignalSpy spy3Relax(&timer, SIGNAL(relax(int, bool)));
    QSignalSpy spy3UpdateIdleAvg(&timer, SIGNAL(updateIdleAvg(double)));
    for (int i = 0; i < part3; i++) {
        QCOMPARE(timer.m_state, RSITimer::TimerState::Monitoring);
        idleTime->setIdleTime( 0 );
        timer.timeout();
    }
    QCOMPARE(timer.m_state, RSITimer::TimerState::Suggesting);
    QCOMPARE(spy3Relax.count(), 1);

    // For now, RSITimer owns idleTime, so not deleting it.
}

void RSITimerTest::testSuspended()
{
    RSIIdleTimeFake* idleTime = new RSIIdleTimeFake();
    RSITimer timer( idleTime, m_intervals, true, true );

    timer.slotStop();
    QCOMPARE(timer.m_state, RSITimer::TimerState::Suspended);

    QSignalSpy spy1Relax(&timer, SIGNAL(relax(int, bool)));
    QSignalSpy spy1UpdateIdleAvg(&timer, SIGNAL(updateIdleAvg(double)));

    // Idle for long enough.
    idleTime->setIdleTime(0);
    for (int i = 0; i < m_intervals[TINY_BREAK_INTERVAL]; i++) {
        timer.timeout();
        QCOMPARE(timer.m_state, RSITimer::TimerState::Suspended);
    }
    QCOMPARE(spy1Relax.count(), 0);
    QCOMPARE(spy1UpdateIdleAvg.count(), 0);

    timer.slotStart();
    QCOMPARE(timer.m_state, RSITimer::TimerState::Monitoring);

    // For now, RSITimer owns idleTime, so not deleting it.
}

void RSITimerTest::triggerSimpleBigBreak()
{
    RSIIdleTimeFake* idleTime = new RSIIdleTimeFake();
    RSITimer timer( idleTime, m_intervals, true, true );

    int tinyBreaks = m_intervals[BIG_BREAK_INTERVAL] / (m_intervals[TINY_BREAK_INTERVAL] + m_intervals[PATIENCE_INTERVAL] + m_intervals[TINY_BREAK_DURATION]);
    // We don't tick big pause timer during tiny breaks and patience, so it will actually happen later.
    int ticks = m_intervals[BIG_BREAK_INTERVAL] + tinyBreaks * (m_intervals[PATIENCE_INTERVAL] + m_intervals[TINY_BREAK_DURATION]);

    // Part one, no idleness till big break.
    QSignalSpy spy1Relax(&timer, SIGNAL(relax(int, bool)));
    QSignalSpy spy1UpdateIdleAvg(&timer, SIGNAL(updateIdleAvg(double)));

    idleTime->setIdleTime(0);
    for (int i = 0; i < ticks; i++) {
        timer.timeout();
    }

    QCOMPARE(timer.m_state, RSITimer::TimerState::Suggesting);
    // Number of relax updates during N tiny breaks, plus one for the actual big break.
    int relaxCountExp = tinyBreaks * (2 + m_intervals[PATIENCE_INTERVAL]) + 1;
    QCOMPARE(spy1Relax.count(), relaxCountExp);
    QVERIFY2(spy1UpdateIdleAvg.count() >= m_intervals[BIG_BREAK_INTERVAL], "Failed to update the indicator regularly.");

    // Part two, making the big break.
    QSignalSpy spy2Relax(&timer, SIGNAL(relax(int, bool)));
    for (int i = 0; i < m_intervals[BIG_BREAK_DURATION]; i++) {
        idleTime->setIdleTime( (i + 1) * 1000 );
        timer.timeout();
    }
    QCOMPARE(timer.m_state, RSITimer::TimerState::Monitoring);
    QCOMPARE(spy2Relax.count(), m_intervals[BIG_BREAK_DURATION]);
}

#include "rsitimer_test.moc"
