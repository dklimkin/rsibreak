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

#include <algorithm>
#include <QDebug>

#include "rsitimercounter.h"


int RSITimerCounter::tick(const int idleTime) {
    qDebug() << "Tick! For delay " << m_delayTicks
             << "idleTime: " << idleTime
             << "m_counter: " << m_counter
             << "m_breakLength: " << m_breakLength
             << "m_resetThreshold: " << m_resetThreshold;

    m_counter++;

    // Not idle for too long, time for a break.
    if (m_counter >= m_delayTicks) {
        reset();
        return m_breakLength;
    }

    // Idle for enough to consider the break has happened.
    if (idleTime >= m_resetThreshold) {
        reset();
        return 0;
    }

    // In-flight, not time for a break yet.
    return 0;
}

bool RSITimerCounter::isReset() {
    return m_counter == 0;
}

void RSITimerCounter::reset() {
    m_counter = 0;
}

void RSITimerCounter::postpone(int ticks) {
    m_counter = std::max(0, m_counter - ticks);
}

int RSITimerCounter::counterLeft() const {
    return m_delayTicks - m_counter;
}

int RSITimerCounter::getDelayTicks() const {
    return m_delayTicks;
}
