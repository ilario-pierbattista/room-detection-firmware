/**
 * \file template.c
 *
 * \brief Empty application template
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
 * $Id: template.c 5223 2012-09-10 16:47:17Z ataradov $
 *
 */

#include <avr/sleep.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "config.h"
#include "hal.h"
#include "phy.h"
#include "sys.h"
#include "nwk.h"
#include "halUart.h"
#include "sysTimer.h"

/*****************************************************************************
*****************************************************************************/
// Put your preprocessor definitions here

/*****************************************************************************
*****************************************************************************/
typedef enum AppState_t
{
  APP_STATE_INITIAL,
  APP_STATE_START_SEND,
  APP_STATE_GO_TO_SLEEP,
  APP_STATE_SLEEPING,
} AppState_t;

/*****************************************************************************
*****************************************************************************/
// Put your function prototypes here

/*****************************************************************************
*****************************************************************************/
static AppState_t appState = APP_STATE_INITIAL;
static SYS_Timer_t appSleepTimer;
static uint8_t sleepCtr;

/*****************************************************************************
*****************************************************************************/
static void appSleepTimerHandler(SYS_Timer_t *timer)
{
  appState = APP_STATE_START_SEND;
  (void)timer;
}

/*****************************************************************************
*****************************************************************************/
static void appInit(void)
{
  appSleepTimer.interval = APP_SLEEP_INTERVAL;
  appSleepTimer.mode = SYS_TIMER_INTERVAL_MODE;
  appSleepTimer.handler = appSleepTimerHandler;

  sleepCtr = 0;
  appState = APP_STATE_START_SEND;
}

/*****************************************************************************
*****************************************************************************/
void PHY_EdConf(int8_t ed)
{
  char hex[] = "0123456789abcdef";
  uint8_t v = ed - PHY_RSSI_BASE_VAL;
  HAL_UartWriteByte(hex[(v >> 4) & 0x0f]);
  HAL_UartWriteByte(hex[v & 0x0f]);
  HAL_UartWriteByte('\r');
  HAL_UartWriteByte('\n');
}

/*****************************************************************************
*****************************************************************************/
static void APP_TaskHandler(void)
{
  switch (appState)
  {
    case APP_STATE_INITIAL:
    {
      appInit();
    } break;

    case APP_STATE_START_SEND:
    {
      if (sleepCtr < 5) {
        HAL_UartWriteByte('R');
        HAL_UartWriteByte('a');
        HAL_UartWriteByte('d');
        HAL_UartWriteByte('i');
        HAL_UartWriteByte('o');
        HAL_UartWriteByte(' ');
        HAL_UartWriteByte('s');
        HAL_UartWriteByte('e');
        HAL_UartWriteByte('n');
        HAL_UartWriteByte('d');
        HAL_UartWriteByte('\r');
        HAL_UartWriteByte('\n');
      
        PHY_SetChannel(APP_CHANNEL);
        PHY_EdReq();
        appState = APP_STATE_GO_TO_SLEEP;
      } else {
        HAL_UartWriteByte('D');
        HAL_UartWriteByte('e');
        HAL_UartWriteByte('e');
        HAL_UartWriteByte('p');
        HAL_UartWriteByte(' ');
        HAL_UartWriteByte('s');
        HAL_UartWriteByte('l');
        HAL_UartWriteByte('e');
        HAL_UartWriteByte('e');
        HAL_UartWriteByte('p');
        HAL_UartWriteByte('\r');
        HAL_UartWriteByte('\n');
        
        do {
          set_sleep_mode(SLEEP_MODE_PWR_DOWN);
          sleep_mode();
        } while(0);
      }
    } break;

    case APP_STATE_GO_TO_SLEEP:
    {     
      if (!NWK_Busy())
      {
        HAL_UartWriteByte('R');
        HAL_UartWriteByte('a');
        HAL_UartWriteByte('d');
        HAL_UartWriteByte('i');
        HAL_UartWriteByte('o');
        HAL_UartWriteByte(' ');
        HAL_UartWriteByte('s');
        HAL_UartWriteByte('l');
        HAL_UartWriteByte('e');
        HAL_UartWriteByte('e');
        HAL_UartWriteByte('p');
        HAL_UartWriteByte('\r');
        HAL_UartWriteByte('\n');
        
        sleepCtr++;
        NWK_SleepReq();
        appState = APP_STATE_SLEEPING;
      }
    } break;

    default:
      break;
  }
}

/*****************************************************************************
*****************************************************************************/
int main(void)
{
  SYS_Init();
  HAL_UartInit(115200);
  
  while (1)
  {
    SYS_TaskHandler();
    APP_TaskHandler();
  }
}
