/*
 * File      : adc.c
 * This file is part of RT-Thread RTOS
 * COPYRIGHT (C) 2006 - 2017, RT-Thread Development Team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Change Logs:
 * Date           Author       Notes
 * 2017-12-04     Haley        the first version
 */

#include <rtdevice.h>
#include "am_mcu_apollo.h"
#include "board.h"

#ifdef RT_USING_ADC

/* sem define */
rt_sem_t adcsem = RT_NULL;

#define BATTERY_GPIO            29                        /* Battery */
#define BATTERY_ADC_PIN         AM_HAL_PIN_29_ADCSE1 
#define BATTERY_ADC_CHANNEL     AM_HAL_ADC_SLOT_CHSEL_SE1 /* BATTERY ADC采集通道 */
#define BATTERY_ADC_CHANNELNUM  1                         /* BATTERY ADC采集通道号 */

#define ADC_CTIMER_NUM          3                         /* ADC使用定时器 */

#define ADC_CHANNEL_NUM         1                         /* ADC采集通道个数 */
#define ADC_SAMPLE_NUM          8                         /* ADC采样个数, NE_OF_OUTPUT */

rt_uint8_t bat_adc_cnt = (ADC_CHANNEL_NUM + 1)*ADC_SAMPLE_NUM;
rt_int16_t am_adc_buffer_pool[64];

rt_uint8_t am_adc_data_get(rt_int16_t *buff, rt_uint16_t size)
{
    /* wait adc interrupt release sem forever */	
    rt_sem_take(adcsem, RT_WAITING_FOREVER);

    /* copy the data */
    rt_memcpy(buff, am_adc_buffer_pool, size*sizeof(rt_int16_t));

    return 0;
}

void am_adc_start(void)
{
    /* adcsem create */
    adcsem = rt_sem_create("adcsem", 0, RT_IPC_FLAG_FIFO);

    /* Start the ctimer */
    am_hal_ctimer_start(ADC_CTIMER_NUM, AM_HAL_CTIMER_TIMERA);

    /* Trigger the ADC once */
    am_hal_adc_trigger();
}

void am_adc_stop(void)
{
    /* Stop the ctimer */
    am_hal_ctimer_stop(ADC_CTIMER_NUM, AM_HAL_CTIMER_TIMERA);

    /* adcsem delete */
    rt_sem_delete(adcsem);
}

/**
 * @brief Interrupt handler for the ADC
 *
 * This function is Interrupt handler for the ADC
 *
 * @return None.
 */
void am_adc_isr(void)
{
    uint32_t ui32Status, ui32FifoData;

    /* Read the interrupt status */
    ui32Status = am_hal_adc_int_status_get(true);

    /* Clear the ADC interrupt */
    am_hal_adc_int_clear(ui32Status);

    /* If we got a FIFO 75% full (which should be our only ADC interrupt), go ahead and read the data */
    if (ui32Status & AM_HAL_ADC_INT_FIFOOVR1)
    {
        do
        {
            /* Read the value from the FIFO into the circular buffer */
            ui32FifoData = am_hal_adc_fifo_pop();

            if(AM_HAL_ADC_FIFO_SLOT(ui32FifoData) == BATTERY_ADC_CHANNELNUM)
                am_adc_buffer_pool[bat_adc_cnt++] = AM_HAL_ADC_FIFO_SAMPLE(ui32FifoData);

            if(bat_adc_cnt > (ADC_CHANNEL_NUM + 1)*ADC_SAMPLE_NUM - 1)
            {
                /* shift data */
                rt_memmove(am_adc_buffer_pool, am_adc_buffer_pool + ADC_CHANNEL_NUM*ADC_SAMPLE_NUM, ADC_CHANNEL_NUM*ADC_SAMPLE_NUM*sizeof(rt_int16_t));
                bat_adc_cnt = (ADC_CHANNEL_NUM + 1)*ADC_SAMPLE_NUM;

                /* release adcsem */
                rt_sem_release(adcsem);
            }
        } while (AM_HAL_ADC_FIFO_COUNT(ui32FifoData) > 0);
    }
}

static void timerA3_for_adc_init(void)
{
    /* Start a timer to trigger the ADC periodically (1 second) */
    am_hal_ctimer_config_single(ADC_CTIMER_NUM, AM_HAL_CTIMER_TIMERA,
                                   AM_HAL_CTIMER_XT_2_048KHZ |
                                   AM_HAL_CTIMER_FN_REPEAT |
                                   AM_HAL_CTIMER_INT_ENABLE |
                                   AM_HAL_CTIMER_PIN_ENABLE);

    am_hal_ctimer_int_enable(AM_HAL_CTIMER_INT_TIMERA3);

    /* Set 512 sample rate */
    am_hal_ctimer_period_set(ADC_CTIMER_NUM, AM_HAL_CTIMER_TIMERA, 3, 1);

    /* Enable the timer A3 to trigger the ADC directly */
    am_hal_ctimer_adc_trigger_enable();

    /* Start the timer */
    //am_hal_ctimer_start(ADC_CTIMER_NUM, AM_HAL_CTIMER_TIMERA);
}

/**
 * @brief Initialize the ADC
 *
 * This function initialize the ADC
 *
 * @return None.
 */
int rt_hw_adc_init(void)
{
    am_hal_adc_config_t sADCConfig;

    /* timer for adc init*/
    timerA3_for_adc_init();

    /* Set a pin to act as our ADC input */
    am_hal_gpio_pin_config(BATTERY_GPIO, BATTERY_ADC_PIN);

    /* Enable interrupts */
    am_hal_interrupt_enable(AM_HAL_INTERRUPT_ADC);

    /* Enable the ADC power domain */
    am_hal_pwrctrl_periph_enable(AM_HAL_PWRCTRL_ADC);

    /* Set up the ADC configuration parameters. These settings are reasonable
       for accurate measurements at a low sample rate */
    sADCConfig.ui32Clock = AM_HAL_ADC_CLOCK_HFRC;
    sADCConfig.ui32TriggerConfig = AM_HAL_ADC_TRIGGER_SOFT;
    sADCConfig.ui32Reference = AM_HAL_ADC_REF_INT_2P0;
    sADCConfig.ui32ClockMode = AM_HAL_ADC_CK_LOW_POWER;
    sADCConfig.ui32PowerMode = AM_HAL_ADC_LPMODE_0;
    sADCConfig.ui32Repeat = AM_HAL_ADC_REPEAT;
    am_hal_adc_config(&sADCConfig);

    /* For this example, the samples will be coming in slowly. This means we
       can afford to wake up for every conversion */
    am_hal_adc_int_enable(AM_HAL_ADC_INT_FIFOOVR1);

    /* Set up an ADC slot */
    am_hal_adc_slot_config(BATTERY_ADC_CHANNELNUM, AM_HAL_ADC_SLOT_AVG_1 |
                              AM_HAL_ADC_SLOT_14BIT |
                              BATTERY_ADC_CHANNEL |
                              AM_HAL_ADC_SLOT_ENABLE);

    /* Enable the ADC */
    am_hal_adc_enable();

    rt_kprintf("adc_init!\n");

    return 0;
}
#ifdef RT_USING_COMPONENTS_INIT
INIT_BOARD_EXPORT(rt_hw_adc_init);
#endif

#endif
/*@}*/
