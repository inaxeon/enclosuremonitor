/*
 *   File:   sc16is7xx.h
 *   Author: Matt
 *
 *   Created on 15 May 2018, 10:39
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

#ifndef __SC16IS7XX_H__
#define __SC16IS7XX_H__

#define UARTA   0
#define UARTB   1
#define UARTC   2
#define UARTD   3

/* Values to OR straight on to the LCR. No need to mess around I thinks. */
#define PARITY_NONE     (0 << 3)
#define PARITY_ODD      (1 << 3)
#define PARITY_EVEN     (3 << 3)
#define PARITY_MARK     (5 << 3)
#define PARITY_SPACE    (7 << 3)

#define sc16is7xx1_open(baud, data_bits, parity, stop_bits, rxint) sc16is7xx_open(0, baud, data_bits, parity, stop_bits, rxint)
#define sc16is7xx1_busy() sc16is7xx_busy(0)
#define sc16is7xx1_put(c) sc16is7xx_put(0, c)
#define sc16is7xx1_data_ready() sc16is7xx_data_ready(0)
#define sc16is7xx1_get() sc16is7xx_get(0)
#define sc16is7xx1_clear_oerr()

void sc16is7xx_open(uint8_t index, uint32_t baud, uint8_t data_bits, bool parity, uint8_t stop_bits, bool rxint);
bool sc16is7xx_busy(uint8_t unit);
void sc16is7xx_put(uint8_t unit, char c);
bool sc16is7xx_data_ready(uint8_t unit);
char sc16is7xx_get(uint8_t unit);
void sc16is7xx_clear_oerr(uint8_t unit);



#endif /* __SC16IS7XX_H__ */
