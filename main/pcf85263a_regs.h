#pragma once
#include <stdint.h>

/* ==========================================================================
 * PCF85263A Register Map
 * Valid address range: 0x00 – 0x2F
 * Datasheet Rev. 5.3
 * ========================================================================== */


 #define PCF85263A_I2C_ADDR                  81

/* --------------------------------------------------------------------------
 * RTC time and date registers (RTCM = 0)
 * -------------------------------------------------------------------------- */
#define PCF85263A_REG_100TH_SECONDS        0x00
#define PCF85263A_REG_SECONDS              0x01
#define PCF85263A_REG_MINUTES              0x02
#define PCF85263A_REG_HOURS                0x03
#define PCF85263A_REG_DAYS                 0x04
#define PCF85263A_REG_WEEKDAYS             0x05
#define PCF85263A_REG_MONTHS               0x06
#define PCF85263A_REG_YEARS                0x07

/* --------------------------------------------------------------------------
 * Alarm 1 registers (RTC mode)
 * -------------------------------------------------------------------------- */
#define PCF85263A_REG_SECOND_ALARM1         0x08
#define PCF85263A_REG_MINUTE_ALARM1         0x09
#define PCF85263A_REG_HOUR_ALARM1           0x0A
#define PCF85263A_REG_DAY_ALARM1            0x0B
#define PCF85263A_REG_MONTH_ALARM1          0x0C

/* --------------------------------------------------------------------------
 * Alarm 2 registers (RTC mode)
 * -------------------------------------------------------------------------- */
#define PCF85263A_REG_MINUTE_ALARM2         0x0D
#define PCF85263A_REG_HOUR_ALARM2           0x0E
#define PCF85263A_REG_WEEKDAY_ALARM2        0x0F

/* --------------------------------------------------------------------------
 * Alarm enable register
 * -------------------------------------------------------------------------- */
#define PCF85263A_REG_ALARM_ENABLES         0x10

/* --------------------------------------------------------------------------
 * Timestamp registers (RTC mode)
 * -------------------------------------------------------------------------- */
#define PCF85263A_REG_TSR1_SECONDS          0x11
#define PCF85263A_REG_TSR1_MINUTES          0x12
#define PCF85263A_REG_TSR1_HOURS            0x13
#define PCF85263A_REG_TSR1_DAYS             0x14
#define PCF85263A_REG_TSR1_MONTHS           0x15
#define PCF85263A_REG_TSR1_YEARS            0x16

#define PCF85263A_REG_TSR2_SECONDS          0x17
#define PCF85263A_REG_TSR2_MINUTES          0x18
#define PCF85263A_REG_TSR2_HOURS            0x19
#define PCF85263A_REG_TSR2_DAYS             0x1A
#define PCF85263A_REG_TSR2_MONTHS           0x1B
#define PCF85263A_REG_TSR2_YEARS            0x1C

#define PCF85263A_REG_TSR3_SECONDS          0x1D
#define PCF85263A_REG_TSR3_MINUTES          0x1E
#define PCF85263A_REG_TSR3_HOURS            0x1F
#define PCF85263A_REG_TSR3_DAYS             0x20
#define PCF85263A_REG_TSR3_MONTHS           0x21
#define PCF85263A_REG_TSR3_YEARS            0x22

/* --------------------------------------------------------------------------
 * Timestamp mode control
 * -------------------------------------------------------------------------- */
#define PCF85263A_REG_TSR_MODE              0x23

/* --------------------------------------------------------------------------
 * Offset correction register
 * -------------------------------------------------------------------------- */
#define PCF85263A_REG_OFFSET                0x24

/* --------------------------------------------------------------------------
 * Control and function registers
 * -------------------------------------------------------------------------- */
#define PCF85263A_REG_OSCILLATOR            0x25
#define PCF85263A_REG_BATTERY_SWITCH        0x26
#define PCF85263A_REG_PIN_IO                0x27
#define PCF85263A_REG_FUNCTION              0x28

/* --------------------------------------------------------------------------
 * Interrupt enable registers
 * -------------------------------------------------------------------------- */
#define PCF85263A_REG_INTA_ENABLE            0x29
#define PCF85263A_REG_INTB_ENABLE            0x2A

/* --------------------------------------------------------------------------
 * Flags register
 * -------------------------------------------------------------------------- */
#define PCF85263A_REG_FLAGS                  0x2B

/* --------------------------------------------------------------------------
 * RAM register
 * -------------------------------------------------------------------------- */
#define PCF85263A_REG_RAM_BYTE               0x2C

/* --------------------------------------------------------------------------
 * WatchDog register
 * -------------------------------------------------------------------------- */
#define PCF85263A_REG_WATCHDOG               0x2D

/* --------------------------------------------------------------------------
 * Stop enable register
 * -------------------------------------------------------------------------- */
#define PCF85263A_REG_STOP_ENABLE            0x2E

/* --------------------------------------------------------------------------
 * Resets register
 * -------------------------------------------------------------------------- */
#define PCF85263A_REG_RESETS                 0x2F
