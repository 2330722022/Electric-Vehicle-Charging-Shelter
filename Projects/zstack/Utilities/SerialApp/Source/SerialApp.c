/*********************************************************************
 * INCLUDES
 */

#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include "AF.h"
#include "OnBoard.h"
#include "OSAL_Tasks.h"
#include "SerialApp.h"
#include "ZDApp.h"
#include "ZDObject.h"
#include "ZDProfile.h"

#include "hal_drivers.h"
#include "hal_key.h"
#include "hal_adc.h"
#if defined ( LCD_SUPPORTED )
  #include "hal_lcd.h"
#endif
#include "hal_led.h"
#include "hal_uart.h"

#include "DebugUtils.h"
#include "SensorConfig.h"

/*********************************************************************
 * MACROS
 */

/*********************************************************************
 * CONSTANTS
 */

#if !defined( SERIAL_APP_PORT )
#define SERIAL_APP_PORT  0
#endif

#if !defined( SERIAL_APP_BAUD )
  //#define SERIAL_APP_BAUD  HAL_UART_BR_38400
  #define SERIAL_APP_BAUD  HAL_UART_BR_9600
#endif

// When the Rx buf space is less than this threshold, invoke the Rx callback.
#if !defined( SERIAL_APP_THRESH )
#define SERIAL_APP_THRESH  64
#endif

#if !defined( SERIAL_APP_RX_SZ )
#define SERIAL_APP_RX_SZ  128
#endif

#if !defined( SERIAL_APP_TX_SZ )
#define SERIAL_APP_TX_SZ  128
#endif

// Millisecs of idle time after a byte is received before invoking Rx callback.
#if !defined( SERIAL_APP_IDLE )
#define SERIAL_APP_IDLE  6
#endif

// Loopback Rx bytes to Tx for throughput testing.
#if !defined( SERIAL_APP_LOOPBACK )
#define SERIAL_APP_LOOPBACK  FALSE
#endif

// This is the max byte count per OTA message.
#if !defined( SERIAL_APP_TX_MAX )
#define SERIAL_APP_TX_MAX  80
#endif

#define SERIAL_APP_RSP_CNT  4

// Sensor Configuration
#define SENSOR_ADC_CHANNEL      HAL_ADC_CHANNEL_0
#define SENSOR_ADC_RESOLUTION    HAL_ADC_RESOLUTION_12
#define SENSOR_ADC_INTERVAL      5000
#define SENSOR_DATA_EVT          0x0004

// MQ-2 Configuration
#define MQ2_ADC_CHANNEL          HAL_ADC_CHANNEL_5
#define MQ2_ADC_RESOLUTION       HAL_ADC_RESOLUTION_12

// Flame Sensor Configuration
#define FLAME_ADC_CHANNEL        HAL_ADC_CHANNEL_6
#define FLAME_ADC_RESOLUTION     HAL_ADC_RESOLUTION_12

// PMS7003 Configuration
#define PMS7003_HEADER           0x42
#define PMS7003_HEADER_SECOND    0x4D
#define PMS7003_FRAME_LENGTH     32

// ===== TERMINAL SENSOR CONFIG (toggle before burning) =====
//#define TERMINAL_PMS7003_ONLY    // Uncomment: PMS7003+ADC0 only (no MQ2/Flame)
                                     // Comment out: all sensors (PMS7003+ADC0+MQ2+Flame)

// Sensor Data Format
#define SENSOR_TYPE_PMS7003      0x01
#define SENSOR_TYPE_ADC          0x02
#define SENSOR_TYPE_MQ2          0x03
#define SENSOR_TYPE_FLAME        0x04
#define SENSOR_TYPE_COMBINED     0x05

// This list should be filled with Application specific Cluster IDs.
const cId_t SerialApp_ClusterList[SERIALAPP_MAX_CLUSTERS] =
{
  SERIALAPP_CLUSTERID1,
  SERIALAPP_CLUSTERID2,
  SERIALAPP_CONNECTREQ_CLUSTER,            
  SERIALAPP_CONNECTRSP_CLUSTER             
};

const SimpleDescriptionFormat_t SerialApp_SimpleDesc =
{
  SERIALAPP_ENDPOINT,              //  int   Endpoint;
  SERIALAPP_PROFID,                //  uint16 AppProfId[2];
  SERIALAPP_DEVICEID,              //  uint16 AppDeviceId[2];
  SERIALAPP_DEVICE_VERSION,        //  int   AppDevVer:4;
  SERIALAPP_FLAGS,                 //  int   AppFlags:4;
  SERIALAPP_MAX_CLUSTERS,          //  byte  AppNumInClusters;
  (cId_t *)SerialApp_ClusterList,  //  byte *pAppInClusterList;
  SERIALAPP_MAX_CLUSTERS,          //  byte  AppNumOutClusters;
  (cId_t *)SerialApp_ClusterList   //  byte *pAppOutClusterList;
};

endPointDesc_t SerialApp_epDesc =
{
  SERIALAPP_ENDPOINT,
 &SerialApp_TaskID,
  (SimpleDescriptionFormat_t *)&SerialApp_SimpleDesc,
  noLatencyReqs
};

/*********************************************************************
 * TYPEDEFS
 */

/*********************************************************************
 * GLOBAL VARIABLES
 */
devStates_t SampleApp_NwkState;   
uint8 SerialApp_TaskID;           // Task ID for internal task/event processing.

/*********************************************************************
 * EXTERNAL VARIABLES
 */

/*********************************************************************
 * EXTERNAL FUNCTIONS
 */

/*********************************************************************
 * LOCAL VARIABLES
 */

static uint8 SerialApp_MsgID;

static afAddrType_t SerialApp_TxAddr;
static uint8 SerialApp_TxSeq;
static uint8 SerialApp_TxBuf[SERIAL_APP_TX_MAX+1];
static uint8 SerialApp_TxLen;

static afAddrType_t SerialApp_RxAddr;
static uint8 SerialApp_RxSeq;
static uint8 SerialApp_RspBuf[SERIAL_APP_RSP_CNT];

// Sensor Variables
static uint8 pms7003Buffer[64];
static uint8 pms7003Index = 0;
static uint8 pms7003State = 0;
static uint16 lastAdcValue = 0;
static uint16 mq2Value = 0;
static uint16 flameValue = 0;
static uint8 sensorDataBuffer[32];

// UART Variables
// Coordinator: Cached sensor values for unified output
static uint16 coAdc0Value = 0;
static uint16 coPm1Value = 0;
static uint16 coPm25Value = 0;
static uint16 coPm10Value = 0;
static uint16 coMq2Value = 0;
static uint16 coFlameValue = 0;

static uint8 uartTxInProgress = FALSE;

// Ring buffer for UART reception
#define UART_RING_BUFFER_SIZE 256
static uint8 uartRingBuffer[UART_RING_BUFFER_SIZE];
static uint16 uartRingBufferHead = 0;
static uint16 uartRingBufferTail = 0;

/*********************************************************************
 * LOCAL FUNCTIONS
 */

static void SerialApp_ProcessMSGCmd( afIncomingMSGPacket_t *pkt );
static void SerialApp_Send(void);
static void SerialApp_Resp(void);
static void SerialApp_CallBack(uint8 port, uint8 event); 
static void SerialApp_DeviceConnect(void);              
static void SerialApp_DeviceConnectRsp(uint8*);         
static void SerialApp_ConnectReqProcess(uint8*);

// Sensor Functions
static void SerialApp_ProcessPMS7003Data(void);
static void SerialApp_ReadADCSensor(void);
static void SerialApp_SendSensorData(uint8 sensorType, uint8 *data, uint8 len);
static void SerialApp_ProcessSensorEvent(void);

// UART Functions
static uint8 SerialApp_SafeUARTWrite(uint8 port, const uint8 *data, uint16 len);
static void SerialApp_ProcessUARTBuffer(void);

/*********************************************************************
 * @fn      SerialApp_Init
 *
 * @brief   This is called during OSAL tasks' initialization.
 *
 * @param   task_id - the Task ID assigned by OSAL.
 *
 * @return  none
 */
void SerialApp_Init( uint8 task_id )
{
  halUARTCfg_t uartConfig;

  SerialApp_TaskID = task_id;
  SerialApp_RxSeq = 0xC3;
  SampleApp_NwkState = DEV_INIT;       

  afRegister( (endPointDesc_t *)&SerialApp_epDesc );

  RegisterForKeys( task_id );

  uartConfig.configured           = TRUE;              // 2x30 don't care - see uart driver.
  uartConfig.baudRate             = SERIAL_APP_BAUD;   // 9600 baud
  uartConfig.flowControl          = FALSE;             // No flow control
  uartConfig.flowControlThreshold = SERIAL_APP_THRESH; // 2x30 don't care - see uart driver.
  uartConfig.rx.maxBufSize        = SERIAL_APP_RX_SZ;  // 2x30 don't care - see uart driver.
  uartConfig.tx.maxBufSize        = SERIAL_APP_TX_SZ;  // 2x30 don't care - see uart driver.
  uartConfig.idleTimeout          = SERIAL_APP_IDLE;   // 2x30 don't care - see uart driver.
  uartConfig.intEnable            = TRUE;              // 2x30 don't care - see uart driver.
  uartConfig.callBackFunc         = SerialApp_CallBack;
  HalUARTOpen (SERIAL_APP_PORT, &uartConfig);

  HalAdcInit();

#if defined ( LCD_SUPPORTED )
  HalLcdInit();
  HalLcdWriteString( "SerialApp", HAL_LCD_LINE_2 );
  HalLcdWriteString( "Initializing...", HAL_LCD_LINE_3 );
  
  // Keep LCD on by writing initial sensor data
  char initBuffer1[17] = "PM2.5:--";
  char initBuffer2[17] = "PM10:--";
  HalLcdWriteScreen(initBuffer1, initBuffer2);
#endif
  
  ZDO_RegisterForZDOMsg( SerialApp_TaskID, End_Device_Bind_rsp );
  ZDO_RegisterForZDOMsg( SerialApp_TaskID, Match_Desc_rsp );
  
  // Start UART processing timer (1000ms)
  osal_start_timerEx(SerialApp_TaskID, SERIALAPP_PROCESS_EVT, 1000);
  
  // Debug: Initialization complete
  (void)SerialApp_SafeUARTWrite(0, (uint8*)"[INIT] SerialApp started, waiting for network...\r\n", 47);
  
#if defined ( LCD_SUPPORTED )
  (void)SerialApp_SafeUARTWrite(0, (uint8*)"[INIT] LCD initialized and displaying\r\n", 38);
#endif
  
  osal_start_timerEx( SerialApp_TaskID, SENSOR_DATA_EVT, SENSOR_ADC_INTERVAL );
}

/*********************************************************************
 * @fn      SerialApp_ProcessEvent
 *
 * @brief   Generic Application Task event processor.
 *
 * @param   task_id  - The OSAL assigned task ID.
 * @param   events   - Bit map of events to process.
 *
 * @return  Event flags of all unprocessed events.
 */
UINT16 SerialApp_ProcessEvent( uint8 task_id, UINT16 events )
{
  (void)task_id;  // Intentionally unreferenced parameter
  
  if ( events & SYS_EVENT_MSG )
  {
    afIncomingMSGPacket_t *MSGpkt;

    while ( (MSGpkt = (afIncomingMSGPacket_t *)osal_msg_receive( SerialApp_TaskID )) )
    {
      switch ( MSGpkt->hdr.event )
      {
      case AF_INCOMING_MSG_CMD:
        SerialApp_ProcessMSGCmd( MSGpkt );
        break;
        
      case ZDO_STATE_CHANGE:
        SampleApp_NwkState = (devStates_t)(MSGpkt->hdr.status);
        
        if ( (SampleApp_NwkState == DEV_ZB_COORD)
            || (SampleApp_NwkState == DEV_ROUTER)
            || (SampleApp_NwkState == DEV_END_DEVICE) )
        {
            HalLedSet(HAL_LED_1, HAL_LED_MODE_ON);
            
            if (SampleApp_NwkState == DEV_ZB_COORD)
            {
              (void)SerialApp_SafeUARTWrite(0, (uint8*)"[COORD] Network established\r\n", 30);
            }
            else
            {
              uint16 selfAddr = NLME_GetShortAddr();
              char deviceBuffer[40];
              sprintf(deviceBuffer, "[END0x%04X] Network established\r\n", selfAddr);
              (void)SerialApp_SafeUARTWrite(0, (uint8*)deviceBuffer, strlen(deviceBuffer));
            }
            
            if(SampleApp_NwkState != DEV_ZB_COORD)
              SerialApp_DeviceConnect();              
        }
        else
        {
          HalLedSet(HAL_LED_1, HAL_LED_MODE_OFF);
        }
        break;

      default:
        break;
      }

      osal_msg_deallocate( (uint8 *)MSGpkt );
    }

    return ( events ^ SYS_EVENT_MSG );
  }

  if ( events & SERIALAPP_SEND_EVT )
  {
    SerialApp_Send();
    return ( events ^ SERIALAPP_SEND_EVT );
  }

  if ( events & SERIALAPP_RESP_EVT )
  {
    SerialApp_Resp();
    return ( events ^ SERIALAPP_RESP_EVT );
  }

  if ( events & SENSOR_DATA_EVT )
  {
    SerialApp_ProcessSensorEvent();
    return ( events ^ SENSOR_DATA_EVT );
  }

  if ( events & SERIALAPP_PROCESS_EVT )
  {
    SerialApp_ProcessUARTBuffer();
    // Restart timer for next processing
    osal_start_timerEx(SerialApp_TaskID, SERIALAPP_PROCESS_EVT, 1000);
    return ( events ^ SERIALAPP_PROCESS_EVT );
  }

  return ( 0 );  // Discard unknown events.
}

/*********************************************************************
 * @fn      SerialApp_ProcessMSGCmd
 *
 * @brief   Data message processor callback. This function processes
 *          any incoming data - probably from other devices. Based
 *          on the cluster ID, perform the intended action.
 *
 * @param   pkt - pointer to the incoming message packet
 *
 * @return  TRUE if the 'pkt' parameter is being used and will be freed later,
 *          FALSE otherwise.
 */
void SerialApp_ProcessMSGCmd( afIncomingMSGPacket_t *pkt )
{
  uint8 stat;
  uint8 seqnb;
  uint8 delay;

  switch ( pkt->clusterId )
  {
  // A message with a serial data block to be transmitted on the serial port.
  case SERIALAPP_CLUSTERID1:
    // Store the address for sending and retrying.
    osal_memcpy(&SerialApp_RxAddr, &(pkt->srcAddr), sizeof( afAddrType_t ));

    // Check if this is PMS7003 binary data (1 byte type + 32 bytes raw frame)
    if (pkt->cmd.DataLength == 33 && pkt->cmd.Data[0] == SENSOR_TYPE_PMS7003)
    {
      // This is PMS7003 binary data - parse it properly
      uint8 *pmsData = &pkt->cmd.Data[1]; // Skip type byte
      uint16 srcAddr = pkt->srcAddr.addr.shortAddr;
      
      // Verify PMS7003 header
      if (pmsData[0] == 0x42 && pmsData[1] == 0x4D)
      {
        // Calculate checksum (sum of first 30 bytes)
        uint16 calcChecksum = 0;
        for (uint8 i = 0; i < 30; i++)
        {
          calcChecksum += pmsData[i];
        }
        
        // Get received checksum (bytes 30 and 31) - PMS7003 uses little-endian for checksum!
        uint16 recvChecksum = ((uint16)pmsData[30] << 8) | pmsData[31];
        
        if (calcChecksum == recvChecksum)
        {
          // Valid PMS7003 frame - parse PM values
          coPm1Value = ((uint16)pmsData[4] << 8) | pmsData[5];
          coPm25Value = ((uint16)pmsData[6] << 8) | pmsData[7];
          coPm10Value = ((uint16)pmsData[8] << 8) | pmsData[9];
          
          // Print PMS7003 data from terminal with source address
          {
            char pmsBuffer[80];
            sprintf(pmsBuffer, "[COORD] RX [END0x%04X] PMS7003: PM1.0=%d PM2.5=%d PM10=%d\r\n", 
                    srcAddr, coPm1Value, coPm25Value, coPm10Value);
            (void)SerialApp_SafeUARTWrite(SERIAL_APP_PORT, (uint8*)pmsBuffer, strlen(pmsBuffer));
          }
        }
      }
    }
    // Check if this is combined sensor data (ADC0+MQ2+Flame in one message)
    else if (pkt->cmd.DataLength == 8 && pkt->cmd.Data[1] == SENSOR_TYPE_COMBINED)
    {
      uint16 srcAddr = pkt->srcAddr.addr.shortAddr;
      
      // Parse all values from combined message
      coAdc0Value = BUILD_UINT16(pkt->cmd.Data[2], pkt->cmd.Data[3]);
      coMq2Value = BUILD_UINT16(pkt->cmd.Data[4], pkt->cmd.Data[5]);
      coFlameValue = BUILD_UINT16(pkt->cmd.Data[6], pkt->cmd.Data[7]);
      
      // Print combined RX message
      {
        char combRxBuf[80];
        sprintf(combRxBuf, "[COORD] RX [END0x%04X] ADC0=%d MQ2=%d Flame=%d\r\n", 
                srcAddr, coAdc0Value, coMq2Value, coFlameValue);
        (void)SerialApp_SafeUARTWrite(SERIAL_APP_PORT, (uint8*)combRxBuf, strlen(combRxBuf));
      }
      
      // Print combined data line
      {
        char combBuf[60];
        sprintf(combBuf, "[END0x%04X] %u,%u,%u,%u,%u\r\n", 
                srcAddr, coPm1Value, coPm25Value, coPm10Value, coMq2Value, coFlameValue);
        (void)SerialApp_SafeUARTWrite(SERIAL_APP_PORT, (uint8*)combBuf, strlen(combBuf));
      }
    }
    // Check if this is individual sensor data (legacy support)
    else if (pkt->cmd.DataLength == 4 && 
             (pkt->cmd.Data[1] == SENSOR_TYPE_ADC || pkt->cmd.Data[1] == SENSOR_TYPE_MQ2 || pkt->cmd.Data[1] == SENSOR_TYPE_FLAME))
    {
      uint8 sensorType = pkt->cmd.Data[1];
      uint16 value = BUILD_UINT16(pkt->cmd.Data[2], pkt->cmd.Data[3]);
      uint16 srcAddr = pkt->srcAddr.addr.shortAddr;
      
      if (sensorType == SENSOR_TYPE_MQ2)
      {
        coMq2Value = value;
      }
      else if (sensorType == SENSOR_TYPE_FLAME)
      {
        coFlameValue = value;
      }
      else if (sensorType == SENSOR_TYPE_ADC)
      {
        coAdc0Value = value;
      }
    }
    else
    {
      // Normal serial data handling (text-based)
      seqnb = pkt->cmd.Data[0];

      // Keep message if not a repeat packet
      if ( (seqnb > SerialApp_RxSeq) ||                    // Normal
          ((seqnb < 0x80 ) && ( SerialApp_RxSeq > 0x80)) ) // Wrap-around
      {
          // Transmit the data on the serial port.
          if ( SerialApp_SafeUARTWrite( SERIAL_APP_PORT, pkt->cmd.Data+1, (pkt->cmd.DataLength-1) ) )
          {
            // Save for next incoming message
            SerialApp_RxSeq = seqnb;
            stat = OTA_SUCCESS;
          }
          else
          {
            stat = OTA_SER_BUSY;
          }
      }
      else
      {
          stat = OTA_DUP_MSG;
      }
    }

    // Select approproiate OTA flow-control delay.
    delay = (stat == OTA_SER_BUSY) ? SERIALAPP_NAK_DELAY : SERIALAPP_ACK_DELAY;

    // Build & send OTA response message.
    SerialApp_RspBuf[0] = stat;
    SerialApp_RspBuf[1] = seqnb;
    SerialApp_RspBuf[2] = LO_UINT16( delay );
    SerialApp_RspBuf[3] = HI_UINT16( delay );
    osal_set_event( SerialApp_TaskID, SERIALAPP_RESP_EVT );
    osal_stop_timerEx(SerialApp_TaskID, SERIALAPP_RESP_EVT);
    break;

  // A response to a received serial data block.   // �ӵ���Ӧ��Ϣ
  case SERIALAPP_CLUSTERID2:
    if ((pkt->cmd.Data[1] == SerialApp_TxSeq) &&
       ((pkt->cmd.Data[0] == OTA_SUCCESS) || (pkt->cmd.Data[0] == OTA_DUP_MSG)))
    {
      SerialApp_TxLen = 0;
      osal_stop_timerEx(SerialApp_TaskID, SERIALAPP_SEND_EVT);
    }
    else
    {
      // Re-start timeout according to delay sent from other device.
      delay = BUILD_UINT16( pkt->cmd.Data[2], pkt->cmd.Data[3] );
      osal_start_timerEx( SerialApp_TaskID, SERIALAPP_SEND_EVT, delay );
    }
    break;

    case SERIALAPP_CONNECTREQ_CLUSTER:
      SerialApp_ConnectReqProcess((uint8*)pkt->cmd.Data);
      
    case SERIALAPP_CONNECTRSP_CLUSTER:
      SerialApp_DeviceConnectRsp((uint8*)pkt->cmd.Data);
      
    default:
      break;
  }
}

/*********************************************************************
 * @fn      SerialApp_Send
 *
 * @brief   Send data OTA.
 *
 * @param   none
 *
 * @return  none
 */
static void SerialApp_Send(void)
{
#if SERIAL_APP_LOOPBACK
    if (SerialApp_TxLen < SERIAL_APP_TX_MAX)
    {
        SerialApp_TxLen += HalUARTRead(SERIAL_APP_PORT, SerialApp_TxBuf+SerialApp_TxLen+1,
                                                      SERIAL_APP_TX_MAX-SerialApp_TxLen);
    }
  
    if (SerialApp_TxLen)
    {
      (void)SerialApp_TxAddr;
      if (SerialApp_SafeUARTWrite(SERIAL_APP_PORT, SerialApp_TxBuf+1, SerialApp_TxLen))
      {
        SerialApp_TxLen = 0;
      }
      else
      {
        osal_set_event(SerialApp_TaskID, SERIALAPP_SEND_EVT);
      }
    }
#else
    // Data is now processed in SerialApp_ProcessUARTBuffer
    // This function is only used for re-sending failed transmissions
    if (SerialApp_TxLen)
    {
      if (afStatus_SUCCESS != AF_DataRequest(&SerialApp_TxAddr,
                                             (endPointDesc_t *)&SerialApp_epDesc,
                                              SERIALAPP_CLUSTERID1,
                                              SerialApp_TxLen+1, SerialApp_TxBuf,
                                              &SerialApp_MsgID, 0, AF_DEFAULT_RADIUS))
      {
        osal_set_event(SerialApp_TaskID, SERIALAPP_SEND_EVT);
      }
      else
      {
        SerialApp_TxLen = 0; // Reset after successful send
      }
    }
#endif
}

/*********************************************************************
 * @fn      SerialApp_Resp
 *
 * @brief   Send data OTA.
 *
 * @param   none
 *
 * @return  none
 */
static void SerialApp_Resp(void)
{
  if (afStatus_SUCCESS != AF_DataRequest(&SerialApp_RxAddr,
                                         (endPointDesc_t *)&SerialApp_epDesc,
                                          SERIALAPP_CLUSTERID2,
                                          SERIAL_APP_RSP_CNT, SerialApp_RspBuf,
                                         &SerialApp_MsgID, 0, AF_DEFAULT_RADIUS))
  {
    osal_set_event(SerialApp_TaskID, SERIALAPP_RESP_EVT);
  }
}

/*********************************************************************
 * @fn      SerialApp_CallBack
 *
 * @brief   Send data OTA.
 *
 * @param   port - UART port.
 * @param   event - the UART port event flag.
 *
 * @return  none
 */
static void SerialApp_CallBack(uint8 port, uint8 event)
{
  (void)port;

  if (event & (HAL_UART_RX_FULL | HAL_UART_RX_ABOUT_FULL | HAL_UART_RX_TIMEOUT))
  {
    // Read data into ring buffer instead of processing directly
    uint8 data[SERIAL_APP_RX_SZ];
    uint16 len = HalUARTRead(SERIAL_APP_PORT, data, SERIAL_APP_RX_SZ);
    
    if (len > 0)
    {
      // Add data to ring buffer - PMS7003 uses binary protocol, don't filter any bytes
      for (uint16 i = 0; i < len; i++)
      {
        uint16 nextHead = (uartRingBufferHead + 1) % UART_RING_BUFFER_SIZE;
        if (nextHead != uartRingBufferTail)
        {
          uartRingBuffer[uartRingBufferHead] = data[i];
          uartRingBufferHead = nextHead;
        }
      }
    }
  }
}

/*********************************************************************
*********************************************************************/
void  SerialApp_DeviceConnect()              
{
#if ZDO_COORDINATOR
  
#else
  
  uint16 nwkAddr;
  uint16 parentNwkAddr;
  char buff[40] = {0};
  
  HalLedBlink( HAL_LED_2, 3, 50, (1000 / 4) );
  
  nwkAddr = NLME_GetShortAddr();
  parentNwkAddr = NLME_GetCoordShortAddr();
  sprintf(buff, "[END0x%04X] Connected, parent:0x%04X\r\n", nwkAddr, parentNwkAddr);
  HalUARTWrite ( 0, (uint8*)buff, strlen(buff));
  
  SerialApp_TxAddr.addrMode = (afAddrMode_t)Addr16Bit;
  SerialApp_TxAddr.endPoint = SERIALAPP_ENDPOINT;
  SerialApp_TxAddr.addr.shortAddr = parentNwkAddr;
  
  buff[0] = HI_UINT16( nwkAddr );
  buff[1] = LO_UINT16( nwkAddr );
  
  if ( AF_DataRequest( &SerialApp_TxAddr, &SerialApp_epDesc,
                       SERIALAPP_CONNECTREQ_CLUSTER,
                       2,
                       (uint8*)buff,
                       &SerialApp_MsgID, 
                       0, 
                       AF_DEFAULT_RADIUS ) == afStatus_SUCCESS )
  {
  }
  else
  {
    // Error occurred in request to send.
  }
  
#endif    //ZDO_COORDINATOR
}

void SerialApp_DeviceConnectRsp(uint8 *buf)
{
#if ZDO_COORDINATOR
  
#else
  uint16 selfAddr = NLME_GetShortAddr();
  SerialApp_TxAddr.addrMode = (afAddrMode_t)Addr16Bit;
  SerialApp_TxAddr.endPoint = SERIALAPP_ENDPOINT;
  SerialApp_TxAddr.addr.shortAddr = BUILD_UINT16(buf[1], buf[0]);
  
  HalLedSet(HAL_LED_2, HAL_LED_MODE_ON);
  {
    char connBuf[40];
    sprintf(connBuf, "[END0x%04X] Connect confirm success\r\n", selfAddr);
    (void)SerialApp_SafeUARTWrite(0, (uint8*)connBuf, strlen(connBuf));
  }
#endif
}

void SerialApp_ConnectReqProcess(uint8 *buf)
{
  uint16 nwkAddr;
  uint16 childAddr;
  char buff[40] = {0};
  
  SerialApp_TxAddr.addrMode = (afAddrMode_t)Addr16Bit;
  SerialApp_TxAddr.endPoint = SERIALAPP_ENDPOINT;
  childAddr = BUILD_UINT16(buf[1], buf[0]);
  SerialApp_TxAddr.addr.shortAddr = childAddr;
  nwkAddr = NLME_GetShortAddr();
  
  sprintf(buff, "[COORD] END[0x%04X] connect request\r\n", childAddr);
  (void)SerialApp_SafeUARTWrite(0, (uint8*)buff, strlen(buff));
  
  buff[0] = HI_UINT16( nwkAddr );
  buff[1] = LO_UINT16( nwkAddr );
  
  if ( AF_DataRequest( &SerialApp_TxAddr, &SerialApp_epDesc,
                       SERIALAPP_CONNECTRSP_CLUSTER,
                       2,
                       (uint8*)buff,
                       &SerialApp_MsgID, 
                       0, 
                       AF_DEFAULT_RADIUS ) == afStatus_SUCCESS )
  {
  }
  else
  {
    // Error occurred in request to send.
  }
  
  HalLedSet(HAL_LED_2, HAL_LED_MODE_ON);
  {
    char succBuf[40];
    sprintf(succBuf, "[COORD] END[0x%04X] connect success\r\n", childAddr);
    (void)SerialApp_SafeUARTWrite(0, (uint8*)succBuf, strlen(succBuf));
  }
}

static void SerialApp_ReadADCSensor(void)
{
  lastAdcValue = HalAdcRead(SENSOR_ADC_CHANNEL, SENSOR_ADC_RESOLUTION);
  
  char buffer[80];
  uint8 isCoordinator = (SampleApp_NwkState == DEV_ZB_COORD);
  
  if (isCoordinator)
  {
    // Coordinator only reads ADC0, MQ2/Flame are on end device
    float voltage = (lastAdcValue * 3.3) / 4095.0;
    sprintf(buffer, "[COORD] ADC: ADC0=%d(%.2fV)\r\n", lastAdcValue, voltage);
    (void)SerialApp_SafeUARTWrite(0, (uint8*)buffer, strlen(buffer));
  }
  else
  {
    uint16 selfAddr = NLME_GetShortAddr();
    
#ifdef TERMINAL_PMS7003_ONLY
    // PMS7003-only: only ADC0 available
    float voltage = (lastAdcValue * 3.3) / 4095.0;
    sprintf(buffer, "[END0x%04X] ADC: ADC0=%d(%.2fV)\r\n", 
            selfAddr, lastAdcValue, voltage);
    (void)SerialApp_SafeUARTWrite(0, (uint8*)buffer, strlen(buffer));
    
    // Send ADC0 only
    sensorDataBuffer[0] = SENSOR_TYPE_ADC;
    sensorDataBuffer[1] = LO_UINT16(lastAdcValue);
    sensorDataBuffer[2] = HI_UINT16(lastAdcValue);
    SerialApp_SendSensorData(SENSOR_TYPE_ADC, sensorDataBuffer, 3);
#else
    // Full sensors: read MQ2 + Flame
    mq2Value = HalAdcRead(MQ2_ADC_CHANNEL, MQ2_ADC_RESOLUTION);
    flameValue = HalAdcRead(FLAME_ADC_CHANNEL, FLAME_ADC_RESOLUTION);
    
    float voltage = (lastAdcValue * 3.3) / 4095.0;
    float mq2Voltage = (mq2Value * 3.3) / 4095.0;
    float flameVoltage = (flameValue * 3.3) / 4095.0;
    
    sprintf(buffer, "[END0x%04X] ADC: ADC0=%d(%.2fV) MQ2=%d(%.2fV) Flame=%d(%.2fV)\r\n", 
            selfAddr, lastAdcValue, voltage, mq2Value, mq2Voltage, flameValue, flameVoltage);
    (void)SerialApp_SafeUARTWrite(0, (uint8*)buffer, strlen(buffer));
    
#if defined ( LCD_SUPPORTED )
    {
      char lcdBuffer1[17];
      char lcdBuffer2[17];
      sprintf(lcdBuffer1, "MQ2:%d", mq2Value);
      sprintf(lcdBuffer2, "Flame:%d", flameValue);
      HalLcdWriteScreen(lcdBuffer1, lcdBuffer2);
    }
#endif
    
    // Send combined sensor data (ADC0+MQ2+Flame) in one message
    sensorDataBuffer[0] = SENSOR_TYPE_COMBINED;
    sensorDataBuffer[1] = LO_UINT16(lastAdcValue);
    sensorDataBuffer[2] = HI_UINT16(lastAdcValue);
    sensorDataBuffer[3] = LO_UINT16(mq2Value);
    sensorDataBuffer[4] = HI_UINT16(mq2Value);
    sensorDataBuffer[5] = LO_UINT16(flameValue);
    sensorDataBuffer[6] = HI_UINT16(flameValue);
    SerialApp_SendSensorData(SENSOR_TYPE_COMBINED, sensorDataBuffer, 7);
#endif
  }
}

static void SerialApp_SendSensorData(uint8 sensorType, uint8 *data, uint8 len)
{
  if (SampleApp_NwkState == DEV_ZB_COORD || 
      SampleApp_NwkState == DEV_ROUTER || 
      SampleApp_NwkState == DEV_END_DEVICE)
  {
    // For PMS7003, send the complete 32-byte raw frame with type prefix
    if (sensorType == SENSOR_TYPE_PMS7003)
    {
      uint8 sendBuf[33]; // 1 byte type + 32 bytes raw frame
      sendBuf[0] = sensorType;
      osal_memcpy(&sendBuf[1], data, 32);
      
      // Send with fixed length of 33 bytes - critical for binary data!
      AF_DataRequest(&SerialApp_TxAddr,
                     (endPointDesc_t *)&SerialApp_epDesc,
                     SERIALAPP_CLUSTERID1,
                     33, sendBuf,
                     &SerialApp_MsgID, 0, AF_DEFAULT_RADIUS);
    }
    else
    {
      // For other sensors or normal data
      uint8 sendBuf[SERIAL_APP_TX_MAX];
      uint8 sendLen = len;
      
      if (sendLen > (SERIAL_APP_TX_MAX - 1))
      {
        sendLen = SERIAL_APP_TX_MAX - 1;
      }
      
      sendBuf[0] = ++SerialApp_TxSeq;
      osal_memcpy(sendBuf + 1, data, sendLen);
      
      AF_DataRequest(&SerialApp_TxAddr,
                     (endPointDesc_t *)&SerialApp_epDesc,
                     SERIALAPP_CLUSTERID1,
                     sendLen + 1, sendBuf,
                     &SerialApp_MsgID, 0, AF_DEFAULT_RADIUS);
    }
  }
}

static void SerialApp_ProcessSensorEvent(void)
{
  SerialApp_ReadADCSensor();
  osal_start_timerEx(SerialApp_TaskID, SENSOR_DATA_EVT, SENSOR_ADC_INTERVAL);
}

// Empty implementations for linker errors
void SerialApp_DebugMessage(const char *message)
{
}

void SerialApp_DebugSensorConfig(void)
{
}

void SerialApp_DebugNetworkStatus(void)
{
}

// Safe UART write function to prevent interrupt recursion
static uint8 SerialApp_SafeUARTWrite(uint8 port, const uint8 *data, uint16 len)
{
  if (data == NULL || len == 0)
  {
    return FALSE;
  }
  
  // Disable UART interrupts temporarily
  uartTxInProgress = TRUE;
  
  // Write data
  uint8 result = HalUARTWrite(port, (uint8*)data, len);
  
  // Re-enable UART interrupts
  uartTxInProgress = FALSE;
  
  return result;
}

// Process UART buffer from ring buffer
static void SerialApp_ProcessUARTBuffer(void)
{
  // Process PMS7003 data only - do NOT forward remaining bytes as regular serial data
  // to avoid flooding the coordinator with binary garbage
  SerialApp_ProcessPMS7003Data();
}

// Process PMS7003 data from ring buffer
static void SerialApp_ProcessPMS7003Data(void)
{
  // Check if there's data in the ring buffer
  uint16 available = (uartRingBufferHead - uartRingBufferTail + UART_RING_BUFFER_SIZE) % UART_RING_BUFFER_SIZE;
  
  while (available > 0)
  {
    uint8 data = uartRingBuffer[uartRingBufferTail];
    
    switch (pms7003State)
    {
      case 0: // Waiting for header
        if (data == PMS7003_HEADER)
        {
          pms7003State = 1;
          pms7003Buffer[0] = data;
          pms7003Index = 1;
          
          // Move to next byte
          uartRingBufferTail = (uartRingBufferTail + 1) % UART_RING_BUFFER_SIZE;
          available = (uartRingBufferHead - uartRingBufferTail + UART_RING_BUFFER_SIZE) % UART_RING_BUFFER_SIZE;
        }
        else
        {
          // Skip non-header byte
          uartRingBufferTail = (uartRingBufferTail + 1) % UART_RING_BUFFER_SIZE;
          available = (uartRingBufferHead - uartRingBufferTail + UART_RING_BUFFER_SIZE) % UART_RING_BUFFER_SIZE;
        }
        break;
        
      case 1: // Waiting for second header byte
        if (data == PMS7003_HEADER_SECOND)
        {
          pms7003State = 2;
          pms7003Buffer[pms7003Index++] = data;
          
          // Move to next byte
          uartRingBufferTail = (uartRingBufferTail + 1) % UART_RING_BUFFER_SIZE;
          available = (uartRingBufferHead - uartRingBufferTail + UART_RING_BUFFER_SIZE) % UART_RING_BUFFER_SIZE;
        }
        else
        {
          // Invalid second header, reset
          pms7003State = 0;
          pms7003Index = 0;
          
          // Move to next byte
          uartRingBufferTail = (uartRingBufferTail + 1) % UART_RING_BUFFER_SIZE;
          available = (uartRingBufferHead - uartRingBufferTail + UART_RING_BUFFER_SIZE) % UART_RING_BUFFER_SIZE;
        }
        break;
        
      case 2: // Receiving data
        // Check if we got a new frame header (frame synchronization)
        if (data == PMS7003_HEADER && pms7003Index < 32)
        {
          // New frame starting, reset and start new frame
          pms7003State = 1;
          pms7003Buffer[0] = data;
          pms7003Index = 1;
          
          // Move to next byte
          uartRingBufferTail = (uartRingBufferTail + 1) % UART_RING_BUFFER_SIZE;
          available = (uartRingBufferHead - uartRingBufferTail + UART_RING_BUFFER_SIZE) % UART_RING_BUFFER_SIZE;
          break;
        }
        
        pms7003Buffer[pms7003Index++] = data;
        
        // Move to next byte
        uartRingBufferTail = (uartRingBufferTail + 1) % UART_RING_BUFFER_SIZE;
        available = (uartRingBufferHead - uartRingBufferTail + UART_RING_BUFFER_SIZE) % UART_RING_BUFFER_SIZE;
        
        // Check if we have complete 32-byte frame
        if (pms7003Index >= 32)
        {
          // Calculate checksum (sum of first 30 bytes)
          uint16 calcChecksum = 0;
          for (uint8 j = 0; j < 30; j++)
          {
            calcChecksum += pms7003Buffer[j];
          }
          
          // Get received checksum (bytes 30 and 31)
          uint16 recvChecksum = BUILD_UINT16(pms7003Buffer[31], pms7003Buffer[30]);
          
          if (calcChecksum == recvChecksum)
          {
            // Valid data
            uint16 pm1_0 = BUILD_UINT16(pms7003Buffer[5], pms7003Buffer[4]);
            uint16 pm2_5 = BUILD_UINT16(pms7003Buffer[7], pms7003Buffer[6]);
            uint16 pm10_0 = BUILD_UINT16(pms7003Buffer[9], pms7003Buffer[8]);
            
            // Print PMS7003 data locally with device identifier
            if (SampleApp_NwkState != DEV_ZB_COORD)
            {
              uint16 selfAddr = NLME_GetShortAddr();
              char pmsBuffer[80];
              sprintf(pmsBuffer, "[END0x%04X] PMS7003: PM1.0=%d PM2.5=%d PM10=%d\r\n", 
                      selfAddr, pm1_0, pm2_5, pm10_0);
              (void)SerialApp_SafeUARTWrite(0, (uint8*)pmsBuffer, strlen(pmsBuffer));
            }
            
#if defined ( LCD_SUPPORTED )
            char lcdBuffer1[17];
            char lcdBuffer2[17];
            sprintf(lcdBuffer1, "PM2.5:%d", pm2_5);
            sprintf(lcdBuffer2, "PM10:%d", pm10_0);
            HalLcdWriteScreen(lcdBuffer1, lcdBuffer2);
#endif
            
            // Send complete 32-byte raw PMS7003 frame via Zigbee
            SerialApp_SendSensorData(SENSOR_TYPE_PMS7003, pms7003Buffer, 32);
          }
          
          // Reset for next frame
          pms7003State = 0;
          pms7003Index = 0;
        }
        break;
    }
  }
}

