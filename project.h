/*
 *   File:   project.h
 *   Author: Matt
 *
 *   Created on 11 May 2018, 11:51
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


#ifndef __PROJECT_H__
#define __PROJECT_H__

#define MAX_DESC        12
#define MAX_SENSORS     10
#define MAX_RECIPIENTS  4
#define MAX_RECIPIENT   16

#define _I2C_XFER_
#define _I2C_XFER_MANY_
#define _I2C_XFER_X16_
#define _I2C_XFER_BYTE_
#define _I2C_DS2482_SPECIAL_

#define _USART1_
#define _OW_DS2482_

#define F_CPU      16000000

#define CONFIG_MAGIC        0x454D

#define CLRWDT() asm("wdr")

#define g_irq_disable cli
#define g_irq_enable sei

#define USART1_DDR         DDRD
#define USART1_TX          PD3
#define USART1_RX          PD2
#define USART1_XCK         PD5

#define SPI_DDR            DDRB
#define SPI_PORT           PORTB
#define SPI_MISO           PB3
#define SPI_MOSI           PB2
#define SPI_SCK            PB1

#define SC16IS7XX_INT_PORT PORTE
#define SC16IS7XX_INT_DDR  DDRE
#define SC16IS7XX_INT      PE6

#define SC16IS7XX_EN_PORT  PORTD
#define SC16IS7XX_EN_DDR   DDRD
#define SC16IS7XX_EN       PD7

#define ACINT_PORT         PORTB
#define ACINT_DDR          DDRB
#define ACINT              PB6

#define RTCINT_PORT        PORTB
#define RTCINT_DDR         DDRB
#define RTCINT             PB7

#define GSM_PORT           PORTB
#define GSM_DDR            DDRB
#define GSM_PWR            PB4
#define GSM_RESET          PB5

#define _SPI_CONSOLE_

#define SC16IS7XX_BAUD       9600
#define UART1_BAUD           4800

#define TIMEOUT_TICK_PER_SECOND  (100)
#define TIMEOUT_MS_PER_TICK      (1000 / TIMEOUT_TICK_PER_SECOND)

#define MAINS_HOLDOFF_SECONDS          5
#define BATTERY_VOLTAGE_LOW_THRESHOLD  350

#ifdef _SPI_CONSOLE_

#define console_busy         sc16is7xx1_busy
#define console_put          sc16is7xx1_put
#define console_data_ready   sc16is7xx1_data_ready
#define console_get          sc16is7xx1_get
#define console_clear_oerr   sc16is7xx1_clear_oerr

#else

#define console_busy         usart1_busy
#define console_put          usart1_put
#define console_data_ready   usart1_data_ready
#define console_get          usart1_get
#define console_clear_oerr   usart1_clear_oerr

#endif /* _SPI_CONSOLE_ */

#define gsm_usart_put        usart1_put
#define gsm_usart_data_ready usart1_data_ready
#define gsm_usart_get        usart1_get

#endif /* __PROJECT_H__ */