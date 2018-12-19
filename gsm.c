/*
 *   File:   gsm.c
 *   Author: Matt
 *
 *   Created on 31 May 2018, 11:11
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
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <avr/pgmspace.h>
#include <util/delay.h>

#include "gsm.h"
#include "timeout.h"
#include "usart.h"
#include "util.h"

typedef enum
{
    GSM_STATE_INIT,
    GSM_STATE_READY, // 1
    GSM_STATE_AWAIT_CMGF, // 2
    GSM_STATE_AWAIT_ATE0, // 3
    GSM_STATE_AWAIT_SEND_SMS_INPUT, // 4
    GSM_STATE_AWAIT_SEND_SMS_RESPONSE, // 5
    GSM_STATE_AWAIT_READ_SMS_META,
    GSM_STATE_AWAIT_READ_SMS_TEXT,
    GSM_STATE_AWAIT_READ_ALL_SMS_META, // 8
    GSM_STATE_AWAIT_READ_ALL_SMS_TEXT,
    GSM_STATE_AWAIT_RESPONSE,
} gsm_state_t;

#define MAX_RX_BUFFER                         128
#define MAX_SMS_BUFFER                        (MAX_SMS * 2)

#define INIT_START                            0x01
#define INIT_CPIN                             0x02
#define INIT_SMS                              0x04
#define INIT_PB                               0x08

static uint8_t _g_gsm_state;
static uint8_t _g_init_flags;
static uint8_t _g_rx_index;
static int8_t _g_abort_timer;
static int16_t _g_last_index;

static char _g_receive_buffer[MAX_RX_BUFFER + 1];
static char _g_message_buffer[MAX_SMS_BUFFER + 1];

union
{
    gsm_cb_t cb;
    gsm_readsms_cb_t readsms_cb;
} _g_current_callback;

void (*_g_ready_callback)(void);

static void gsm_update_state(uint8_t newstate);
static void gsm_fill_line_buffer(const char c);
static void gsm_fill_message_buffer(const char c);
static void gsm_reset_buffer(void);
static void gsm_reset_message_buffer(void);
static void gsm_process_line(uint8_t state, const char *line);
static void gsm_input_message(void);
static void gsm_process_sms(uint8_t state, char *meta, char *message);
static void gsm_finish_operation(bool success);

void gsm_init(void (*ready_callback)(void))
{
    _g_gsm_state = GSM_STATE_INIT;
    _g_init_flags = 0;
    _g_last_index = -1;
    _g_abort_timer = -1;
    _g_ready_callback = ready_callback;

    memset(&_g_current_callback, 0, sizeof(gsm_cb_t));
    gsm_reset_buffer();
    gsm_reset_message_buffer();

    GSM_PORT &= ~_BV(GSM_RESET);
    GSM_PORT &= ~_BV(GSM_PWR);
    GSM_DDR |= _BV(GSM_PWR);
    GSM_DDR |= _BV(GSM_RESET);

    _delay_ms(10);
    // Take out of reset
    GSM_PORT |= _BV(GSM_RESET);

    _delay_ms(10);

    // Power on
    GSM_PORT |= _BV(GSM_PWR);
    _delay_ms(100);
    GSM_PORT &= ~_BV(GSM_PWR);
}

void gsm_process(void)
{
    while (gsm_usart_data_ready())
    {
        char c = gsm_usart_get();
        //printf("%c %d ", c, _g_gsm_state);
        if (_g_gsm_state == GSM_STATE_AWAIT_READ_SMS_TEXT || _g_gsm_state == GSM_STATE_AWAIT_READ_ALL_SMS_TEXT)
            gsm_fill_message_buffer(c);
        else
            gsm_fill_line_buffer(c);
    }
}

static void gsm_fill_line_buffer(const char c)
{
    if (c == '\r')
        return;

    else if (c != '\n')
    {
        if (_g_rx_index < MAX_RX_BUFFER)
        {
            _g_receive_buffer[_g_rx_index] = c;
            _g_rx_index++;
            _g_receive_buffer[_g_rx_index] = 0;

            if (_g_gsm_state == GSM_STATE_AWAIT_SEND_SMS_INPUT &&
                _g_receive_buffer[0] == '>' &&
                _g_receive_buffer[1] == ' ')
            {
                gsm_input_message();
            }
        }
    }
    else
    {
        if (_g_receive_buffer[0])
        {
            //printf("Got line from GSM: '%s' %d\r\n", _g_receive_buffer, _g_gsm_state);
            gsm_process_line(_g_gsm_state, _g_receive_buffer);
        }
    }
}

static void gsm_fill_message_buffer(const char c)
{
    if (c == '\r')
        return;

    else if (c != '\n')
    {
        if (_g_rx_index < MAX_SMS_BUFFER)
        {
            _g_message_buffer[_g_rx_index] = c;
            _g_rx_index++;
            _g_message_buffer[_g_rx_index] = 0;
        }
    }
    else
    {
        gsm_process_sms(_g_gsm_state, _g_receive_buffer, _g_message_buffer);
    }
}

static void gsm_puts(const char *str)
{
    while (*str)
        gsm_usart_put(*str++);
}

static void gsm_finish_operation(bool success)
{
    if (success)
    {
        if (_g_current_callback.cb.success_callback)
            _g_current_callback.cb.success_callback(_g_current_callback.cb.data);
    }
    else
    {
        if (_g_current_callback.cb.fail_callback)
            _g_current_callback.cb.fail_callback(_g_current_callback.cb.data);
    }

    memset(&_g_current_callback, 0, sizeof(gsm_cb_t));
    gsm_update_state(GSM_STATE_READY);
}

static void gsm_reset_buffer(void)
{
    _g_receive_buffer[0] = 0;
    _g_rx_index = 0;
}

static void gsm_reset_message_buffer(void)
{
    _g_message_buffer[0] = 0;
    _g_rx_index = 0;
}

static void gsm_abort_operation(void *data)
{
    printf("GSM: ERROR: Operation timed out\r\n");
    gsm_finish_operation(false);
}

static void gsm_update_state(uint8_t newstate)
{
    // Starting operation. Set abort timer.
    if (_g_gsm_state == GSM_STATE_READY && newstate != GSM_STATE_READY)
        _g_abort_timer = timeout_create(60000, true, false, &gsm_abort_operation, NULL);
    
    // Operation finished. Kill abort timer.
    if (newstate == GSM_STATE_READY && _g_gsm_state != GSM_STATE_READY)
    {
        timeout_stop(_g_abort_timer);
        timeout_destroy(_g_abort_timer);
    }

    //printf("gsm_update_state: old state: %d new state: %d\r\n", _g_gsm_state, newstate);
    _g_gsm_state = newstate;
}

static void gsm_process_line(uint8_t state, const char *line)
{
    if (!strcmp_p(line, "START"))
    {
        _g_init_flags = INIT_START;
        _g_gsm_state = GSM_STATE_INIT;
        goto done;
    }
    
    if (state == GSM_STATE_INIT)
    {
        if (!strcmp_p(line, "+CPIN: READY"))
            _g_init_flags |= INIT_CPIN;

        if (!strcmp_p(line, "SMS DONE"))
            _g_init_flags |= INIT_SMS;

        if (!strcmp_p(line, "PB DONE"))
            _g_init_flags |= INIT_PB;

        if (_g_init_flags == (INIT_START | INIT_CPIN | INIT_SMS | INIT_PB))
        {
            char send_buf[64];
            // All init messages seen. Turn echo off.
            sprintf(send_buf, "ATE0\r");
            gsm_puts(send_buf);
            gsm_update_state(GSM_STATE_AWAIT_ATE0);
        }
    }
    else if (state == GSM_STATE_AWAIT_ATE0)
    {
        if (!strcmp_p(line, "OK"))
        {
            char send_buf[64];
            // Now set message format.
            sprintf(send_buf, "AT+CMGF=1\r");
            gsm_puts(send_buf);
            gsm_update_state(GSM_STATE_AWAIT_CMGF);
        }
    }
    else if (state == GSM_STATE_AWAIT_CMGF)
    {
        if (!strcmp_p(line, "OK"))
        {
            gsm_finish_operation(true);
            _g_ready_callback();
        }
        else
        {
            gsm_finish_operation(false);
        }
    }
    else if (state == GSM_STATE_AWAIT_SEND_SMS_INPUT)
    {
        printf("GSM: ERROR: Failed to start send\r\n");
        // Received a full line instead of input prompt. Something went wrong.
        gsm_finish_operation(false);
    }
    else if (state == GSM_STATE_AWAIT_SEND_SMS_RESPONSE)
    {
        //printf("got: %s\r\n", line);

        if (!strncmp_p(line, "+CMGS:", 6))
            goto done;

        if (!strcmp_p(line, "OK"))
            gsm_finish_operation(true);
        else
            gsm_finish_operation(false);
    }
    else if (state == GSM_STATE_AWAIT_READ_SMS_META)
    {
        if (!strncmp_p(line, "+CMGR:", 6))
        {
            gsm_update_state(GSM_STATE_AWAIT_READ_SMS_TEXT);
            // Only reset buffer index. Keep buffer for completion of operation.
            _g_rx_index = 0;
            return;
        }
        else
        {
            gsm_finish_operation(false);
        }
    }
    else if (state == GSM_STATE_AWAIT_READ_ALL_SMS_META)
    {
        if (!strcmp_p(line, "OK"))
        {
            if (_g_current_callback.readsms_cb.endofmessages_callback)
                _g_current_callback.readsms_cb.endofmessages_callback(_g_current_callback.cb.data);

            memset(&_g_current_callback, 0, sizeof(gsm_readsms_cb_t));
            
            gsm_update_state(GSM_STATE_READY);
            goto done;
        }
        if (!strncmp_p(line, "+CMGL:", 6))
        {
            _g_gsm_state = GSM_STATE_AWAIT_READ_ALL_SMS_TEXT;
            _g_rx_index = 0;
            return;
        }
        else
        {
            gsm_finish_operation(false);
        }
    }
    else if (state == GSM_STATE_AWAIT_RESPONSE)
    {
        if (!strcmp_p(line, "OK"))
            gsm_finish_operation(true);
        else
            gsm_finish_operation(false);
    }

done:
    gsm_reset_buffer();
}

static void gsm_input_message(void)
{
    gsm_update_state(GSM_STATE_AWAIT_SEND_SMS_RESPONSE);
    gsm_puts(_g_message_buffer);
    //printf("buffer: %s\r\n", _g_message_buffer);
    gsm_usart_put(0x1A);
    gsm_reset_buffer();
}

static void gsm_process_sms(uint8_t state, char *meta, char *message)
{
    char *saveptr;
    char *index = NULL;
    char *flags;
    char *from;

    //printf("gsm_process_sms: '%s' '%s'\r\n", _g_receive_buffer, _g_message_buffer);    
    if (state == GSM_STATE_AWAIT_READ_SMS_TEXT)
    {
        uint8_t len;
        flags = csvfield(meta, &saveptr);

        if (!strncmp_p(flags, "+CMGR: \"", 8))
            flags += 8;

        len = strlen(flags);
        if (len && flags[len - 1] == '"')
            flags[len - 1] = 0;
    }
    else
    {
        index = csvfield(meta, &saveptr);

        if (!strncmp_p(index, "+CMGL: ", 7))
            index += 7;

        flags = csvfield(NULL, &saveptr);
    }

    from = csvfield(NULL, &saveptr);
    csvfield(NULL, &saveptr); /* Skip empty field */
    csvfield(NULL, &saveptr); /* Skip date */

    //printf("gsm_process_sms: message: '%s' index: '%s' flags: '%s' from: '%s' date: '%s'\r\n", message, index, flags, from, date);

    decode_ucs2(message);

    if (_g_current_callback.readsms_cb.success_callback)
        _g_current_callback.readsms_cb.success_callback(_g_current_callback.readsms_cb.data,
            state == GSM_STATE_AWAIT_READ_SMS_TEXT ? _g_last_index : atoi(index), from, flags, message);
    
    if (state == GSM_STATE_AWAIT_READ_SMS_TEXT)
    {
        memset(&_g_current_callback, 0, sizeof(gsm_readsms_cb_t));
        gsm_update_state(GSM_STATE_READY);
    }
    else
    {
        gsm_update_state(GSM_STATE_AWAIT_READ_ALL_SMS_META);
    }

    gsm_reset_message_buffer();
    gsm_reset_buffer();
}

void gsm_send_sms(const char *recipient, const char *message, gsm_cb_t *callback)
{
    char send_buf[64];

    if (_g_gsm_state != GSM_STATE_READY)
    {
        if (callback)
            callback->fail_callback(callback->data);
        return;
    }

    if (callback)
        memcpy(&_g_current_callback, callback, sizeof(gsm_cb_t));
    
    sprintf(send_buf, "AT+CMGS=\"%s\"\r", recipient);
    strncpy(_g_message_buffer, message, MAX_SMS);
    _g_message_buffer[MAX_SMS] = 0;

    gsm_update_state(GSM_STATE_AWAIT_SEND_SMS_INPUT);
    gsm_puts(send_buf);
}

void gsm_read_sms(int index, gsm_readsms_cb_t *callback)
{
    char send_buf[64];

    if (_g_gsm_state != GSM_STATE_READY)
    {
        if (callback)
            callback->fail_callback(callback->data);
        return;
    }

    if (callback)
        memcpy(&_g_current_callback, callback, sizeof(gsm_readsms_cb_t));

    _g_last_index = index;

    sprintf(send_buf, "AT+CMGR=%d\r", index);
    gsm_update_state(GSM_STATE_AWAIT_READ_SMS_META);
    gsm_puts(send_buf);
}

void gsm_read_unread_sms(gsm_readsms_cb_t *callback)
{
    char send_buf[64];

    //printf("gsm_read_all_sms: state: %d\r\n", _g_gsm_state);
    if (_g_gsm_state != GSM_STATE_READY)
    {
        if (callback)
            callback->fail_callback(callback->data);
        return;
    }

    if (callback)
        memcpy(&_g_current_callback, callback, sizeof(gsm_readsms_cb_t));

    sprintf(send_buf, "AT+CMGL=\"ALL\"\r");
    gsm_update_state(GSM_STATE_AWAIT_READ_ALL_SMS_META);
    gsm_puts(send_buf);
}

void gsm_delete_read_sms(gsm_cb_t *callback)
{
    char send_buf[64];

    if (_g_gsm_state != GSM_STATE_READY)
    {
        if (callback)
            callback->fail_callback(callback->data);
        return;
    }

    if (callback)
        memcpy(&_g_current_callback, callback, sizeof(gsm_cb_t));

    sprintf(send_buf, "AT+CMGD=0,2\r");
    gsm_update_state(GSM_STATE_AWAIT_RESPONSE);
    gsm_puts(send_buf);
}

void gsm_delete_sms(int index, gsm_cb_t *callback)
{
    char send_buf[64];

    if (_g_gsm_state != GSM_STATE_READY)
    {
        if (callback)
            callback->fail_callback(callback->data);
        return;
    }

    if (callback)
        memcpy(&_g_current_callback, callback, sizeof(gsm_cb_t));

    sprintf(send_buf, "AT+CMGD=%u,0\r", index);
    gsm_update_state(GSM_STATE_AWAIT_RESPONSE);
    gsm_puts(send_buf);
}
