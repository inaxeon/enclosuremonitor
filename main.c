/*
 *   File:   main.c
 *   Author: Matt
 *
 *   Created on 11 May 2018, 12:13
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

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <util/delay.h>
#include <avr/io.h>
#include <avr/pgmspace.h>
#include <avr/interrupt.h>

#include "config.h"
#include "i2c.h"
#include "spi.h"
#include "ds18x20.h"
#include "ds2482.h"
#include "usart.h"
#include "sc16is7xx.h"
#include "onewire.h"
#include "gsm.h"
#include "sms.h"
#include "adc.h"
#include "util.h"
#include "timer.h"
#include "timeout.h"
#include "smshistory.h"

char _g_dotBuf[MAX_DESC];
char _g_sms_buf[MAX_SMS + 1];

typedef struct
{
    sys_config_t *config;
    uint8_t sensor_ids[MAX_SENSORS][DS18X20_ROMCODE_SIZE];
    uint8_t num_sensors;
    int16_t temp_result[MAX_SENSORS];
    uint16_t temp_state;
    int8_t measure_timer;
    int8_t readtemp_timer;
    uint16_t mains_counter;
    uint16_t mains_result;
    uint8_t last_portb;
} sys_runstate_t;

sys_config_t _g_cfg;
sys_runstate_t _g_rs;

FILE uart_str = FDEV_SETUP_STREAM(print_char, NULL, _FDEV_SETUP_RW);

static void io_init(void);
static char *dots_for(const char *str);
static void print_temp(uint8_t temp, int16_t result, const char *desc, uint8_t nl);
static void start_measure(void *param);
static void read_sensors(void *param);
static void check_ctrld(void *param);
static void check_mains(void *param);

ISR(PCINT0_vect)
{
    if (!(_g_rs.last_portb & _BV(PB6)) && (PINB & _BV(PB6)))
        _g_rs.mains_counter++;
    _g_rs.last_portb = PINB;
}

ISR(TIMER1_OVF_vect)
{
}

int main(void)
{
    uint8_t i;
    sys_runstate_t *rs = &_g_rs;
    sys_config_t *config = &_g_cfg;
    rs->config = config;

    io_init();
    spi_init();
    g_irq_enable();

    usart1_open(USART_CONT_RX, (((F_CPU / UART1_BAUD) / 16) - 1)); // Internal USART: GSM
    sc16is7xx1_open(SC16IS7XX_BAUD, 8, false, 1, false);           // SPI UART: Console

    stdout = &uart_str;

    rs->mains_counter = 0;
    rs->temp_state = 0;

    adc_init();
    i2c_init(400);
    ow_init();

    printf("\r\n");

    load_configuration(config);
    configuration_bootprompt(config);

    sms_init(config);

    for (i = 0; i < MAX_SENSORS; i++)
        rs->temp_result[i] = 0;

    if (ds18x20_search_sensors(&rs->num_sensors, rs->sensor_ids))
        printf("\r\nFound %u of %u maximum sensors\r\n", rs->num_sensors, MAX_SENSORS);
    else
        printf("\r\nHardware error searching for sensors\r\n");

    if (rs->num_sensors == 0)
        printf("No sensors found.\r\n");

    if (rs->num_sensors != config->expected_sensors)
    {
        if (sms_can_send_message())
        {
            sprintf(_g_sms_buf, "Temperature sensor failure. There should be %u sensors, but %u were found", config->expected_sensors, rs->num_sensors);
            sms_try_send(MESSAGE_STARTUP, 0, _g_sms_buf);
        }
    }

    CLRWDT();

    printf("Press Ctrl+D at any time to reset\r\n");

    timeout_init();
    sms_history_init();
    
    rs->measure_timer = timeout_create(100, true, false, &start_measure, (void *)rs);
    rs->readtemp_timer = timeout_create(760, false, false, &read_sensors, (void *)rs);
    timeout_create(50, true, true, &check_ctrld, (void *)rs);
    timeout_create(1000, true, true, &check_mains, (void *)rs);

    // Idle loop
    for (;;)
    {
        timeout_check();
        gsm_process();
        sms_process();
    }
}

static void io_init(void)
{
    SC16IS7XX_EN_DDR |= _BV(SC16IS7XX_EN);
    SC16IS7XX_INT_DDR &= ~_BV(SC16IS7XX_INT);

    ACINT_DDR &= ~_BV(ACINT);
    RTCINT_DDR &= ~_BV(RTCINT);

    DDRE &= ~_BV(PE6); // INT6
    EIMSK |= _BV(INT6);

    // Configure INT6 as rising edge interrupt
    EICRB |= _BV(ISC61);
    EICRB |= _BV(ISC60);

    DDRB &= ~_BV(PB6);
    PCICR |= _BV(PCIE0);
    PCMSK0 |= _BV(PCINT6);
    
    // Disable USB, because the bootloader has probably left it on
    USBCON &= ~_BV(USBE);
}

static void start_measure(void *param)
{
    sys_runstate_t *rs = (sys_runstate_t *)param;
    uint8_t i;

    for (i = 0; i < rs->num_sensors; i++)
        ds18x20_start_meas(rs->sensor_ids[i]);

    timeout_start(rs->readtemp_timer);
}

static void read_sensors(void *param)
{
    sys_runstate_t *rs = (sys_runstate_t *)param;
    uint16_t battery_voltage;
    uint8_t i;

    for (i = 0; i < rs->num_sensors; i++)
    {
        int16_t reading_temp;

        if (ds18x20_read_decicelsius(rs->sensor_ids[i], &reading_temp))
        {
            rs->temp_result[i] = reading_temp;
            rs->temp_state |= (1 << i);
        }
        else
        {
            rs->temp_state &= ~(1 << i);
        }
    }

    for (i = 0; i < rs->num_sensors; i++)
    {
        if ((rs->temp_state & (1 << i)) == (1 << i))
        {
            print_temp(i, rs->temp_result[i], rs->config->temp_sensors[i].name, (i == 0));

            if (rs->temp_result[i] > rs->config->temp_sensors[i].high_threshold)
            {
                if (sms_can_send_message())
                {
                    fixedpoint_sign(rs->temp_result[i], current);
                    fixedpoint_sign(rs->config->temp_sensors[i].high_threshold, threshold);

                    sprintf(_g_sms_buf, "Sensor '%s' is above threshold: current: %s%u.%u threshold: %s%u.%u",
                        rs->config->temp_sensors[i].name,
                        fixedpoint_arg(rs->temp_result[i], current),
                        fixedpoint_arg(rs->config->temp_sensors[i].high_threshold, threshold)
                    );
                    sms_try_send(MESSAGE_TEMP_RANGE_HIGH, i, _g_sms_buf);
                }
                else
                {
                    printf("Not sending high threshold SMS. Buffer not available\r\n");
                }
            }

            if (rs->temp_result[i] < rs->config->temp_sensors[i].low_threshold)
            {
                if (sms_can_send_message())
                {
                    fixedpoint_sign(rs->temp_result[i], current);
                    fixedpoint_sign(rs->config->temp_sensors[i].low_threshold, threshold);

                    sprintf(_g_sms_buf, "Sensor '%s' is below threshold: current: %s%u.%u threshold: %s%u.%u",
                        rs->config->temp_sensors[i].name,
                        fixedpoint_arg(rs->temp_result[i], current),
                        fixedpoint_arg(rs->config->temp_sensors[i].low_threshold, threshold)
                    );
                    sms_try_send(MESSAGE_TEMP_RANGE_LOW, i, _g_sms_buf);
                }
                else
                {
                    printf("Not sending low threshold SMS. Buffer not available\r\n");
                }
            }
        }
        else
        {
            printf("Error reading from sensor %u\r\n", i);
            
            if (sms_can_send_message())
            {
                sprintf(_g_sms_buf, "Lost connectivity to temperature sensor '%s'",
                    rs->config->temp_sensors[i].name
                );
                sms_try_send(MESSAGE_TEMP_STATE, i, _g_sms_buf);
            }
        }
    }

    if (rs->num_sensors == 0)
        printf("No sensors\r\n");

    battery_voltage = adc_read_battery();

    if (battery_voltage < BATTERY_VOLTAGE_LOW_THRESHOLD)
    {
        if (sms_can_send_message())
        {
            strcpy(_g_sms_buf, "Low battery alert");
            sms_try_send(MESSAGE_LOW_BATTERY, 0, _g_sms_buf);
        }
    }

    printf("Mains frequency ...........: %u\r\n", rs->mains_result);
    printf("Battery voltage ...........: %u.%02u\r\n", fixedpoint_arg_u_2dp(battery_voltage));

    timeout_start(rs->measure_timer);
}

static void check_ctrld(void *param)
{
    console_clear_oerr();
    
    if (console_data_ready())
    {
        char c = console_get();
        if (c == 4)
        {
            printf("\r\nCtrl+D received. Resetting...\r\n");
            while (console_busy());
            reset();
        }
    }
}

static void check_mains(void *param)
{
    sys_runstate_t *rs = (sys_runstate_t *)param;
    uint16_t temp_mains_result;

    g_irq_disable();
    temp_mains_result = rs->mains_counter;
    rs->mains_counter = 0;
    g_irq_enable();

    if (!rs->mains_result)
    {
        if ((get_tick_count() / TIMEOUT_TICK_PER_SECOND) > MAINS_HOLDOFF_SECONDS && sms_can_send_message())
        {
            sprintf(_g_sms_buf, "Mains power has failed");
            sms_try_send(MESSAGE_MAINS_STATE_OFF, 0, _g_sms_buf);
        }
    }
    if (temp_mains_result && !rs->mains_result)
    {
        if ((get_tick_count() / TIMEOUT_TICK_PER_SECOND) > MAINS_HOLDOFF_SECONDS && sms_can_send_message())
        {
            sprintf(_g_sms_buf, "Mains power restored");
            sms_try_send(MESSAGE_MAINS_STATE_ON, 0, _g_sms_buf);
        }
    }

    rs->mains_result = temp_mains_result;
}

static void print_temp(uint8_t temp, int16_t dec, const char *desc, uint8_t nl)
{
    fixedpoint_sign(dec, dec);

    printf("%sTemp %c (C) [%s] %s..: %s%u.%u\r\n",
        nl ? "\r\n" : "",
        '1' + temp,
        desc, dots_for(desc), fixedpoint_arg(dec, dec));
}

static char *dots_for(const char *str)
{
    uint8_t len = (MAX_DESC - 1) - strlen(str);
    uint8_t di = 0;
    for (di = 0; di < len; di++)
        _g_dotBuf[di] = '.';
    _g_dotBuf[di] = 0;
    return _g_dotBuf;
}

void status_response(char *sendbuffer)
{
    char buf[20];
    uint8_t i;
    sys_runstate_t *rs = &_g_rs;

    sendbuffer[0] = 0;
    for (i = 0; i < rs->num_sensors; i++)
    {
        char tempdesc[8];
        const char *desc;
        fixedpoint_sign(rs->temp_result[i], temp_result);

        sprintf(tempdesc, "Temp%u", i + 1);

        desc = *(rs->config->temp_sensors[i].name) ? rs->config->temp_sensors[i].name : tempdesc;

        if ((rs->temp_state & (1 << i)) == (1 << i))
        {
            sprintf(buf, "%s: %s%u.%u\n", desc,
                fixedpoint_arg(rs->temp_result[i], temp_result));
        }
        else
        {
            sprintf(buf, "%s: Unknown\n", desc);
        }

        if ((strlen(sendbuffer) + strlen(buf)) >= (MAX_SMS))
            return;

        strcat(sendbuffer, buf);
    }

    sprintf(buf, "Power: %s", rs->mains_result > 0 ? "On" : "Off");

    if ((strlen(sendbuffer) + strlen(buf)) >= (MAX_SMS))
        return;

    strcat(sendbuffer, buf);
}