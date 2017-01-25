/* Copyright Statement:
 *
 * (C) 2005-2016  MediaTek Inc. All rights reserved.
 *
 * This software/firmware and related documentation ("MediaTek Software") are
 * protected under relevant copyright laws. The information contained herein
 * is confidential and proprietary to MediaTek Inc. ("MediaTek") and/or its licensors.
 * Without the prior written permission of MediaTek and/or its licensors,
 * any reproduction, modification, use or disclosure of MediaTek Software,
 * and information contained herein, in whole or in part, shall be strictly prohibited.
 * You may only use, reproduce, modify, or distribute (as applicable) MediaTek Software
 * if you have agreed to and been bound by the applicable license agreement with
 * MediaTek ("License Agreement") and been granted explicit permission to do so within
 * the License Agreement ("Permitted User").  If you are not a Permitted User,
 * please cease any access or use of MediaTek Software immediately.
 * BY OPENING THIS FILE, RECEIVER HEREBY UNEQUIVOCALLY ACKNOWLEDGES AND AGREES
 * THAT MEDIATEK SOFTWARE RECEIVED FROM MEDIATEK AND/OR ITS REPRESENTATIVES
 * ARE PROVIDED TO RECEIVER ON AN "AS-IS" BASIS ONLY. MEDIATEK EXPRESSLY DISCLAIMS ANY AND ALL
 * WARRANTIES, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE OR NONINFRINGEMENT.
 * NEITHER DOES MEDIATEK PROVIDE ANY WARRANTY WHATSOEVER WITH RESPECT TO THE
 * SOFTWARE OF ANY THIRD PARTY WHICH MAY BE USED BY, INCORPORATED IN, OR
 * SUPPLIED WITH MEDIATEK SOFTWARE, AND RECEIVER AGREES TO LOOK ONLY TO SUCH
 * THIRD PARTY FOR ANY WARRANTY CLAIM RELATING THERETO. RECEIVER EXPRESSLY ACKNOWLEDGES
 * THAT IT IS RECEIVER'S SOLE RESPONSIBILITY TO OBTAIN FROM ANY THIRD PARTY ALL PROPER LICENSES
 * CONTAINED IN MEDIATEK SOFTWARE. MEDIATEK SHALL ALSO NOT BE RESPONSIBLE FOR ANY MEDIATEK
 * SOFTWARE RELEASES MADE TO RECEIVER'S SPECIFICATION OR TO CONFORM TO A PARTICULAR
 * STANDARD OR OPEN FORUM. RECEIVER'S SOLE AND EXCLUSIVE REMEDY AND MEDIATEK'S ENTIRE AND
 * CUMULATIVE LIABILITY WITH RESPECT TO MEDIATEK SOFTWARE RELEASED HEREUNDER WILL BE,
 * AT MEDIATEK'S OPTION, TO REVISE OR REPLACE MEDIATEK SOFTWARE AT ISSUE,
 * OR REFUND ANY SOFTWARE LICENSE FEES OR SERVICE CHARGE PAID BY RECEIVER TO
 * MEDIATEK FOR SUCH MEDIATEK SOFTWARE AT ISSUE.
 */

#include <stdint.h>
#include <stdio.h>

#include "mt2523.h"
#include "system_mt2523.h"

#include "hal.h"
#include "syslog.h"
#include "sys_init.h"

#include "memory_map.h"
#include "bsp_gpio_ept_config.h"

/* target system frequency selection */
/* Set system clock to 26M Hz */
#ifdef MTK_SYSTEM_CLOCK_26M
static const uint32_t target_freq = 26000;
#else
/* Set system clock to 104M Hz */
#ifdef MTK_SYSTEM_CLOCK_104M
static const uint32_t target_freq = 104000;
#else
static const uint32_t target_freq = 208000;
#endif
#endif

/**
* @brief       This function is low-level libc implementation, which is used for printf family.
* @param[in]   ch: the character that will be put into uart port.
* @return      The character as input.
*/
/* Predefined MACRO of gcc */
#ifdef __GNUC__
int __io_putchar(int ch)
#else
int fputc(int ch, FILE *f)
#endif
{
    hal_uart_put_char(HAL_UART_0, ch);
    if (ch == '\n') {
        hal_uart_put_char(HAL_UART_0, '\r');
    }
    return ch;
}

/**
* @brief       This function is to config system clock.
* @return      None.
*/
static void SystemClock_Config(void)
{
    hal_clock_init();

    /* Initialize the Dynamic Voltage Frequency Switch(DVFS) and switch the target freqeuncy. */
    hal_dvfs_init();
    hal_dvfs_target_cpu_frequency(target_freq, HAL_DVFS_FREQ_RELATION_H);
}

static void cache_init(void)
{
    hal_cache_region_t region, region_number;

    /* Max region number is 16 */
    hal_cache_region_config_t region_cfg_tbl[] = {
        /* cacheable address, cacheable size(both MUST be 4k bytes aligned) */
#ifdef RTOS_UBIN_LENGTH
        {BL_BASE, BL_LENGTH + RTOS_LENGTH + RTOS_UBIN_LENGTH},
#else
        {BL_BASE, BL_LENGTH + RTOS_LENGTH},
#endif
        /* virtual memory */
        {VRAM_BASE, VRAM_LENGTH}
    };

    region_number = (hal_cache_region_t) (sizeof(region_cfg_tbl) / sizeof(region_cfg_tbl[0]));

    hal_cache_init();
    hal_cache_set_size(HAL_CACHE_SIZE_32KB);
    for (region = HAL_CACHE_REGION_0; region < region_number; region++) {
        hal_cache_region_config(region, &region_cfg_tbl[region]);
        hal_cache_region_enable(region);
    }
    for ( ; region < HAL_CACHE_REGION_MAX; region++) {
        hal_cache_region_disable(region);
    }
    hal_cache_enable();
}

uint32_t caculate_mpu_region_size( uint32_t region_size )
{
    uint32_t count;

    if (region_size < 32) {
        return 0;
    }
    for (count = 0; ((region_size  & 0x80000000) == 0); count++, region_size  <<= 1);
    return 30 - count;
}

static void mpu_init(void)
{
    hal_mpu_region_t region, region_number;
    hal_mpu_region_config_t region_config;
    typedef struct {
        uint32_t mpu_region_base_address;			/**< MPU region start address */
        uint32_t mpu_region_end_address;			/**< MPU region end address */
        hal_mpu_access_permission_t mpu_region_access_permission; /**< MPU region access permission */
        uint8_t mpu_subregion_mask; 			/**< MPU sub region mask*/
        bool mpu_xn;					/**< XN attribute of MPU, if set TRUE, execution of an instruction fetched from the corresponding region is not permitted */
    } mpu_region_information_t;

#if defined (__GNUC__) || defined (__CC_ARM)

    //RAM: VECTOR TABLE+RAM CODE+RO DATA
    extern uint32_t Image$$RAM_TEXT$$Base;
    extern uint32_t Image$$RAM_TEXT$$Limit;
    //TCM: TCM CODE+RO DATA
    extern uint32_t Image$$TCM$$RO$$Base;
    extern uint32_t Image$$TCM$$RO$$Limit;

    /* MAX region number is 8 */
    mpu_region_information_t region_information[] = {
        /* mpu_region_start_address, mpu_region_end_address, mpu_region_access_permission, mpu_subregion_mask, mpu_xn */
        {(uint32_t) &Image$$RAM_TEXT$$Base,  (uint32_t) &Image$$RAM_TEXT$$Limit, HAL_MPU_READONLY, 0x0, FALSE},//Vector table+RAM code+RAM rodata
        {(uint32_t) &Image$$RAM_TEXT$$Base + VRAM_BASE,  (uint32_t) &Image$$RAM_TEXT$$Limit + VRAM_BASE, HAL_MPU_NO_ACCESS, 0x0, TRUE},//Virtual memory
        {(uint32_t) &Image$$TCM$$RO$$Base, (uint32_t) &Image$$TCM$$RO$$Limit, HAL_MPU_READONLY, 0x0, FALSE}//TCM code+TCM rodata
    };

#elif defined (__ICCARM__)

#pragma section = ".intvec"
#pragma section = ".ram_rodata"
#pragma section = ".tcm_code"
#pragma section = ".tcm_rwdata"

    /* MAX region number is 8, please DO NOT modify memory attribute of this structure! */
    _Pragma("location=\".ram_rodata\"") static mpu_region_information_t region_information[] = {
        /* mpu_region_start_address, mpu_region_end_address, mpu_region_access_permission, mpu_subregion_mask, mpu_xn */
        {(uint32_t)__section_begin(".intvec"), (uint32_t)__section_end(".ram_rodata"), HAL_MPU_READONLY, 0x0, FALSE},//Vector table+RAM code+RAM rodata
        {(uint32_t)__section_begin(".intvec") + VRAM_BASE, (uint32_t)__section_end(".ram_rodata") + VRAM_BASE, HAL_MPU_NO_ACCESS, 0x0, TRUE}, //Virtual memory
        {(uint32_t)__section_begin(".tcm_code"), (uint32_t)__section_begin(".tcm_rwdata"), HAL_MPU_READONLY, 0x0, FALSE}//TCM code+TCM rodata
    };

#endif

    hal_mpu_config_t mpu_config = {
        /* PRIVDEFENA, HFNMIENA */
        TRUE, TRUE
    };

    region_number = (hal_mpu_region_t) (sizeof(region_information) / sizeof(region_information[0]));

    hal_mpu_init(&mpu_config);
    for (region = HAL_MPU_REGION_0; region < region_number; region++) {
        /* Updata region information to be configured */
        region_config.mpu_region_address = region_information[region].mpu_region_base_address;
        region_config.mpu_region_size = (hal_mpu_region_size_t) caculate_mpu_region_size(region_information[region].mpu_region_end_address - region_information[region].mpu_region_base_address);
        region_config.mpu_region_access_permission = region_information[region].mpu_region_access_permission;
        region_config.mpu_subregion_mask = region_information[region].mpu_subregion_mask;
        region_config.mpu_xn = region_information[region].mpu_xn;

        hal_mpu_region_configure(region, &region_config);
        hal_mpu_region_enable(region);
    }
    /* make sure unused regions are disabled */
    for ( ; region < HAL_MPU_REGION_MAX; region++) {
        hal_mpu_region_disable(region);
    }
    hal_mpu_enable();
}


/**
* @brief       This function is to setup system hardware, such as increase system clock, uart init etc.
* @return      None.
*/
static void prvSetupHardware(void)
{
    /* system HW init */
    cache_init();
    mpu_init();

    /* peripherals init */
    hal_flash_init();
    hal_nvic_init();
    /* bsp_ept_gpio_setting_init() under driver/board/mt25x3_hdk/ept will initialize the GPIO settings
     * generated by easy pinmux tool (ept). ept_*.c and ept*.h are the ept files and will be used by
     * bsp_ept_gpio_setting_init() for GPIO pinumux setup.
     */
    bsp_ept_gpio_setting_init();
    hal_rtc_init();
    hal_sleep_manager_init();
}

/**
* @brief       This function is to do system initialization, eg: system clock, hardware and logging.
* @return      None.
*/
void system_init(void)
{
    SystemClock_Config();
    SystemCoreClockUpdate();

    /* Please refer to system log dev guide for more detail about log initialization. */
    log_uart_init(HAL_UART_0);
    prvSetupHardware();
    log_init(NULL, NULL, NULL);
}
