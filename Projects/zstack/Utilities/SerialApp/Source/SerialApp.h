/**************************************************************************************************
  Filename:       SerialApp.h
  Revised:        $Date: 2009-02-25 17:31:49 -0800 (Wed, 25 Feb 2009) $
  Revision:       $Revision: 19273 $

  Description:    This file contains the Serial Transfer Application definitions.


  Copyright 2004-2007 Texas Instruments Incorporated. All rights reserved.

  IMPORTANT: Your use of this Software is limited to those specific rights
  granted under the terms of a software license agreement between the user
  who downloaded the software, his/her employer (which must be your employer)
  and Texas Instruments Incorporated (the "License").  You may not use this
  Software unless you agree to abide by the terms of the License. The License
  limits your use, and you acknowledge, that the Software may not be modified,
  copied or distributed unless embedded on a Texas Instruments microcontroller
  or used solely and exclusively in conjunction with a Texas Instruments radio
  frequency transceiver, which is integrated into your product.  Other than for
  the foregoing purpose, you may not use, reproduce, copy, prepare derivative
  works of, modify, distribute, perform, display or sell this Software and/or
  its documentation for any purpose.

  YOU FURTHER ACKNOWLEDGE AND AGREE THAT THE SOFTWARE AND DOCUMENTATION ARE
  PROVIDED �AS IS?WITHOUT WARRANTY OF ANY KIND, EITHER EXPRESS OR IMPLIED, 
  INCLUDING WITHOUT LIMITATION, ANY WARRANTY OF MERCHANTABILITY, TITLE, 
  NON-INFRINGEMENT AND FITNESS FOR A PARTICULAR PURPOSE. IN NO EVENT SHALL
  TEXAS INSTRUMENTS OR ITS LICENSORS BE LIABLE OR OBLIGATED UNDER CONTRACT,
  NEGLIGENCE, STRICT LIABILITY, CONTRIBUTION, BREACH OF WARRANTY, OR OTHER
  LEGAL EQUITABLE THEORY ANY DIRECT OR INDIRECT DAMAGES OR EXPENSES
  INCLUDING BUT NOT LIMITED TO ANY INCIDENTAL, SPECIAL, INDIRECT, PUNITIVE
  OR CONSEQUENTIAL DAMAGES, LOST PROFITS OR LOST DATA, COST OF PROCUREMENT
  OF SUBSTITUTE GOODS, TECHNOLOGY, SERVICES, OR ANY CLAIMS BY THIRD PARTIES
  (INCLUDING BUT NOT LIMITED TO ANY DEFENSE THEREOF), OR OTHER SIMILAR COSTS.

  Should you have any questions regarding your right to use this Software,
  contact Texas Instruments Incorporated at www.TI.com. 
**************************************************************************************************/

#ifndef SERIALAPP_H
#define SERIALAPP_H

#ifdef __cplusplus
extern "C"
{
#endif

/*********************************************************************
 * INCLUDES
 */
#include "ZComDef.h"
#include "hal_adc.h"

/*********************************************************************
 * CONSTANTS
 */

// These constants are only for example and should be changed to the
// device's needs
#define SERIALAPP_ENDPOINT           11

#define SERIALAPP_PROFID             0x0F05
#define SERIALAPP_DEVICEID           0x0001
#define SERIALAPP_DEVICE_VERSION     0
#define SERIALAPP_FLAGS              0

#define SERIALAPP_MAX_CLUSTERS       4
#define SERIALAPP_CLUSTERID1         1
#define SERIALAPP_CLUSTERID2         2
#define SERIALAPP_CONNECTREQ_CLUSTER 3  
#define SERIALAPP_CONNECTRSP_CLUSTER 4    

#define SERIALAPP_SEND_EVT           0x0001
#define SERIALAPP_RESP_EVT           0x0002
#define SENSOR_DATA_EVT              0x0004
#define SERIALAPP_PROCESS_EVT        0x0008

// OTA Flow Control Delays
#define SERIALAPP_ACK_DELAY          1
#define SERIALAPP_NAK_DELAY          16

// OTA Flow Control Status
#define OTA_SUCCESS                  ZSuccess
#define OTA_DUP_MSG                 (ZSuccess+1)
#define OTA_SER_BUSY                (ZSuccess+2)

// Sensor Types
#define SENSOR_TYPE_PMS7003          0x01
#define SENSOR_TYPE_ADC              0x02
#define SENSOR_TYPE_MQ2              0x03
#define SENSOR_TYPE_FLAME            0x04

// Sensor Configuration
#define SENSOR_ADC_CHANNEL           HAL_ADC_CHANNEL_0
#define SENSOR_ADC_RESOLUTION        HAL_ADC_RESOLUTION_12
#define SENSOR_ADC_INTERVAL          5000

// MQ-2 Configuration
#define MQ2_ADC_CHANNEL              HAL_ADC_CHANNEL_5
#define MQ2_ADC_RESOLUTION           HAL_ADC_RESOLUTION_12

// Flame Sensor Configuration
#define FLAME_ADC_CHANNEL            HAL_ADC_CHANNEL_6
#define FLAME_ADC_RESOLUTION         HAL_ADC_RESOLUTION_12

// PMS7003 Configuration
#define PMS7003_HEADER               0x42
#define PMS7003_HEADER_SECOND        0x4D
#define PMS7003_FRAME_LENGTH         32

/*********************************************************************
 * MACROS
 */

/*********************************************************************
 * GLOBAL VARIABLES
 */
extern byte SerialApp_TaskID;

/*********************************************************************
 * FUNCTIONS
 */

/*
 * Task Initialization for the Serial Transfer Application
 */
extern void SerialApp_Init( byte task_id );

/*
 * Task Event Processor for the Serial Transfer Application
 */
extern UINT16 SerialApp_ProcessEvent( byte task_id, UINT16 events );

/*********************************************************************
*********************************************************************/

#ifdef __cplusplus
}
#endif

#endif /* SERIALAPP_H */
