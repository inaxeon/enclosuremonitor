/*
 *   File:   usart.c
 *   Author: Matt
 *
 *   Created on 13 November 2017, 10:32
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
#include <stdbool.h>

#include <avr/io.h>

#include "usart.h"

#ifdef _USART1_

void usart1_open(uint8_t flags, uint16_t brg)
{
    UCSR1C |= _BV(UMSEL11);

    if (flags & USART_SYNC)
        UCSR1C |= _BV(UMSEL10);
    else
        UCSR1C &= ~_BV(UMSEL10);

    if (flags & USART_9BIT)
    {
        UCSR1C |= _BV(UCSZ10);
        UCSR1C |= _BV(UCSZ11);
        UCSR1B |= _BV(UCSZ12);
    }
    else
    {
        UCSR1C |= _BV(UCSZ10);
        UCSR1C |= _BV(UCSZ11);
        UCSR1B &= ~_BV(UCSZ12);
    }

    if (flags & USART_SYNC)
    {
        if (flags & USART_SYNC_MASTER)
            USART1_DDR |= _BV(USART1_XCK);
        else
            USART1_DDR &= ~_BV(USART1_XCK);
    }

    if (flags & USART_CONT_RX)
        UCSR1B |= _BV(RXEN1);
    else
        UCSR1B &= ~_BV(RXEN1);

    if (flags & USART_IOR)
        UCSR1B |= _BV(RXCIE1);
    else
        UCSR1B &= ~_BV(RXCIE1);

    if (flags & USART_IOT)
        UCSR1B |= _BV(TXCIE1);
    else
        UCSR1B &= ~_BV(TXCIE1);

    UBRR1L = (brg & 0xFF);
    UBRR1H = (brg >> 8);

    UCSR1B |= _BV(TXEN1);

    USART1_DDR |= _BV(USART1_TX);
    USART1_DDR &= ~_BV(USART1_RX);
}

bool usart1_busy(void)
{
    if ((UCSR1A & _BV(UDRE1)) == 0)
        return true;
    return false;
}

void usart1_put(char c)
{
    UDR1 = c;
}

bool usart1_data_ready(void)
{
    if ((UCSR1A &_BV(RXC1)) != 0)
        return true;

    return false;
}

char usart1_get(void)
{
    char data;
    data = UDR1;
    return data;
}

void usart1_clear_oerr(void)
{
    UCSR1A &= ~_BV(DOR1);
}


#endif /* _USART1_ */
