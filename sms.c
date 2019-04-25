/* 
 *   File:   sms.c
 *   Author: Matt
 *
 *   Created on 17 December 2018, 06:10
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
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <avr/pgmspace.h>

#include "config.h"
#include "timeout.h"
#include "smshistory.h"
#include "util.h"
#include "sms.h"
#include "gsm.h"

#define SMS_STATE_INIT                       0
#define SMS_STATE_READY                      1

#define SMS_STATE_CMD_GET_UNREAD             2
#define SMS_STATE_CMD_GETTING_UNREAD         3
#define SMS_STATE_CMD_READ_UNREAD            4
#define SMS_STATE_CMD_START_EXEC             5
#define SMS_STATE_CMD_EXEC                   6
#define SMS_STATE_CMD_AWAIT_DELETE           7
#define SMS_STATE_CMD_START_DELETE           8
#define SMS_STATE_CMD_DELETE                 9

#define SMS_STATE_START_SENDALL              11
#define SMS_STATE_SENDALL                    12

#define MAX_UNREAD                           16

#define SMS_POLL_INTERVAL                    3000

typedef struct
{
    uint8_t state;
    char buffer[MAX_SMS + 1];
    bool perform_reset;
    uint8_t pos;
    uint8_t pos_processing;
    int8_t read_timer_handle;
    int8_t unread_messages[MAX_UNREAD];
    char *receive_buffer;
    const char *from_buffer;
    const char *sendall_buffer;
    sys_config_t *config;
} sms_state_t;

sms_state_t _g_sms_state;

static void sms_gsm_ready(void);
static void sms_send_buffer(sms_state_t *st);
static void sms_send_message_success(void *param);
static void sms_send_message_fail(void *param);
static void sms_read_message_success(void *data, int16_t index, const char *from, const char *status, const char *message);
static void sms_read_message_fail(void *data);
static void sms_read_messages_complete(void *data);
static void sms_delete_message_success(void *data);
static void sms_delete_message_fail(void *data);
static void sms_start_read_sms_messages(void *data);

extern void status_response(char *sendbuffer);

void sms_init(sys_config_t *config)
{
    sms_state_t *st = &_g_sms_state;

    st->state = SMS_STATE_INIT;
    st->read_timer_handle = -1;
    st->perform_reset = false;
    st->config = config;
    st->sendall_buffer = NULL;

    gsm_init(&sms_gsm_ready);
}

void sms_process(void)
{
    sms_state_t *st = &_g_sms_state;

    if (st->state == SMS_STATE_READY)
    {
        if (st->sendall_buffer)
        {
            st->state = SMS_STATE_START_SENDALL;
            return;
        }
        if (st->read_timer_handle < 0)
        {
            st->read_timer_handle = timeout_create(SMS_POLL_INTERVAL, true, false, &sms_start_read_sms_messages, (void *)st);
            if (st->read_timer_handle < 0)
            {
                printf("SMS: ERROR: Failed to start read SMS timer\r\n");
                return;
            }
        }
    }
    else if (st->state == SMS_STATE_CMD_GET_UNREAD)
    {
        gsm_readsms_cb_t cb;

        st->pos = 0;
        st->pos_processing = 0;
        st->state = SMS_STATE_CMD_GETTING_UNREAD;
        memset(st->unread_messages, -1, MAX_UNREAD);

        cb.success_callback = &sms_read_message_success;
        cb.fail_callback = &sms_read_message_fail;
        cb.endofmessages_callback = &sms_read_messages_complete;
        cb.data = st;

        printf("SMS: Start reading unread messages\r\n");

        gsm_read_unread_sms(&cb);
    }
    else if (st->state == SMS_STATE_CMD_READ_UNREAD)
    {
        gsm_readsms_cb_t cb;

        if (st->pos != st->pos_processing)
            return;

        if (st->unread_messages[st->pos] < 0)
        {
            st->state = SMS_STATE_READY;
            printf("SMS: No unread messages left to process\r\n");
            return;
        }

        cb.success_callback = &sms_read_message_success;
        cb.fail_callback = &sms_read_message_fail;
        cb.endofmessages_callback = NULL;
        cb.data = st;

        printf("SMS: Read message in position %u\r\n", st->unread_messages[st->pos]);

        gsm_read_sms(st->unread_messages[st->pos], &cb);

        st->pos_processing++;
    }
    else if (st->state == SMS_STATE_CMD_START_EXEC)
    {
        uint8_t i;

        for (i = 0; i < MAX_RECIPIENTS; i++)
        {
            if (match_phonenumber(st->config->sms_recipients[i].number, st->from_buffer))
            {
                printf("SMS: Matched recipient %u. Sending to command handler\r\n", i);

                if (!stricmp(st->buffer, "status"))
                {
                    status_response(st->buffer);
                    sms_send_buffer(st);
                    return;
                }

                if (st->config->sms_recipients[i].admin)
                {
                    if (!stricmp(st->buffer, "reset"))
                    {
                        st->state = SMS_STATE_CMD_EXEC;
                        sms_respond_to_source("Reset has been scheduled");
                        st->perform_reset = true;
                    }
                    else
                    {
                        st->state = SMS_STATE_CMD_EXEC;

                        if (configuration_prompt_handler(st->buffer, st->config, true) != 0)
                        {
                            sms_respond_to_source("Bad or unknown command");
                        }
                        else
                        {
                            if (st->state == SMS_STATE_CMD_EXEC)
                                sms_respond_to_source("Command accepted");
                        }
                    }
                    return;
                }
            }
        }

        printf("SMS: Sender not permitted to run command. Deleting message\r\n", i);
        st->state = SMS_STATE_CMD_START_DELETE;
    }
    else if (st->state == SMS_STATE_CMD_START_DELETE)
    {
        gsm_cb_t cb;

        cb.success_callback = &sms_delete_message_success;
        cb.fail_callback = &sms_delete_message_fail;
        cb.data = st;

        st->state = SMS_STATE_CMD_DELETE;

        printf("SMS: Starting delete of message in position %u\r\n", st->unread_messages[st->pos - 1]);

        gsm_delete_sms(st->unread_messages[st->pos - 1], &cb);
    }
    else if (st->state == SMS_STATE_START_SENDALL)
    {
        st->pos = 0;
        st->pos_processing = 0;

        st->state = SMS_STATE_SENDALL;
    }
    else if (st->state == SMS_STATE_SENDALL)
    {
        gsm_cb_t cb;
        recipient_config_t *recipient;

        if (st->pos != st->pos_processing)
            return;

        if (st->pos >= MAX_RECIPIENTS)
        {
            printf("SMS: No more recipients to send to\r\n");
            st->state = SMS_STATE_READY;
            st->sendall_buffer = NULL;
            return;
        }
            
        cb.success_callback = &sms_send_message_success;
        cb.fail_callback = &sms_send_message_fail;
        cb.data = st;
        
        recipient = &st->config->sms_recipients[st->pos];

        if (!*recipient->number)
        {
            printf("SMS: Not sending message to recipient in location %u. No number configured\r\n", st->pos);
            st->pos_processing++;
            st->pos++;
            return;
        }

        if (!recipient->notify)
        {
            printf("SMS: Not sending message to recipient in location %u. Not set for notify\r\n", st->pos);
            st->pos_processing++;
            st->pos++;
            return;
        }

        printf("SMS: Sending message '%s' to '%s'\r\n", st->sendall_buffer, recipient->number);
        gsm_send_sms(recipient->number, st->sendall_buffer, &cb);
        st->pos_processing++;
    }
}

static void sms_start_read_sms_messages(void *data)
{
    sms_state_t *st = (sms_state_t *)data;

    if (st->state == SMS_STATE_READY)
    {
        st->state = SMS_STATE_CMD_GET_UNREAD;
        printf("SMS: Timer started read of SMS messages\r\n");
    }
    else
    {
        printf("SMS: Not starting read of messages. Already busy\r\n");
    }

    timeout_destroy(st->read_timer_handle);
    st->read_timer_handle = -1;
}

static void sms_send_message_success(void *data)
{
    sms_state_t *st = (sms_state_t *)data;

    if (st->state == SMS_STATE_CMD_AWAIT_DELETE)
    {
        printf("SMS: Sent SMS message to single recipient\r\n");
        st->state = SMS_STATE_CMD_START_DELETE;
    }
    else if (st->state == SMS_STATE_SENDALL)
    {
        printf("SMS: Sent SMS message to one of multiple recipients\r\n");
        st->pos++;
    }
}

static void sms_send_message_fail(void *data)
{
    sms_state_t *st = (sms_state_t *)data;

    if (st->state == SMS_STATE_CMD_AWAIT_DELETE)
    {
        printf("SMS: ERROR: Failed to send SMS message to single recipient\r\n");
        st->state = SMS_STATE_CMD_START_DELETE;
    }
    else if (st->state == SMS_STATE_SENDALL)
    {
        printf("SMS: ERROR: Failed to send SMS message to one of multiple recipients\r\n");
        st->pos++;
    }
}

static void sms_read_message_success(void *data, int16_t index, const char *from, const char *status, const char *message)
{
    sms_state_t *st = (sms_state_t *)data;

    if (st->state == SMS_STATE_CMD_GETTING_UNREAD)
    {
        if (st->pos >= MAX_UNREAD)
        {
            printf("SMS: Too many unread messages to process\r\n");
        }
        else
        {
            printf("SMS: Flagging unread message: index: %d from: %s\r\n", index, from);
            st->unread_messages[st->pos++] = index;
        }
    }
    else if (st->state == SMS_STATE_CMD_READ_UNREAD)
    {
        printf("SMS: Read message '%s' from '%s'\r\n", message, from);
        strcpy(st->buffer, message);
        st->from_buffer = from;
        st->pos++;
        st->state = SMS_STATE_CMD_START_EXEC;
    }
}

static void sms_read_messages_complete(void *data)
{
    sms_state_t *st = (sms_state_t *)data;
    if (st->state == SMS_STATE_CMD_GETTING_UNREAD)
    {
        st->state = SMS_STATE_CMD_READ_UNREAD;
        st->pos_processing = 0;
        st->pos = 0;
    }
    else
    {
        printf("SMS: ERROR: Received read messages complete callback in invalid state\r\n");
    }
}

static void sms_read_message_fail(void *data)
{
    sms_state_t *st = (sms_state_t *)data;

    if (st->state == SMS_STATE_CMD_GETTING_UNREAD)
    {
        printf("SMS: ERROR: Failed to read message\r\n");
    }
    else if (st->state == SMS_STATE_CMD_READ_UNREAD)
    {
        printf("SMS: ERROR: Failed to read message\r\n");
        st->pos++;
    }
}

static void sms_delete_message_success(void *data)
{
    sms_state_t *st = (sms_state_t *)data;

    st->state = SMS_STATE_CMD_READ_UNREAD;
    printf("SMS: Completed delete of message\r\n");

    if (st->perform_reset)
    {
        printf("SMS: Performing reset\r\n");
        reset();
    }
}

static void sms_delete_message_fail(void *data)
{
    sms_state_t *st = (sms_state_t *)data;

    st->state = SMS_STATE_CMD_READ_UNREAD;

    printf("SMS: Failed to delete message\r\n");
}

static void sms_gsm_ready(void)
{
    printf("SMS: GSM modem initialisation complete\r\n");
    _g_sms_state.state = SMS_STATE_READY;
}

void sms_respond_to_source(const char *fmt, ...)
{
    va_list args;
    sms_state_t *st = &_g_sms_state;

    if (st->state == SMS_STATE_CMD_EXEC)
    {
        va_start(args, fmt);
        vsprintf(st->buffer, fmt, args);
        va_end(args);

        sms_send_buffer(st);
    }
    else
    {
        printf("SMS: ERROR: Trying to respond in wrong state\r\n");
    }
}

static void sms_send_buffer(sms_state_t *st)
{
    gsm_cb_t cb;

    cb.success_callback = &sms_send_message_success;
    cb.fail_callback = &sms_send_message_fail;
    cb.data = st;

    printf("SMS: Sending response '%s' to '%s'\r\n", st->buffer, st->from_buffer);

    gsm_send_sms(st->from_buffer, st->buffer, &cb);

    st->state = SMS_STATE_CMD_AWAIT_DELETE;
}

void sms_try_send(uint8_t type, uint8_t index, const char *message)
{
    sms_state_t *st = &_g_sms_state;

    if (sms_history_lodge(type, index, st->config->resend_delay))
        st->sendall_buffer = message;
    else
        printf("SMS: Too early to send message type '%u' index '%u'\r\n", type, index);
}

bool sms_can_send_message(void)
{
    sms_state_t *st = &_g_sms_state;
    return (st->sendall_buffer == NULL);
}
