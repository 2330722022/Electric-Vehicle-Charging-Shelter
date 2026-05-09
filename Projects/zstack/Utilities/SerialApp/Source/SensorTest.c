#include "SerialApp.h"
#include "SensorConfig.h"
#include "hal_uart.h"
#include "hal_led.h"
#include "stdio.h"

void SerialApp_SelfTest(void)
{
  char buffer[64];
  uint16 adcValue;
  float voltage;
  
  HalUARTWrite(0, (uint8*)"=== SerialApp Sensor Test ===\r\n", 32);
  
  // Test ADC
  HalUARTWrite(0, (uint8*)"Testing ADC...\r\n", 16);
  adcValue = HalAdcRead(SENSOR_ADC_CHANNEL, SENSOR_ADC_RESOLUTION);
  voltage = (adcValue * ADC_VOLTAGE_REFERENCE) / ADC_MAX_VALUE;
  
  sprintf(buffer, "ADC Value: %d, Voltage: %.2fV\r\n", adcValue, voltage);
  HalUARTWrite(0, (uint8*)buffer, strlen(buffer));
  
  // Test Network Status
  HalUARTWrite(0, (uint8*)"Network Status: ", 16);
  switch (SampleApp_NwkState)
  {
    case DEV_INIT:
      HalUARTWrite(0, (uint8*)"Initializing\r\n", 14);
      break;
    case DEV_ZB_COORD:
      HalUARTWrite(0, (uint8*)"Coordinator\r\n", 12);
      break;
    case DEV_ROUTER:
      HalUARTWrite(0, (uint8*)"Router\r\n", 8);
      break;
    case DEV_END_DEVICE:
      HalUARTWrite(0, (uint8*)"End Device\r\n", 12);
      break;
    default:
      HalUARTWrite(0, (uint8*)"Unknown\r\n", 9);
      break;
  }
  
  // Test LED
  HalUARTWrite(0, (uint8*)"Testing LEDs...\r\n", 17);
  HalLedSet(HAL_LED_1, HAL_LED_MODE_ON);
  osal_start_timerEx(SerialApp_TaskID, SENSOR_DATA_EVT, 1000);
  HalLedSet(HAL_LED_1, HAL_LED_MODE_OFF);
  
  HalLedSet(HAL_LED_2, HAL_LED_MODE_ON);
  osal_start_timerEx(SerialApp_TaskID, SENSOR_DATA_EVT, 1000);
  HalLedSet(HAL_LED_2, HAL_LED_MODE_OFF);
  
  HalUARTWrite(0, (uint8*)"=== Test Complete ===\r\n", 24);
}

void SerialApp_DiagnosticInfo(void)
{
  char buffer[128];
  
  HalUARTWrite(0, (uint8*)"=== Diagnostic Information ===\r\n", 32);
  
  sprintf(buffer, "Task ID: %d\r\n", SerialApp_TaskID);
  HalUARTWrite(0, (uint8*)buffer, strlen(buffer));
  
  sprintf(buffer, "Network State: %d\r\n", SampleApp_NwkState);
  HalUARTWrite(0, (uint8*)buffer, strlen(buffer));
  
  sprintf(buffer, "ADC Channel: %d\r\n", SENSOR_ADC_CHANNEL);
  HalUARTWrite(0, (uint8*)buffer, strlen(buffer));
  
  sprintf(buffer, "ADC Resolution: %d\r\n", SENSOR_ADC_RESOLUTION);
  HalUARTWrite(0, (uint8*)buffer, strlen(buffer));
  
  sprintf(buffer, "ADC Interval: %d ms\r\n", SENSOR_ADC_INTERVAL_MS);
  HalUARTWrite(0, (uint8*)buffer, strlen(buffer));
  
  sprintf(buffer, "PMS7003 Enabled: %s\r\n", SENSOR_PMS7003_ENABLE ? "Yes" : "No");
  HalUARTWrite(0, (uint8*)buffer, strlen(buffer));
  
  HalUARTWrite(0, (uint8*)"=== End Diagnostic ===\r\n", 26);
}