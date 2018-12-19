/*
 *   File:   gsm.h
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

#ifndef __GSM_H__
#define __GSM_H__

#define MAX_SMS (160 + 1)

typedef struct
{
    void *data;
    void (*fail_callback)(void *data);
    void (*success_callback)(void *data);
} gsm_cb_t;

typedef struct
{
    void *data;
    void (*fail_callback)(void *data);
    void (*success_callback)(void *data, int16_t index, const char *from, const char *status, char *message);
    void (*endofmessages_callback)(void *data);
} gsm_readsms_cb_t;

void gsm_init(void (*ready_callback)(void));
void gsm_process(void);
void gsm_send_sms(const char *recipient, const char *message, gsm_cb_t *callback);
void gsm_read_unread_sms(gsm_readsms_cb_t *callback);
void gsm_read_sms(int index, gsm_readsms_cb_t *callback);
void gsm_delete_read_sms(gsm_cb_t *callback);
void gsm_delete_sms(int index, gsm_cb_t *callback);
void gsm_delete_unread_sms(gsm_cb_t *callback);

#endif /* __GSM_H__ */