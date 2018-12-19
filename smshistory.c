/* 
 *   File:   smshistory.c
 *   Author: Matt
 *
 *   Created on 03 July 2018, 16:03
 * 
 *   Keeps a history of message types sent, so that recipients don't get spammed
 *   by the same message type over and over.
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
#include <stdbool.h>
#include <string.h>

#include "smshistory.h"
#include "timeout.h"

#define MAX_SMS_HISTORY    8

typedef struct
{
    uint8_t type;
    uint8_t index;
    int32_t next_allowed_timestamp;
} history_entry_t;

uint8_t _g_history_idx;

history_entry_t _g_history[MAX_SMS_HISTORY];

void sms_history_init(void)
{
    _g_history_idx = 0;
    memset(_g_history, 0, sizeof(_g_history));
}

bool sms_history_lodge(uint8_t type, uint8_t index, uint16_t seconds_till_next)
{
    uint8_t i;

    for (i = 0; i < MAX_SMS_HISTORY; i++)
    {
        //printf("S: %u %lu %u %u\r\n", i, _g_history[i].next_allowed_timestamp, _g_history[i].type, _g_history[i].index);
        if (_g_history[i].type == type && _g_history[i].index == index)
        {
            // Not ready for another message like this yet. Discard.
            if (_g_history[i].next_allowed_timestamp > get_tick_count())
            {
                //printf("Rejecting message type: %u index: %u next_allowed_timestamp: %lu get_tick_count(): %lu\r\n", type, index,
                    //_g_history[i].next_allowed_timestamp, get_tick_count());
                return false;
            }
        }
    }

    _g_history[_g_history_idx].type = type;
    _g_history[_g_history_idx].index = index;
    _g_history[_g_history_idx].next_allowed_timestamp = (get_tick_count() + (seconds_till_next * TIMEOUT_TICK_PER_SECOND));

    _g_history_idx++;

    if (_g_history_idx >= MAX_SMS_HISTORY)
        _g_history_idx = 0;

    return true;
}
