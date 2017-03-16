/**
 * \file Ping.c
 *
 * \brief Peer2Peer application implementation
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
 * $Id: Peer2Peer.c 5245 2012-09-10 20:07:02Z ataradov $
 *
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "config.h"
#include "hal.h"
#include "phy.h"
#include "sys.h"
#include "nwk.h"
#include "halUart.h"
#include "halSleep.h"
#include "sysTimer.h"
#include "halTemperature.h"

/*****************************************************************************
*****************************************************************************/
#ifdef NWK_ENABLE_SECURITY
  #define APP_BUFFER_SIZE     NWK_MAX_SECURED_PAYLOAD_SIZE
#else
  #define APP_BUFFER_SIZE     NWK_MAX_PAYLOAD_SIZE
#endif

/*****************************************************************************
*****************************************************************************/
typedef enum AppState_t
{
  APP_STATE_INITIAL,
  APP_STATE_IDLE,
} AppState_t;

/*****************************************************************************
*****************************************************************************/
static void appSendData(void);

/*****************************************************************************
*****************************************************************************/
static AppState_t appState = APP_STATE_INITIAL;
static SYS_Timer_t appTimer;
static NWK_DataReq_t appDataReq;
static bool appDataReqBusy = false;
static uint8_t appUartBuffer[APP_BUFFER_SIZE];
static uint8_t appUartBufferPtr = 0;
static uint8_t appPingCounter = 0;
static bool receivedPacket = false;

/*****************************************************************************
*****************************************************************************/
static void appSendDataConf(NWK_DataReq_t *req)
{
  appDataReqBusy = false;
  appSendData();
  (void)req;
}

/*****************************************************************************
*****************************************************************************/
static void appSendData(void)
{
  if (appDataReqBusy)
    return;

  appDataReq.dstAddr = 1;
  appDataReq.dstEndpoint = APP_ENDPOINT;
  appDataReq.srcEndpoint = APP_ENDPOINT;
  appDataReq.options = NWK_OPT_ENABLE_SECURITY;
  appDataReq.data = &appPingCounter;
  appDataReq.size = sizeof(appPingCounter);
  appDataReq.confirm = appSendDataConf;
  NWK_DataReq(&appDataReq);

  appPingCounter++;
  appDataReqBusy = true;
}

/*****************************************************************************
*****************************************************************************/
/*
void PHY_EdConf(int8_t ed)
{
  appCurrentEd = ed;
}*/


/*****************************************************************************
*****************************************************************************/
void HAL_UartBytesReceived(uint16_t bytes)
{
  for (uint16_t i = 0; i < bytes; i++)
  {
    uint8_t byte = HAL_UartReadByte();

    if (appUartBufferPtr == sizeof(appUartBuffer))
      appSendData();

    if (appUartBufferPtr < sizeof(appUartBuffer))
      appUartBuffer[appUartBufferPtr++] = byte;
  }
}

/*****************************************************************************
*****************************************************************************/
static void appTimerHandler(SYS_Timer_t *timer)
{
  if (APP_ADDR == 0) {
    appSendData();
    (void)timer;
  }
  if (APP_ADDR == 1) {
    HAL_UartWriteByte('w');
    HAL_UartWriteByte('a');
    HAL_UartWriteByte('i');
    HAL_UartWriteByte('t');
    HAL_UartWriteByte('i');
    HAL_UartWriteByte('n');
    HAL_UartWriteByte('g');
    HAL_UartWriteByte('.');
    HAL_UartWriteByte('.');
    HAL_UartWriteByte('.');
    HAL_UartWriteByte('\r');
    HAL_UartWriteByte('\n');
  }
}

/*****************************************************************************
*****************************************************************************/
static bool appDataInd(NWK_DataInd_t *ind)
{
  uint8_t i;
  char hex[] = "0123456789abcdef";
  receivedPacket = true;

  //PHY_EdReq();

  HAL_UartWriteByte('l');
  HAL_UartWriteByte('q');
  HAL_UartWriteByte('i');
  HAL_UartWriteByte(':');
  HAL_UartWriteByte(' ');
  HAL_UartWriteByte(hex[(ind->lqi >> 4) & 0x0f]);
  HAL_UartWriteByte(hex[ind->lqi & 0x0f]);

  HAL_UartWriteByte(' ');
  HAL_UartWriteByte(' ');

  HAL_UartWriteByte('r');
  HAL_UartWriteByte('s');
  HAL_UartWriteByte('s');
  HAL_UartWriteByte('i');
  HAL_UartWriteByte(':');
  HAL_UartWriteByte(' ');
  HAL_UartWriteByte(hex[(ind->rssi >> 4) & 0x0f]);
  HAL_UartWriteByte(hex[ind->rssi & 0x0f]);

  HAL_UartWriteByte(' ');
  HAL_UartWriteByte(' ');

  HAL_UartWriteByte('d');
  HAL_UartWriteByte('a');
  HAL_UartWriteByte('t');
  HAL_UartWriteByte('a');
  HAL_UartWriteByte(':');
  HAL_UartWriteByte(' ');

  for (i = 0; i < ind->size; i++)
    HAL_UartWriteByte(ind->data[i]);

  HAL_UartWriteByte('\r');
  HAL_UartWriteByte('\n');
  HAL_UartWriteByte('\r');
  HAL_UartWriteByte('\n');
  return true;
}

/*****************************************************************************
*****************************************************************************/
static void appInit(void)
{
  NWK_SetAddr(APP_ADDR);
  NWK_SetPanId(APP_PANID);
  PHY_SetChannel(APP_CHANNEL);
  PHY_SetRxState(true);
  NWK_OpenEndpoint(APP_ENDPOINT, appDataInd);

  appTimer.interval = APP_FLUSH_TIMER_INTERVAL;
  appTimer.mode = SYS_TIMER_PERIODIC_MODE;
  appTimer.handler = appTimerHandler;
  SYS_TimerStart(&appTimer);
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
      appState = APP_STATE_IDLE;
    } break;

    case APP_STATE_IDLE:
      break;

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
    HAL_UartTaskHandler();
    APP_TaskHandler();
  }
}
