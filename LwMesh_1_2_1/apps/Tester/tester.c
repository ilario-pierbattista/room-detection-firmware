/**
 * \file tester.c
 *
 * \brief Tester application
 *
 * Copyright (C) 2012-2014, Atmel Corporation. All rights reserved.
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
 * Modification and other use of this code is subject to Atmel's Limited
 * License Agreement (license.txt).
 *
 * $Id: tester.c 9267 2014-03-18 21:46:19Z ataradov $
 *
 */

/*- Includes ---------------------------------------------------------------*/
#include "config.h"
#include "hal.h"
#include "phy.h"
#include "sys.h"
#include "nwk.h"
#include "halBoard.h"
#include "halUart.h"
#include "tester.h"
#include "commands.h"
#include "filter.h"
#include "events.h"
#include "time.h"

/*- Types ------------------------------------------------------------------*/
typedef enum AppState_t
{
  APP_STATE_INITIAL,
  APP_STATE_IDLE,
} AppState_t;

typedef enum AppUartState_t
{
  APP_UART_STATE_IDLE,
  APP_UART_STATE_READ_SIZE_1,
  APP_UART_STATE_READ_SIZE_2,
  APP_UART_STATE_READ_DATA,
  APP_UART_STATE_READ_CRC_1,
  APP_UART_STATE_READ_CRC_2,
} AppUartState_t;

/*- Prototypes -------------------------------------------------------------*/

/*- Variables --------------------------------------------------------------*/
static AppState_t appState = APP_STATE_INITIAL;
static AppUartState_t appUartState = APP_UART_STATE_IDLE;
static uint8_t appUartBuffer[APP_UART_BUFFER_SIZE];
static uint16_t appUartBufferPtr;
static uint16_t appUartBufferCrc;
static uint16_t appUartSize;
static uint16_t appUartCrc;

/*- Implementations --------------------------------------------------------*/

/*************************************************************************//**
*****************************************************************************/
static uint16_t appCrcCcittUpdate(uint16_t crc, uint8_t byte)
{
  byte ^= (byte ^ (crc & 0xff)) << 4;
  crc = (((uint16_t)byte << 8) | ((crc >> 8) & 0xff)) ^
      (uint8_t)(byte >> 4) ^ ((uint16_t)byte << 3);
  return crc;
}

/*************************************************************************//**
*****************************************************************************/
void appUartSendCommand(uint8_t *buf, uint16_t size)
{
  uint16_t crc = 0xffff;

  HAL_UartWriteByte(APP_UART_START_SYMBOL);
  HAL_UartWriteByte(size & 0xff);
  HAL_UartWriteByte(size >> 8);

  for (uint16_t i = 0; i < size; i++)
  {
    HAL_UartWriteByte(buf[i]);
    crc = appCrcCcittUpdate(crc, buf[i]);
  }

  HAL_UartWriteByte(crc & 0xff);
  HAL_UartWriteByte(crc >> 8);
}

/*************************************************************************//**
*****************************************************************************/
static void appUartSendAck(AppStatus_t status)
{
  AppCommandAck_t ack;

  ack.id = APP_COMMAND_ACK;
  ack.status = status;
  ack.time = appTimeGet();

  appUartSendCommand((uint8_t *)&ack, sizeof(ack));
}

/*************************************************************************//**
*****************************************************************************/
static void appUartStateMachine(uint8_t byte)
{
  switch (appUartState)
  {
    case APP_UART_STATE_IDLE:
    {
      if (APP_UART_START_SYMBOL == byte)
        appUartState = APP_UART_STATE_READ_SIZE_1;
    } break;

    case APP_UART_STATE_READ_SIZE_1:
    {
      appUartSize = byte;
      appUartState = APP_UART_STATE_READ_SIZE_2;
    } break;

    case APP_UART_STATE_READ_SIZE_2:
    {
      appUartSize |= (uint16_t)byte << 8;
      appUartBufferPtr = 0;
      appUartBufferCrc = 0xffff;
      appUartState = APP_UART_STATE_READ_DATA;
    } break;

    case APP_UART_STATE_READ_DATA:
    {
      appUartBuffer[appUartBufferPtr++] = byte;
      appUartBufferCrc = appCrcCcittUpdate(appUartBufferCrc, byte);

      if (appUartBufferPtr == appUartSize)
        appUartState = APP_UART_STATE_READ_CRC_1;
    } break;

    case APP_UART_STATE_READ_CRC_1:
    {
      appUartCrc = byte;
      appUartState = APP_UART_STATE_READ_CRC_2;
    } break;

    case APP_UART_STATE_READ_CRC_2:
    {
      appUartCrc |= (uint16_t)byte << 8;

      if (appUartCrc == appUartBufferCrc)
        appUartSendAck(appCommandReceived(appUartBuffer, appUartSize));
      else
        appUartSendAck(APP_STATUS_INVALID_CRC);

      appUartState = APP_UART_STATE_IDLE;
    } break;
  }
}

/*************************************************************************//**
*****************************************************************************/
void HAL_UartBytesReceived(uint16_t bytes)
{
  for (uint16_t i = 0; i < bytes; i++)
  {
    uint8_t byte = HAL_UartReadByte();
    appUartStateMachine(byte);
  }
}

/*************************************************************************//**
*****************************************************************************/
static void appInit()
{
  HAL_BoardInit();
}

/*************************************************************************//**
*****************************************************************************/
static void APP_TaskHandler(void)
{
  switch (appState)
  {
    case APP_STATE_INITIAL:
    {
      appInit();
      appTimeInit();
      appCommandsInit();
    #ifdef NWK_ENABLE_ADDRESS_FILTER
      appFilterInit();
    #endif
    #ifdef APP_ENABLE_EVENTS_BUFFER
      appEventsInit();
    #endif
      appState = APP_STATE_IDLE;
    } break;

    case APP_STATE_IDLE:
    {
    } break;
  }
}

/*************************************************************************//**
*****************************************************************************/
int main(void)
{
  SYS_Init();
  HAL_UartInit(38400);

  while (1)
  {
    SYS_TaskHandler();
    HAL_UartTaskHandler();
    APP_TaskHandler();
  }
}
