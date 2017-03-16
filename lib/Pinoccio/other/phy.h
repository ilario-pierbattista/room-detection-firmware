#ifndef _TEST_H_
#define _TEST_H_

#ifdef __cplusplus
extern "C"{
#endif

#include <stdint.h>
#include <stdbool.h>
#include "sysConfig.h"
#include "atmega128rfa1.h"

/*****************************************************************************
*****************************************************************************/
#define PHY_RSSI_BASE_VAL                  (-90)

#define PHY_HAS_RANDOM_NUMBER_GENERATOR
#define PHY_HAS_AES_MODULE

/*****************************************************************************
*****************************************************************************/
typedef struct PHY_DataInd_t
{
  uint8_t    *data;
  uint8_t    size;
  uint8_t    lqi;
  int8_t     rssi;
} PHY_DataInd_t;

void PHY_Init(void);

#ifdef __cplusplus
} // extern "C"
#endif
#endif // _TEST_H_

