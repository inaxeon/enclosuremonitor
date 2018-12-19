/*
 *   File:   sc16is7xx.c
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

#include "project.h"

#include <stdint.h>
#include <stdbool.h>
#include <avr/io.h>

#include <stdio.h>
#include <avr/pgmspace.h>
#include "util.h"

#include "sc16is7xx.h"
#include "spi.h"

/* Register offsets */
#define RBR             0x00    /* receive buffer       */
#define THR             0x00    /* transmit holding     */
#define IER             0x01    /* interrupt enable     */
#define IIR             0x02    /* interrupt identity   */
#define FCR             0x02    /* FIFO control         */
#define LCR             0x03    /* line control         */
#define MCR             0x04    /* Modem control        */
#define LSR             0x05    /* line status          */
#define MSR             0x06    /* Modem status         */
#define DLL             0x00    /* divisor latch (ls) (DLAB=1) */
#define DLM             0x01    /* divisor latch (ms) (DLAB=1) */

/* Interrupt Enable Register */
#define IER_ERDAI       0x01    /* rx data recv'd       */
#define IER_ETHREI      0x02    /* tx reg. empty        */
#define IER_ELSI        0x04    /* rx line status       */
#define IER_EMSI        0x08    /* MODEM status         */

/* Interrupt Identification Register */
#define IIR_NOINT       0x01    /* no interrupt pending */
#define IIR_IMASK       0x06    /* interrupt identity:  */
#define IIR_LSI         0x06    /*  - rx line status    */
#define IIR_RDAI        0x04    /*  - rx data recv'd    */
#define IIR_THREI       0x02    /*  - tx reg. empty     */
#define IIR_MSI         0x00    /*  - MODEM status      */

/* FIFO Control Register */
#define FCR_ENABLE      0x01    /* enable FIFO          */
#define FCR_CLRX        0x02    /* clear Rx FIFO        */
#define FCR_CLTX        0x04    /* clear Tx FIFO        */
#define FCR_DMA         0x10    /* enter DMA mode       */
#define FCR_TRG1        0x00    /* Rx FIFO trig lev 1   */
#define FCR_TRG4        0x40    /* Rx FIFO trig lev 4   */
#define FCR_TRG8        0x80    /* Rx FIFO trig lev 8   */
#define FCR_TRG14       0xc0    /* Rx FIFO trig lev 14  */

/* Line Control Register */
#define LCR_DLAB        0x80    /* Divisor Latch Access */

/* Modem Control Register */
#define MCR_DTR         0x01    /* Data Terminal Ready  */
#define MCR_RTS         0x02    /* Request to Send      */
#define MCR_OUT2        0x08    /* OUT2: interrupt mask */

/* Line Status Register */
#define LSR_DR          0x01    /* Data ready           */
#define LSR_OE          0x02    /* Overrun              */
#define LSR_PE          0x04    /* Parity error         */
#define LSR_FE          0x08    /* Framing error        */
#define LSR_BI          0x10    /* Break                */
#define LSR_THRE        0x20    /* Xmit hold reg empty  */
#define LSR_TEMT        0x40    /* Xmitter empty        */
#define LSR_ERR         0x80    /* Error                */

#define UART_CLOCK_HZ   7372800
//#define UART_CLOCK_HZ   14745600

#define SPI_READ        0x80
#define SPI_WRITE       0x00

uint8_t sc16is7xx_read_reg(uint8_t index, uint8_t reg)
{
    uint8_t ctrl = SPI_READ | (reg << 3) | (index << 1);
    uint8_t ret;
    SC16IS7XX_EN_PORT &= ~_BV(SC16IS7XX_EN);
    spi_xfer(ctrl);
    ret = spi_xfer(0xFF);
    SC16IS7XX_EN_PORT |= _BV(SC16IS7XX_EN);
    return ret;
}

void sc16is7xx_write_reg(uint8_t unit, uint8_t reg, uint8_t data)
{
    uint8_t ctrl = SPI_WRITE | (reg << 3) | (unit << 1);
    SC16IS7XX_EN_PORT &= ~_BV(SC16IS7XX_EN);
    spi_xfer(ctrl);
    spi_xfer(data);
    SC16IS7XX_EN_PORT |= _BV(SC16IS7XX_EN);
}

void sc16is7xx_open(uint8_t unit, uint32_t baud, uint8_t data_bits, bool parity, uint8_t stop_bits, bool rxint)
{
    if ((unit < UARTA) || (unit > UARTD))
        return;

    uint8_t lcr;
    uint16_t divisor;

    lcr = (data_bits - 5) | ((stop_bits - 1) << 2) | parity;

    if (rxint)
        sc16is7xx_write_reg(unit, IER, IER_ERDAI);
    else
        sc16is7xx_write_reg(unit, IER, 0);

    /* Line control and baud-rate generator. */
    sc16is7xx_write_reg(unit, LCR, lcr | LCR_DLAB);
    divisor = UART_CLOCK_HZ / (baud * 16);
    sc16is7xx_write_reg(unit, DLL, (uint8_t)divisor);
    sc16is7xx_write_reg(unit, DLM, (uint8_t)(divisor >> 8));

    sc16is7xx_write_reg(unit, LCR, lcr);

    /* No flow ctrl: DTR and RTS are both wedged high to keep remote happy. */
    sc16is7xx_write_reg(unit, MCR, MCR_DTR | MCR_RTS | MCR_OUT2);

    /* Enable and clear the FIFOs. Set a large trigger threshold. */
    sc16is7xx_write_reg(unit, FCR, FCR_ENABLE | FCR_CLRX | FCR_CLTX | FCR_TRG14);
}

void sc16is7xx_put(uint8_t unit, char c)
{
    while ((sc16is7xx_read_reg(unit, LSR) & LSR_THRE) == 0);
    sc16is7xx_write_reg(unit, THR, c);
}

bool sc16is7xx_busy(uint8_t unit)
{
    return ((sc16is7xx_read_reg(unit, LSR) & LSR_TEMT) == 0);
}

bool sc16is7xx_data_ready(uint8_t unit)
{
    if (!(sc16is7xx_read_reg(unit, LSR) & LSR_DR))
        return false;
    return true;
}

char sc16is7xx_get(uint8_t unit)
{
    return sc16is7xx_read_reg(unit, RBR);
}
