#include "SerialApp.h"
#include "SensorConfig.h"
#include "hal_uart.h"
#include "hal_lcd.h"
#include "hal_led.h"
#include "stdio.h"
#include "stdarg.h"

// Debug level definitions
#define DEBUG_LEVEL_OFF    0
#define DEBUG_LEVEL_BASIC  1
#define DEBUG_LEVEL_VERBOSE 2

#define DEBUG_LEVEL DEBUG_LEVEL_VERBOSE

void SerialApp_DebugPrint(const char *format, ...)
{
#if DEBUG_LEVEL > 0
  char buffer[128];
  va_list args;
  
  va_start(args, format);
  vsprintf(buffer, format, args);
  va_end(args);
  
  HalUARTWrite(0, (uint8*)buffer, strlen(buffer));
#endif
}

void SerialApp_DebugPrintLCD(const char *line1, const char *line2)
{
#if defined ( LCD_SUPPORTED ) && DEBUG_LEVEL > 0
  HalLcdWriteScreen((char*)line1, (char*)line2);
#endif
}

void SerialApp_DebugPMS7003Data(uint16 pm1_0, uint16 pm2_5, uint16 pm10_0)
{
#if DEBUG_LEVEL >= 1
  char buffer[64];
  sprintf(buffer, "PMS7003 Data:\r\n");
  HalUARTWrite(0, (uint8*)buffer, strlen(buffer));
  
  sprintf(buffer, "  PM1.0: %d μg/m³\r\n", pm1_0);
  HalUARTWrite(0, (uint8*)buffer, strlen(buffer));
  
  sprintf(buffer, "  PM2.5: %d μg/m³\r\n", pm2_5);
  HalUARTWrite(0, (uint8*)buffer, strlen(buffer));
  
  sprintf(buffer, "  PM10: %d μg/m³\r\n", pm10_0);
  HalUARTWrite(0, (uint8*)buffer, strlen(buffer));
  
#if defined ( LCD_SUPPORTED )
  char lcdLine1[17];
  char lcdLine2[17];
  sprintf(lcdLine1, "PM2.5:%d", pm2_5);
  sprintf(lcdLine2, "PM10:%d", pm10_0);
  HalLcdWriteScreen(lcdLine1, lcdLine2);
#endif
#endif
}

void SerialApp_DebugADCData(uint16 adcValue, float voltage)
{
#if DEBUG_LEVEL >= 1
  char buffer[64];
  sprintf(buffer, "ADC Data:\r\n");
  HalUARTWrite(0, (uint8*)buffer, strlen(buffer));
  
  sprintf(buffer, "  Value: %d\r\n", adcValue);
  HalUARTWrite(0, (uint8*)buffer, strlen(buffer));
  
  sprintf(buffer, "  Voltage: %.2fV\r\n", voltage);
  HalUARTWrite(0, (uint8*)buffer, strlen(buffer));
  
#if defined ( LCD_SUPPORTED )
  char lcdLine1[17];
  char lcdLine2[17];
  sprintf(lcdLine1, "ADC:%d", adcValue);
  sprintf(lcdLine2, "%.2fV", voltage);
  HalLcdWriteScreen(lcdLine1, lcdLine2);
#endif
#endif
}

void SerialApp_DebugNetworkStatus(void)
{
#if DEBUG_LEVEL >= 1
  char buffer[64];
  sprintf(buffer, "Network Status:\r\n");
  HalUARTWrite(0, (uint8*)buffer, strlen(buffer));
  
  switch (SampleApp_NwkState)
  {
    case DEV_INIT:
      sprintf(buffer, "  State: Initializing\r\n");
      break;
    case DEV_ZB_COORD:
      sprintf(buffer, "  State: Coordinator\r\n");
      break;
    case DEV_ROUTER:
      sprintf(buffer, "  State: Router\r\n");
      break;
    case DEV_END_DEVICE:
      sprintf(buffer, "  State: End Device\r\n");
      break;
    default:
      sprintf(buffer, "  State: Unknown (%d)\r\n", SampleApp_NwkState);
      break;
  }
  HalUARTWrite(0, (uint8*)buffer, strlen(buffer));
  
  uint16 nwkAddr = NLME_GetShortAddr();
  sprintf(buffer, "  Network Address: 0x%04X\r\n", nwkAddr);
  HalUARTWrite(0, (uint8*)buffer, strlen(buffer));
  
  uint16 parentAddr = NLME_GetCoordShortAddr();
  sprintf(buffer, "  Parent Address: 0x%04X\r\n", parentAddr);
  HalUARTWrite(0, (uint8*)buffer, strlen(buffer));
#endif
}

void SerialApp_DebugSensorConfig(void)
{
#if DEBUG_LEVEL >= 2
  char buffer[64];
  sprintf(buffer, "Sensor Configuration:\r\n");
  HalUARTWrite(0, (uint8*)buffer, strlen(buffer));
  
  sprintf(buffer, "  ADC Channel: %d\r\n", SENSOR_ADC_CHANNEL);
  HalUARTWrite(0, (uint8*)buffer, strlen(buffer));
  
  sprintf(buffer, "  ADC Resolution: %d bits\r\n", 
          SENSOR_ADC_RESOLUTION == HAL_ADC_RESOLUTION_8 ? 8 :
          SENSOR_ADC_RESOLUTION == HAL_ADC_RESOLUTION_10 ? 10 :
          SENSOR_ADC_RESOLUTION == HAL_ADC_RESOLUTION_12 ? 12 : 14);
  HalUARTWrite(0, (uint8*)buffer, strlen(buffer));
  
  sprintf(buffer, "  ADC Interval: %d ms\r\n", SENSOR_ADC_INTERVAL_MS);
  HalUARTWrite(0, (uint8*)buffer, strlen(buffer));
  
  sprintf(buffer, "  PMS7003 Enabled: %s\r\n", SENSOR_PMS7003_ENABLE ? "Yes" : "No");
  HalUARTWrite(0, (uint8*)buffer, strlen(buffer));
#endif
}

void SerialApp_DebugMessage(const char *message)
{
#if DEBUG_LEVEL >= 1
  char buffer[64];
  sprintf(buffer, "[DEBUG] %s\r\n", message);
  HalUARTWrite(0, (uint8*)buffer, strlen(buffer));
#endif
}

void SerialApp_DebugError(const char *error)
{
#if DEBUG_LEVEL >= 1
  char buffer[64];
  sprintf(buffer, "[ERROR] %s\r\n", error);
  HalUARTWrite(0, (uint8*)buffer, strlen(buffer));
  
  HalLedSet(HAL_LED_3, HAL_LED_MODE_BLINK);
#endif
}

void SerialApp_DebugInfo(const char *info)
{
#if DEBUG_LEVEL >= 2
  char buffer[64];
  sprintf(buffer, "[INFO] %s\r\n", info);
  HalUARTWrite(0, (uint8*)buffer, strlen(buffer));
#endif
}