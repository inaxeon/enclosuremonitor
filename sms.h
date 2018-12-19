/* 
 *   File:   sms.h
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

#ifndef __SMS_H__
#define __SMS_H__

void sms_init(sys_config_t *config);
void sms_process(void);
void sms_respond_to_source(const char *fmt, ...);
void sms_try_send(uint8_t type, uint8_t index, const char *message);
bool sms_can_send_message(void);

#endif /* __SMS_H__ */
