/*
 *   File:   spi.c
 *   Author: Matt
 *
 *   Created on 15 May 2018, 10:00
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

#include <avr/io.h>
#include <avr/interrupt.h>

#include "spi.h"

void spi_init(void)
{
    SPI_DDR &= ~((1 << SPI_MISO));
    SPI_DDR |= ((1 << SPI_MOSI) | (1 << SPI_SCK));
    SPI_PORT |= (1 << SPI_MISO);

    SPCR = ((1 << SPE) |            // SPI Enable
        //(0 << SPIE) |               // SPI Interupt Enable
        (0 << DORD) |               // Data Order (0:MSB first / 1:LSB first)
        (1 << MSTR) |               // Master/Slave select
        (1 << SPR1) | (1 << SPR0) | // SPI Clock Rate
        (0 << CPOL) |               // Clock Polarity (0:SCK low / 1:SCK hi when idle)
        (0 << CPHA));               // Clock Phase (0:leading / 1:trailing edge sampling)

    SPSR = (0 << SPI2X);            // Double SPI Speed Bit
}

uint8_t spi_xfer(uint8_t data)
{
    SPDR = data;
    while ((SPSR & (1 << SPIF)) == 0);
    return SPDR;
}
