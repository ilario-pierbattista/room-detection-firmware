/**
 * \file halTimer.c
 *
 * \brief ATmega128rfa1 timer implementation
 *
 * Copyright (C) 2012 Atmel Corporation. All rights reserved.
 *
 * \asf_license_start
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * 3. The name of Atmel may not be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * 4. This software may only be redistributed and used in connection with an
 *    Atmel microcontroller product.
 *
 * THIS SOFTWARE IS PROVIDED BY ATMEL "AS IS" AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT ARE
 * EXPRESSLY AND SPECIFICALLY DISCLAIMED. IN NO EVENT SHALL ATMEL BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * \asf_license_stop
 *
 * $Id: halTimer.c 5242 2012-09-10 18:37:05Z ataradov $
 *
 */

#include <stdbool.h>
#include "hal.h"
#include "halTemperature.h"
#include <avr/sleep.h>
#include <util/delay.h>

/*****************************************************************************
*****************************************************************************/
static volatile uint8_t halAdcFinished;
static int8_t halAdcOffset;

/*****************************************************************************
*****************************************************************************/
static inline int16_t HAL_AdcMeasure(void)
{
  set_sleep_mode(SLEEP_MODE_ADC);
  /* dummy cycle */
  halAdcFinished = false;
  do
  {
    sleep_mode();
    /* sleep, wake up by ADC IRQ */
    /* check here for ADC IRQ because sleep mode could wake up from * another source too */
  }
  while (false == halAdcFinished);
  /* set by ISR */
  return ADC;
}

/*****************************************************************************
*****************************************************************************/
float HAL_MeasureTemperature(void)
{
	int32_t val = 0;
	uint8_t numberOfReadings = 5;

	ADCSRA = (1 << ADEN) | (1 << ADPS2) | (1 << ADPS1); /* PS 64 */
	ADCSRB = (1 << MUX5);
	ADMUX = (1 << REFS1) | (1 << REFS0) | (1 << MUX3) | (1 << MUX0); /* reference: 1.6V, input Temp Sensor */
	
	_delay_us(HAL_TEMPERATURE_READING_DELAY); /* some time to settle */

	ADCSRA |= (1 << ADIF); /* clear flag */
	ADCSRA |= (1 << ADIE);

	/* dummy cycle after REF change (suggested by datasheet) */
	HAL_AdcMeasure();

	_delay_us(HAL_TEMPERATURE_READING_DELAY / 2); /* some time to settle */

	for(uint8_t i=0; i<numberOfReadings; i++){
		val += HAL_AdcMeasure() - halAdcOffset;
	}

	ADCSRA &= ~((1 << ADEN) | (1 << ADIE));

	return (1.13 * val / (float)numberOfReadings - 272.8);
}

/*****************************************************************************
*****************************************************************************/
int8_t HAL_MeasureAdcOffset(void)
{
	uint16_t val;

	ADCSRA = (1 << ADEN) | (1 << ADPS2) | (1 << ADPS1); /* PS 64 */
	ADCSRB = 0;
	ADMUX = (1 << REFS1) | (1 << REFS0) | (1 << MUX3); /* reference: 1.6V, differential ADC0-ADC0 10x */

	_delay_us(HAL_TEMPERATURE_READING_DELAY); /* some time to settle */

	ADCSRA |= (1 << ADIF); /* clear flag */
	ADCSRA |= (1 << ADIE);

	/* dummy cycle after REF change (suggested by datasheet) */
	HAL_AdcMeasure();

	_delay_us(HAL_TEMPERATURE_READING_DELAY / 2); /* some time to settle */

	val = HAL_AdcMeasure();

	ADCSRA &= ~((1 << ADEN) | (1 << ADIE));

  halAdcOffset = val;
	return (val);
}

/*****************************************************************************
*****************************************************************************/
ISR(ADC_vect, ISR_BLOCK)
{
	halAdcFinished = true;
}
