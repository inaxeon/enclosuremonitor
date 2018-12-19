/*
 *   File:   util.c
 *   Author: Matt
 *
 *   Created on 09 August 2015, 14:29
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
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include <avr/wdt.h> 
#include <avr/eeprom.h> 
#include <avr/pgmspace.h>

#include "util.h"
#include "usart.h"
#include "sc16is7xx.h"
#include "config.h"

void reset(void)
{
    /* Uses the watch dog timer to reset */
    wdt_enable(WDTO_15MS);
    while (1);
}

void putch(char byte)
{
    while (console_busy());
    console_put(byte);
}

int print_char(char byte, FILE *stream)
{
    while (console_busy());
    console_put(byte);
    return 0;
}

char wdt_getch(void)
{
    while (!console_data_ready())
        CLRWDT();
    console_clear_oerr();
    return console_get();
}

void format_fixedpoint(char *buf, int16_t value, uint8_t type)
{
    char sign[2];

    sign[1] = 0;

    if ((type == I_1DP || type == I_2DP) && value < 0)
        sign[0] = '-';
    else
        sign[0] = 0;

    if (type == I_1DP || type == U_1DP)
        sprintf(buf, "%s%u.%u", sign, abs(value) / _1DP_BASE, abs(value) % _1DP_BASE);
    if (type == I_2DP || type == U_2DP)
        sprintf(buf, "%s%u.%02u", sign, abs(value) / _2DP_BASE, abs(value) % _2DP_BASE);
}

char *csvfield(char *s, char **saveptr)
{
    char *end;
    bool inquotes = false;
    bool havequotes = false;

    if (s == NULL)
        s = *saveptr;

    if (!(*s))
    {
        *saveptr = s;
        return NULL;
    }

    end = s;

    if (*s == '"')
        havequotes = true;

    while ((*end && *end != ',') || inquotes)
    {
        if (havequotes && *end == '"')
        {
            if (inquotes)
            {
                *end = 0;
                inquotes = false;
            }
            else
            {
                s++;
                inquotes = true;
            }
        }
        end++;
    }
    
    *end = 0;
    *saveptr = end + 1;
    return s;
}

void decode_ucs2(char *str)
{
    char hex[3];
    int len = strlen(str);
    int pos = 0;
    int dest = 0;

    hex[2] = 0;

    if (len % 4)
        return;
    
    if (len >= 4)
    {
        if (str[0] != '0' || str[1] != '0')
            return;
    }

    for (pos = 0; pos < len; pos += 4)
    {
        hex[0] = str[pos + 2];
        hex[1] = str[pos + 3];

        str[dest++] = (char)strtol(hex, NULL, 16);
    }

    str[dest] = 0;
}

bool match_phonenumber(const char *n1, const char *n2)
{
    const char *n;
    int diff;

    if (!*n1 || !*n2)
        return false;

    if ((*n1 == '+' && *n2 == '+') || (*n1 == '0' && *n2 == '0'))
        return strcmp(n1, n2) == 0;

    if (*n1 == '+' && *n2 == '0')
    {
        n = n1;
        n1 = n2;
        n2 = n;
    }
    else
    {
        if (*n1 != '0' && *n2 != '+')
            return false;
    }

    n1++;

    diff = strlen(n2) - strlen(n1);

    if (diff < 0)
        return false;

    n = (n2 + diff);

    return strcmp(n1, n) == 0;
}

void eeprom_write_data(uint8_t addr, uint8_t *bytes, uint8_t len)
{
    uint16_t dest = addr;
    eeprom_update_block(bytes, (void *)dest, len);
}

void eeprom_read_data(uint8_t addr, uint8_t *bytes, uint8_t len)
{
    uint16_t dest = addr;
    eeprom_read_block(bytes, (void *)dest, len);
}
