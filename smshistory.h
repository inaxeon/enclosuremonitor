/* 
 *   File:   smshistory.h
 *   Author: Matt
 *
 *   Created on 03 July 2018, 16:03
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

#ifndef __SMSHISTORY_H__
#define __SMSHISTORY_H__

#define MESSAGE_STARTUP           1
#define MESSAGE_TEMP_RANGE_LOW    2
#define MESSAGE_TEMP_RANGE_HIGH   3
#define MESSAGE_TEMP_STATE        4
#define MESSAGE_MAINS_STATE_OFF   5
#define MESSAGE_MAINS_STATE_ON    6

void sms_history_init(void);
bool sms_history_lodge(uint8_t type, uint8_t index, uint16_t seconds_till_next);

#endif /* __SMSHISTORY_H__ */
