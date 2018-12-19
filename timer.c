/*
 *   File:   timer.c
 *   Author: Matt
 *
 *   Created on 19 May 2018, 18:02
 *
 *   This is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, either version 2 of the License, or
 *   (at your option) any later version.
 *   This software is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *   You should have received a copy of the GNU General Public License
 *   along with this software.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "project.h"

#include <stdint.h>
#include <avr/io.h>

#include "timer.h"

void timer1_init(void)
{
    TCCR1A = 0x00;
    // CLK(i/o) prescaler 256
    TCCR1B |= (1 << CS12);
    TCCR1B &= ~(1 << CS11);
    TCCR1B &= ~(1 << CS10);
    //TCCR1B |= (1 << CS10);

    TIMSK1 |= (1 << TOIE1);

    TCNT1H = 0xFF;
    TCNT1L = 0xFF;

    // Note to self: AVR Timers do not seem to have a 'go' bit
    // They're always going...
}
