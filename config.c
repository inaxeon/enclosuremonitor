/* 
 *   File:   config.c
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

#include "project.h"

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <avr/pgmspace.h>
#include <util/delay.h>

#include "config.h"
#include "sms.h"
#include "util.h"
#include "usart.h"
#include "sc16is7xx.h"
#include "onewire.h"
#include "ds18x20.h"
#include "i2c.h"
#include "adc.h"
#include "gsm.h"

#define CMD_NONE              0x00
#define CMD_READLINE          0x01
#define CMD_COMPLETE          0x02
#define CMD_ESCAPE            0x03
#define CMD_AWAIT_NAV         0x04
#define CMD_PREVCOMMAND       0x05
#define CMD_NEXTCOMMAND       0x06
#define CMD_DEL               0x07
#define CMD_DROP_NAV          0x08
#define CMD_CANCEL            0x10

#define CTL_CANCEL            0x03
#define CTL_XOFF              0x13
#define CTL_U                 0x15

#define SEQ_ESCAPE_CHAR       0x1B
#define SEQ_CTRL_CHAR1        0x5B
#define SEQ_ARROW_UP          0x41
#define SEQ_ARROW_DOWN        0x42
#define SEQ_HOME              0x31
#define SEQ_INS               0x32
#define SEQ_DEL               0x33
#define SEQ_END               0x34
#define SEQ_PGUP              0x35
#define SEQ_PGDN              0x36
#define SEQ_NAV_END           0x7E

#define CMD_MAX_LINE          64
#define CMD_MAX_HISTORY       4

#define PARAM_I16_1DP_TEMP    0
#define PARAM_U16             1
#define PARAM_U16_1DP_TEMPMAX 2
#define PARAM_U8              3
#define PARAM_U8H             4
#define PARAM_U8_PCT          5
#define PARAM_U8_BIT          6
#define PARAM_U8_SIDX         7
#define PARAM_U8_RIDX         8
#define PARAM_U8_TCNT         9
#define PARAM_U16H            10
#define PARAM_DESC            11
#define PARAM_PHONENUMBER     12

static int8_t get_line(char *str, int8_t max, uint8_t *ignore_lf);
static bool parse_param(void *param, uint8_t type, char *arg);
static void temp_sensor_prompt(tempsensor_config_t *sensorconfig, uint8_t num);
static void sms_recipient_prompt(recipient_config_t *sensorconfig, uint8_t num);
static void do_show_sms_recipient(recipient_config_t *recipientconfig, int8_t index, bool sms);
static void default_sms_recipient(recipient_config_t *recipientconfig);
static int8_t sms_recipient_prompt_handler(char *text, recipient_config_t *recipientconfig, bool sms, bool *needs_save);
static void do_show_temp_sensor(tempsensor_config_t *sensorconfig, int8_t index, bool sms);
static void default_tempsensor(tempsensor_config_t *sensorconfig);
static int8_t temp_sensor_prompt_handler(char *text, tempsensor_config_t *sensorconfig, bool sms, bool *needs_save);
static void save_configuration(sys_config_t *config);
static void default_configuration(sys_config_t *config);
static bool do_i2c_read_reg(char *args);
static bool do_i2c_read_reg16(char *args);
static bool do_i2c_read_buf(char *args);
static bool do_i2c_write_reg(char *args);
static bool do_i2c_write_reg16(char *args);
static void do_battery(bool sms);
static void do_modem(void);
static void do_readtemp(void);

uint8_t _g_max_history;
uint8_t _g_show_history;
uint8_t _g_next_history;
char _g_cmd_history[CMD_MAX_HISTORY][CMD_MAX_LINE];

void configuration_bootprompt(sys_config_t *config)
{
    char cmdbuf[64];
    uint8_t i;
    int8_t enter_bootpromt = 0;
    uint8_t ignore_lf = 0;

    _g_max_history = 0;
    _g_show_history = 0;
    _g_next_history = 0;

    memset(_g_cmd_history, 0, CMD_MAX_HISTORY * CMD_MAX_LINE);

    printf("<Press Ctrl+C to enter configuration prompt>\r\n");

    for (i = 0; i < 100; i++)
    {
        if (console_data_ready())
        {
            char c = console_get();
            if (c == 3) /* Ctrl + C */
            {
                enter_bootpromt = 1;
                break;
            }
        }
        _delay_ms(10);
    }

    if (!enter_bootpromt)
        return;

    printf("\r\n");
    
    for (;;)
    {
        int8_t ret;

        printf("config>");
        ret = get_line(cmdbuf, sizeof(cmdbuf), &ignore_lf);

        if (ret == 0 || ret == -1) {
            printf("\r\n");
            continue;
        }

        ret = configuration_prompt_handler(cmdbuf, config, false);

        if (ret > 0)
            printf("Error: command failed\r\n");

        if (ret == -1) {
            return;
        }
    }
}

static void do_show(sys_config_t *config)
{
    printf("\r\nCurrent configuration:\r\n\r\n"
            "\tresenddelay ...........: %u\r\n"
            "\texpectedsensors .......: %u\r\n",
            config->resend_delay,
            config->expected_sensors
        );

    printf("\r\n");
}

static void do_help(void)
{
    printf(
        "\r\nCommands:\r\n\r\n"
        "\tresenddelay [1 to 65535]\r\n"
        "\t\tDelay to re-notify fault conditions (seconds)\r\n\r\n"
        "\texpectedsensors [1 to %u]\r\n"
        "\t\tNumber of temperature sensors which should be attached\r\n\r\n"
        "\ttempsensor [1 to %u]\r\n"
        "\t\tConfigure attached 1-wire temperature sensors\r\n\r\n"
        "\trecipient [1 to %u]\r\n"
        "\t\tConfigure SMS recipients\r\n\r\n"
        "\ti2creadbuf [devaddr(h)] [offset(h)] [len(d)]\r\n"
        "\ti2creadreg [devaddr(h)] [reg(h)]\r\n"
        "\ti2creadreg16 [devaddr(h)] [reg(h)]\r\n"
        "\ti2cwritereg [devaddr(h)] [reg(h)] [data]\r\n"
        "\ti2cwritereg16 [devaddr(h)] [reg(h)] [data(h16)]\r\n"
        "\t\tRead and write to I2C devices\r\n\r\n"
        "\tbattery\r\n"
        "\t\tShow battery voltage\r\n\r\n"
        "\tmodem\r\n"
        "\t\tConnect this terminal to GSM modem for manual command entry\r\n\r\n"
        "\treadtemp\r\n"
        "\t\tShow attached 1-wire temperature sensors\r\n\r\n"
        "\tshow\r\n"
        "\t\tShow current configuration\r\n\r\n"
        "\tdefault\r\n"
        "\t\tLoad the default configuration\r\n\r\n"
        "\tsave\r\n"
        "\t\tSave current configuration\r\n\r\n"
        "\trun\r\n"
        "\t\tExit this menu and start\r\n\r\n"
        "\treset\r\n"
        "\t\tPerform a hard reset\r\n\r\n"
        , MAX_SENSORS, MAX_SENSORS, MAX_RECIPIENTS);
}

static void do_tempsensor_help(void)
{
    printf(
        "\r\nCommands:\r\n\r\n"
        "\tdesc [desc]\r\n"
        "\tnotify [0 or 1]\r\n"
        "\t\tIf set to 1 this sensor will generate alerts\r\n\r\n"
        "\t\tSets the description of this sensor (%u chars max)\r\n\r\n"
        "\tlowthreshold [-55.0 to 125.0]\r\n"
        "\t\tSets the threshold for the low temperature alert\r\n\r\n"
        "\thighthreshold [-55.0 to 125.0]\r\n"
        "\t\tSets the threshold for the high temperature alert\r\n\r\n"
        "\tshow\r\n"
        "\t\tShow current configuration for this sensor\r\n\r\n"
        "\tdefault\r\n"
        "\t\tLoad the default sensor configuration\r\n\r\n"
        "\texit\r\n"
        "\t\tReturn to the main configuration prompt\r\n\r\n"
        , MAX_DESC);
}

static void do_sms_recipient_help(void)
{
    printf(
        "\r\nCommands:\r\n\r\n"
        "\tnumber [phone number]\r\n"
        "\t\tSets the phone number of the recipient\r\n"
        "\t\tCan be +XX or 0XX format (%u chars max)\r\n\r\n"
        "\tnotify [0 or 1]\r\n"
        "\t\tIf set to 1 this number will receive alerts\r\n\r\n"
        "\tadmin [0 or 1]\r\n"
        "\t\tIf set to 1 this number can send configuration messages\r\n\r\n"
        "\tshow\r\n"
        "\t\tShow current configuration for this sensor\r\n\r\n"
        "\tdefault\r\n"
        "\t\tLoad the default sensor configuration\r\n\r\n"
        "\texit\r\n"
        "\t\tReturn to the main configuration prompt\r\n\r\n"
        , MAX_RECIPIENT);
}

int8_t configuration_prompt_handler(char *text, sys_config_t *config, bool sms)
{
    char *command;
    char *arg;
    bool needs_save = false;

    command = strtok(text, " ");
    arg = strtok(NULL, "");
        
    if (!stricmp(command, "resenddelay")) {
        if (!parse_param(&config->resend_delay, PARAM_U16, arg))
            return 1;
        needs_save = true;
    }
    else if (!stricmp(command, "expectedsensors")) {
        if (!parse_param(&config->expected_sensors, PARAM_U8_SIDX, arg))
            return 1;
        needs_save = true;
    }
    else if (!stricmp(command, "i2creadreg")) {

        if (sms)
            return 1;

        if (!do_i2c_read_reg(arg))
            return 1;
    }
    else if (!stricmp(command, "i2creadreg16")) {

        if (sms)
            return 1;

        if (!do_i2c_read_reg16(arg))
            return 1;
    }
    else if (!stricmp(command, "i2creadbuf")) {

        if (sms)
            return 1;

        if (!do_i2c_read_buf(arg))
            return 1;
    }
    else if (!stricmp(command, "i2cwritereg")) {

        if (sms)
            return 1;

        if (!do_i2c_write_reg(arg))
            return 1;
    }
    else if (!stricmp(command, "i2cwritereg16")) {

        if (sms)
            return 1;

        if (!do_i2c_write_reg16(arg))
            return 1;
    }
    else if (!stricmp(command, "readtemp")) {

        if (sms)
            return 1;

        do_readtemp();
    }
    else if (!stricmp(command, "battery")) {
        do_battery(sms);
    }
    else if (!stricmp(command, "modem")) {
        do_modem();
    }
    else if (!stricmp(command, "tempsensor") || !stricmp(command, "sensor")) {
        char *p1 = strtok(arg, " ");
        uint8_t p;

        if (!parse_param(&p, PARAM_U8_SIDX, p1))
            return 1;
        p--;

        if (!sms)
            temp_sensor_prompt(&config->temp_sensors[p], p);
        else
            temp_sensor_prompt_handler(strtok(NULL, ""), &config->temp_sensors[p], sms, &needs_save);        
    }
    else if (!stricmp(command, "recipient") || !stricmp(command, "user")) {
        char *p1 = strtok(arg, " ");
        uint8_t p;

        if (!parse_param(&p, PARAM_U8_RIDX, p1))
            return 1;
        p--;

        if (!sms)
            sms_recipient_prompt(&config->sms_recipients[p], p);
        else
            sms_recipient_prompt_handler(strtok(NULL, ""), &config->sms_recipients[p], sms, &needs_save);
    }
    else if (!stricmp(command, "save")) {
        save_configuration(config);
        printf("\r\nConfiguration saved.\r\n\r\n");
        return 0;
    }
    else if (!stricmp(command, "default")) {

        if (sms)
            return 1;

        default_configuration(config);
        printf("\r\nDefault configuration loaded.\r\n\r\n");
        return 0;
    }
    else if (!stricmp(command, "reset")) {
        reset();
        return 0;
    }
    else if (!stricmp(command, "run")) {

        if (sms)
            return 1;

        printf("\r\nStarting...\r\n");
        return -1;
    }
    else if (!stricmp(command, "show")) {
        uint8_t i;
        if (sms)
            return 1;

        do_show(config);

        for (i = 0; i < MAX_RECIPIENTS; i++)
            do_show_sms_recipient(&config->sms_recipients[i], i, false);
        for (i = 0; i < MAX_SENSORS; i++)
            do_show_temp_sensor(&config->temp_sensors[i], i, false);
    }
    else if ((!stricmp(command, "help") || !stricmp(command, "?"))) {

        if (sms)
            return 1;

        do_help();
        return 0;
    }
    else
    {
        if (!sms)
            printf("Error: no such command (%s)\r\n", command);

        return 1;
    }

    if (sms && needs_save)
    {
        printf("Saving after SMS initiated configuration change\r\n");
        save_configuration(config);
    }

    return 0;
}

static void do_readtemp(void)
{
    uint8_t i;
    uint8_t num_sensors;
    uint8_t sensor_ids[MAX_SENSORS][DS18X20_ROMCODE_SIZE];
    int16_t reading;
    
    if (ds18x20_search_sensors(&num_sensors, sensor_ids))
        printf("\r\nFound %u of %u maximum sensors\r\n", num_sensors, MAX_SENSORS);
    else
        printf("\r\nHardware error searching for sensors\r\n");

    if (!num_sensors)
        goto done;
    
    for (i = 0; i < num_sensors; i++)
        ds18x20_start_meas(sensor_ids[i]);

    _delay_ms(750);

    for (i = 0; i < num_sensors; i++)
    {
        if (ds18x20_read_decicelsius(sensor_ids[i], &reading))
        {
            fixedpoint_sign(reading, reading);

            printf(
                "\r\nSensor %u:\r\n"
                "\tTemp (C) .............: %s%u.%u\r\n"
                "\tBurned in ID .........: %02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X\r\n",
                i + 1,
                fixedpoint_arg(reading, reading),
                sensor_ids[i][0]
#if DS18X20_ROMCODE_SIZE == 8
              , sensor_ids[i][1],
                sensor_ids[i][2],
                sensor_ids[i][3],
                sensor_ids[i][4],
                sensor_ids[i][5],
                sensor_ids[i][6],
                sensor_ids[i][7]
#else
              , 0, 0, 0, 0, 0, 0, 0
#endif /* DS18X20_ROMCODE_SIZE == 8 */
            );
        }
        else
        {
            printf("Failed to read sensor %u\r\n", i + 1);
        }
    }
    
done:
    printf("\r\n");
}

static int8_t sms_recipient_prompt_handler(char *text, recipient_config_t *recipientconfig, bool sms, bool *needs_save)
{
    char *command;
    char *arg;

    command = strtok(text, " ");
    arg = strtok(NULL, "");

    if (!stricmp(command, "number")) {
        if (!parse_param(recipientconfig->number, PARAM_PHONENUMBER, arg))
            return 1;
        *needs_save = true;
    }
    else if (!stricmp(command, "notify")) {
        if (!parse_param(&recipientconfig->notify, PARAM_U8_BIT, arg))
            return 1;
        *needs_save = true;
    }
    else if (!stricmp(command, "admin")) {
        if (!parse_param(&recipientconfig->admin, PARAM_U8_BIT, arg))
            return 1;
        *needs_save = true;
    }
    else if (!stricmp(command, "show")) {
        do_show_sms_recipient(recipientconfig, -1, sms);
        return 0;
    }
    else if ((!stricmp(command, "help") || !stricmp(command, "?"))) {
        do_sms_recipient_help();
        return 0;
    }
    else if (!stricmp(command, "exit")) {
        return -1;
    }
    else if (!stricmp(command, "default")) {
        default_sms_recipient(recipientconfig);
        printf("\r\nDefault recipient config loaded.\r\n\r\n");
        *needs_save = true;
        return 0;
    }
    else
    {
        printf("Error: no such command (%s)\r\n", command);
        return 1;
    }

    return 0;
}

static void do_show_sms_recipient(recipient_config_t *recipientconfig, int8_t index, bool sms)
{
    if (!sms)
    {
        if (index < 0)
            printf("\r\nSMS Recipient parameters:\r\n\r\n");
        else
            printf("SMS Recipient %u:\r\n", index + 1);

        printf(
            "\tnumber ...............: %s\r\n"
            "\tnotify ...............: %u\r\n"
            "\tadmin ................: %u\r\n\r\n",
            recipientconfig->number,
            recipientconfig->notify,
            recipientconfig->admin
        );
    }
    else
    {
        sms_respond_to_source(
            "Number: %s\nNotify: %u\nAdmin: %u",
            recipientconfig->number,
            recipientconfig->notify,
            recipientconfig->admin
        );    
    }
}

static void sms_recipient_prompt(recipient_config_t *sensorconfig, uint8_t num)
{
    char cmdbuf[64];
    bool needs_save_dummy;
    uint8_t ignore_lf = 0;

    for (;;)
    {
        int8_t ret;

        printf("config(recipient%u)>", num + 1);
        ret = get_line(cmdbuf, sizeof(cmdbuf), &ignore_lf);

        if (ret == 0 || ret == -1) {
            /* Ctrl + C */
            printf("\r\n");
            continue;
        }

        ret = sms_recipient_prompt_handler(cmdbuf, sensorconfig, false, &needs_save_dummy);

        if (ret > 0)
            printf("Error: command failed\r\n");

        if (ret == -1) {
            return;
        }
    }
}

static void default_sms_recipient(recipient_config_t *recipientconfig)
{
    recipientconfig->number[0] = 0;
    recipientconfig->notify = 0;
    recipientconfig->admin = 0;
}

static int8_t temp_sensor_prompt_handler(char *text, tempsensor_config_t *sensorconfig, bool sms, bool *needs_save)
{
    char *command;
    char *arg;

    command = strtok(text, " ");
    arg = strtok(NULL, "");

    if (!stricmp(command, "name")) {
        if (!parse_param(sensorconfig->name, PARAM_DESC, arg))
            return 1;
        *needs_save = true;
    }
    else if (!stricmp(command, "notify")) {
        if (!parse_param(&sensorconfig->notify, PARAM_U8_BIT, arg))
            return 1;
        *needs_save = true;
    }
    else if (!stricmp(command, "lowthreshold") || !stricmp(command, "low")) {
        if (!parse_param(&sensorconfig->low_threshold, PARAM_I16_1DP_TEMP, arg))
            return 1;
        *needs_save = true;
    }
    else if (!stricmp(command, "highthreshold") || !stricmp(command, "high")) {
        if (!parse_param(&sensorconfig->high_threshold, PARAM_I16_1DP_TEMP, arg))
            return 1;
        *needs_save = true;
    }
    else if (!stricmp(command, "show")) {
        do_show_temp_sensor(sensorconfig, -1, sms);
        return 0;
    }
    else if ((!stricmp(command, "help") || !stricmp(command, "?"))) {
        do_tempsensor_help();
        return 0;
    }
    else if (!stricmp(command, "exit")) {
        return -1;
    }
    else if (!stricmp(command, "default")) {
        default_tempsensor(sensorconfig);
        printf("\r\nDefault profile loaded.\r\n\r\n");
        *needs_save = true;
        return 0;
    }
    else
    {
        printf("Error: no such command (%s)\r\n", command);
        return 1;
    }

    return 0;
}

static void do_show_temp_sensor(tempsensor_config_t *sensorconfig, int8_t index, bool sms)
{
    char low_threshold_buf[MAX_FDP];
    char high_threshold_buf[MAX_FDP];

    format_i16_1dp(low_threshold_buf, sensorconfig->low_threshold);
    format_i16_1dp(high_threshold_buf, sensorconfig->high_threshold);

    if (!sms)
    {
        if (index < 0)
            printf("\r\nTemperature sensor parameters:\r\n\r\n");
        else
            printf("Temperature sensor %u:\r\n", index + 1);

        printf(
            "\tname .................: %s\r\n"
            "\tnotify ...............: %u\r\n"
            "\tlowthreshold .........: %s\r\n"
            "\thighthreshold ........: %s\r\n\r\n",
            sensorconfig->name,
            sensorconfig->notify,
            low_threshold_buf,
            high_threshold_buf
        );
    }
    else
    {
        sms_respond_to_source("Name: %s\nNotify: %u\nLowThreshold: %s\nHighThreshold: %s",
            sensorconfig->name,
            sensorconfig->notify,
            low_threshold_buf,
            high_threshold_buf
        );
    }
}

static void temp_sensor_prompt(tempsensor_config_t *sensorconfig, uint8_t num)
{
    char cmdbuf[64];
    bool needs_save_dummy;
    uint8_t ignore_lf = 0;

    for (;;)
    {
        int8_t ret;

        printf("config(tempsensor%u)>", num + 1);
        ret = get_line(cmdbuf, sizeof(cmdbuf), &ignore_lf);

        if (ret == 0 || ret == -1) {
            /* Ctrl + C */
            printf("\r\n");
            continue;
        }

        ret = temp_sensor_prompt_handler(cmdbuf, sensorconfig, false, &needs_save_dummy);

        if (ret > 0)
            printf("Error: command failed\r\n");

        if (ret == -1) {
            return;
        }
    }
}

static void default_tempsensor(tempsensor_config_t *sensorconfig)
{
    sensorconfig->name[0] = 0;
    sensorconfig->notify = 1;
    sensorconfig->low_threshold = 150;
    sensorconfig->high_threshold = 300;
}

static bool do_i2c_read_reg(char *args)
{
    uint8_t devaddr;
    uint8_t reg;
    uint8_t data;

    char *strdevaddr = strtok(args, " ");
    char *strreg = strtok(NULL, "");

    if (!parse_param(&devaddr, PARAM_U8H, strdevaddr))
        goto badparam;
    if (!parse_param(&reg, PARAM_U8H, strreg))
        goto badparam;

    if (!i2c_read(devaddr, reg, &data))
        return false;

    printf("0x%02X\r\n", data);

    return true;

badparam:
    printf("Error: Bad parameter\r\n");
    return false;
}

static bool do_i2c_read_reg16(char *args)
{
    uint8_t devaddr;
    uint8_t reg;
    uint16_t data;
    
    char *strdevaddr = strtok(args, " ");
    char *strreg = strtok(NULL, "");

    if (!parse_param(&devaddr, PARAM_U8H, strdevaddr))
        goto badparam;
    if (!parse_param(&reg, PARAM_U8H, strreg))
        goto badparam;

    if (!i2c_read16(devaddr, reg, &data))
        return false;

    printf("0x%04X\r\n", data);

    return true;

badparam:
    printf("Error: Bad parameter\r\n");
    return false;
}

#define CMD_I2C_READBUF_MAX 32

static bool do_i2c_read_buf(char *args)
{
    uint8_t devaddr;
    uint8_t len;
    uint8_t offset;
    uint8_t buffer[CMD_I2C_READBUF_MAX];
    
    char *strdevaddr = strtok(args, " ");
    char *stroffset = strtok(NULL, " ");
    char *strlen = strtok(NULL, "");

    if (!parse_param(&devaddr, PARAM_U8H, strdevaddr))
        goto badparam;
    if (!parse_param(&offset, PARAM_U8H, stroffset))
        goto badparam;
    if (!parse_param(&len, PARAM_U8H, strlen))
        goto badparam;

    if (!len || len > CMD_I2C_READBUF_MAX)
        goto badparam;

    if (!i2c_read_buf(devaddr, offset, buffer, len))
        return false;

    offset = 0;

    while (1)
    {
        uint8_t i;
        uint8_t thisblk = len > 8 ? 8 : len;
        
        printf("%02X: ", offset);
        
        for (i = 0; i < thisblk; i++)
            printf("%02X%s", buffer[offset + i], i == (thisblk - 1) ? "" : " ");
        
        len -= thisblk;
        offset += thisblk;
        
        printf("\r\n");

        if (len == 0)
            break;
    }

    return true;

badparam:
    printf("Error: Bad parameter\r\n");
    return false;
}

static bool do_i2c_write_reg(char *args)
{
    uint8_t devaddr;
    uint8_t reg;
    uint8_t data;

    char *strdevaddr = strtok(args, " ");
    char *strreg = strtok(NULL, " ");
    char *strdata = strtok(NULL, "");

    if (!parse_param(&devaddr, PARAM_U8H, strdevaddr))
        goto badparam;
    if (!parse_param(&reg, PARAM_U8H, strreg))
        goto badparam;
    if (!parse_param(&data, PARAM_U8H, strdata))
        goto badparam;

    if (!i2c_write(devaddr, reg, data))
        return false;

    return true;

badparam:
    printf("Error: Bad parameter\r\n");
    return false;
}

static bool do_i2c_write_reg16(char *args)
{
    uint8_t devaddr;
    uint8_t reg;
    uint16_t data;
    
    char *strdevaddr = strtok(args, " ");
    char *strreg = strtok(NULL, " ");
    char *strdata = strtok(NULL, "");

    if (!parse_param(&devaddr, PARAM_U8H, strdevaddr))
        goto badparam;
    if (!parse_param(&reg, PARAM_U8H, strreg))
        goto badparam;
    if (!parse_param(&data, PARAM_U16H, strdata))
        goto badparam;

    if (!i2c_write16(devaddr, reg, data))
        return false;

    return true;
badparam:
    printf("Error: Bad parameter\r\n");
    return false;
}

static void do_battery(bool sms)
{
    uint16_t battery_voltage = adc_read_battery();

    if (!sms)
        printf("\r\nBattery voltage: %u.%02u\r\n\r\n", fixedpoint_arg_u_2dp(battery_voltage));
    else
        sms_respond_to_source("Battery: %u.%02u V\n(Full: 4.15 V)\n(Empty: 3.20 V)", fixedpoint_arg_u_2dp(battery_voltage));
}

static void do_modem(void)
{
    printf(
        "\r\nPress <Esc> at any time to return to configuration prompt\r\n"
        "Allow a few seconds for modem to boot\r\n"
    );

    gsm_init(NULL);

    for (;;)
    {
        if (console_data_ready())
        {
            char c = console_get();
            if (c == 0x1B)
                break;
            gsm_usart_put(c);
        }
        if (gsm_usart_data_ready())
            console_put(gsm_usart_get());
    }

    printf("\r\n");
}

static bool parse_param(void *param, uint8_t type, char *arg)
{
    int16_t i16param;
    uint16_t u16param;
    uint8_t u8param;
    uint8_t dp = 0;
    uint8_t un = 0;
    char *s;
    char *sparam;

    if (!arg || !*arg)
    {
        /* Avoid stack overflow */
        printf("Error: Missing parameter\r\n");
        return false;
    }

    switch (type)
    {
        case PARAM_U8_PCT:
        case PARAM_U8_BIT:
        case PARAM_U8_SIDX:
        case PARAM_U8_RIDX:
        case PARAM_U8_TCNT:
            if (*arg == '-')
                return false;
            u8param = (uint8_t)atoi(arg);
            if (type == PARAM_U8_BIT && u8param > 1)
                return false;
            if (type == PARAM_U8_PCT && u8param > 100)
                return false;
            if (type == PARAM_U8_TCNT && u8param > 6)
                return false;
            if (type == PARAM_U8_SIDX && (u8param > MAX_SENSORS || u8param < 1))
                return false;
            if (type == PARAM_U8_RIDX && (u8param > MAX_RECIPIENTS || u8param < 1))
                return false;
            *(uint8_t *)param = u8param;
            break;
        case PARAM_U8H:
            u8param = (uint8_t)strtol(arg, NULL, 16);
            *(uint8_t *)param = u8param;
            break;
        case PARAM_I16_1DP_TEMP:
        case PARAM_U16:
        case PARAM_U16_1DP_TEMPMAX:
            s = strtok(arg, ".");
            i16param = atoi(s);
            switch (type)
            {
                case PARAM_U16:
                    un = 1;
                    break;
                case PARAM_I16_1DP_TEMP:
                    i16param *= _1DP_BASE;
                    dp = 1;
                    break;
                case PARAM_U16_1DP_TEMPMAX:
                    i16param *= _1DP_BASE;
                    dp = 1;
                    un = 1;
                    break;
            }

            if (un && *arg == '-')
                return false;

            s = strtok(NULL, "");
            if (s && *s != 0)
            {
                if (dp == 0)
                    return false;
                if (dp == 1 && strlen(s) > 1)
                    return false;
                
                if (*arg == '-')
                    i16param -= atoi(s);
                else
                    i16param += atoi(s);
            }
            if (type == PARAM_I16_1DP_TEMP)
            {
                if (i16param < -550)
                    i16param = -550;
                if (i16param > 1250)
                    i16param = 1250;
            }
            if (type == PARAM_U16_1DP_TEMPMAX)
            {
                if (i16param > 1800)
                    i16param = 1800;
            }
            *(int16_t *)param = i16param;
            break;
        case PARAM_U16H:
            u16param = (uint16_t)strtol(arg, NULL, 16);
            *(uint16_t *)param = u16param;
            break;
        case PARAM_DESC:
            sparam = (char *)param;
            strncpy(sparam, arg, MAX_DESC);
            sparam[MAX_DESC - 1] = 0;
            break;
        case PARAM_PHONENUMBER:
            {
                char *s = (char *)arg;
                while (*s)
                {
                    if (!((*s >= '0' && *s <= '9') || *s == '+'))
                        return false;
                    s++;
                }
                
                sparam = (char *)param;
                strncpy(sparam, arg, MAX_RECIPIENT);
                sparam[MAX_RECIPIENT - 1] = 0;
                break;
            }
        
    }
    return true;
}

static void cmd_erase_line(uint8_t count)
{
    printf("%c[%dD%c[K", SEQ_ESCAPE_CHAR, count, SEQ_ESCAPE_CHAR);
}

static void config_next_command(char *cmdbuf, int8_t *count)
{
    uint8_t previdx;

    if (!_g_max_history)
        return;

    if (*count)
        cmd_erase_line(*count);

    previdx = ++_g_show_history;

    if (_g_show_history == CMD_MAX_HISTORY)
    {
        _g_show_history = 0;
        previdx = 0;
    }

    strcpy(cmdbuf, _g_cmd_history[previdx]);
    *count = strlen(cmdbuf);
    printf("%s", cmdbuf);
}

static void config_prev_command(char *cmdbuf, int8_t *count)
{
    uint8_t previdx;

    if (!_g_max_history)
        return;

    if (*count)
        cmd_erase_line(*count);

    if (_g_show_history == 0)
        _g_show_history = CMD_MAX_HISTORY;

    previdx = --_g_show_history;

    strcpy(cmdbuf, _g_cmd_history[previdx]);
    *count = strlen(cmdbuf);
    printf("%s", cmdbuf);
}

static int get_string(char *str, int8_t max, uint8_t *ignore_lf)
{
    unsigned char c;
    uint8_t state = CMD_READLINE;
    int8_t count;

    count = 0;
    do {
        c = wdt_getch();

        if (state == CMD_ESCAPE) {
            if (c == SEQ_CTRL_CHAR1) {
                state = CMD_AWAIT_NAV;
                continue;
            }
            else {
                state = CMD_READLINE;
                continue;
            }
        }
        else if (state == CMD_AWAIT_NAV)
        {
            if (c == SEQ_ARROW_UP) {
                config_prev_command(str, &count);
                state = CMD_READLINE;
                continue;
            }
            else if (c == SEQ_ARROW_DOWN) {
                config_next_command(str, &count);
                state = CMD_READLINE;
                continue;
            }
            else if (c == SEQ_DEL) {
                state = CMD_DEL;
                continue;
            }
            else if (c == SEQ_HOME || c == SEQ_END || c == SEQ_INS || c == SEQ_PGUP || c == SEQ_PGDN) {
                state = CMD_DROP_NAV;
                continue;
            }
            else {
                state = CMD_READLINE;
                continue;
            }
        }
        else if (state == CMD_DEL) {
            if (c == SEQ_NAV_END && count) {
                putch('\b');
                putch(' ');
                putch('\b');
                count--;
            }

            state = CMD_READLINE;
            continue;
        }
        else if (state == CMD_DROP_NAV) {
            state = CMD_READLINE;
            continue;
        }
        else
        {
            if (count >= max) {
                count--;
                break;
            }

            if (c == 19) /* Swallow XOFF */
                continue;

            if (c == CTL_U) {
                if (count) {
                    cmd_erase_line(count);
                    *(str) = 0;
                    count = 0;
                }
                continue;
            }

            if (c == SEQ_ESCAPE_CHAR) {
                state = CMD_ESCAPE;
                continue;
            }

            /* Unix telnet sends:    <CR> <NUL>
            * Windows telnet sends: <CR> <LF>
            */
            if (*ignore_lf && (c == '\n' || c == 0x00)) {
                *ignore_lf = 0;
                continue;
            }

            if (c == 3) { /* Ctrl+C */
                return -1;
            }

            if (c == '\b' || c == 0x7F) {
                if (!count)
                    continue;

                putch('\b');
                putch(' ');
                putch('\b');
                count--;
                continue;
            }
            if (c != '\n' && c != '\r') {
                putch(c);
            }
            else {
                if (c == '\r') {
                    *ignore_lf = 1;
                    break;
                }

                if (c == '\n')
                    break;
            }
            str[count] = c;
            count++;
        }
    } while (1);

    str[count] = 0;
    return count;
}

static int8_t get_line(char *str, int8_t max, uint8_t *ignore_lf)
{
    uint8_t i;
    int8_t ret;
    int8_t tostore = -1;

    ret = get_string(str, max, ignore_lf);

    if (ret <= 0) {
        return ret;
    }
    
    if (_g_next_history >= CMD_MAX_HISTORY)
        _g_next_history = 0;
    else
        _g_max_history++;

    for (i = 0; i < CMD_MAX_HISTORY; i++)
    {
        if (!strcasecmp(_g_cmd_history[i], str))
        {
            tostore = i;
            break;
        }
    }

    if (tostore < 0)
    {
        // Don't have this command in history. Store it
        strcpy(_g_cmd_history[_g_next_history], str);
        _g_next_history++;
        _g_show_history = _g_next_history;
    }
    else
    {
        // Already have this command in history, set the 'up' arrow to retrieve it.
        tostore++;

        if (tostore == CMD_MAX_HISTORY)
            tostore = 0;

        _g_show_history = tostore;
    }

    printf("\r\n");

    return ret;
}

void load_configuration(sys_config_t *config)
{
    uint16_t config_size = sizeof(sys_config_t);
    if (config_size > 0x100)
    {
        printf("\r\nConfiguration size is too large. Currently %u bytes.", config_size);
        reset();
    }
    
    eeprom_read_data(0, (uint8_t *)config, sizeof(sys_config_t));

    if (config->magic != CONFIG_MAGIC)
    {
        printf("\r\nNo configuration found. Setting defaults\r\n");
        default_configuration(config);
        save_configuration(config);
    }
}

static void default_configuration(sys_config_t *config)
{
    uint8_t i;

    config->magic = CONFIG_MAGIC;
    config->expected_sensors = 0;
    config->resend_delay = 300;

    for (i = 0; i < MAX_SENSORS; i++)
        default_tempsensor(&config->temp_sensors[i]);

    for (i = 0; i < MAX_RECIPIENTS; i++)
        default_sms_recipient(&config->sms_recipients[i]);
}

static void save_configuration(sys_config_t *config)
{
    eeprom_write_data(0, (uint8_t *)config, sizeof(sys_config_t));
}
