/* 
 *   File:   config.h
 *   Author: Matt
 *
 *   Created on 25 November 2014, 15:54
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

#ifndef __CONFIG_H__
#define __CONFIG_H__

#define DS18X20_ROMCODE_SIZE 8

typedef struct {
    int16_t low_threshold;
    int16_t high_threshold;
    uint8_t notify;
    char name[MAX_DESC];
} tempsensor_config_t;

typedef struct {
    uint8_t notify;
    uint8_t admin;
    char number[MAX_RECIPIENT];
} recipient_config_t;

typedef struct {
    uint16_t magic;
    uint8_t expected_sensors;
    uint16_t resend_delay;
    tempsensor_config_t temp_sensors[MAX_SENSORS];
    recipient_config_t sms_recipients[MAX_RECIPIENTS];
} sys_config_t;

void configuration_bootprompt(sys_config_t *config);
void load_configuration(sys_config_t *config);
int8_t configuration_prompt_handler(char *message, sys_config_t *config, bool sms);

#endif /* __CONFIG_H__ */