#include "SerialApp.h"
#include "hal_uart.h"
#include "stdio.h"

void SerialApp_SensorDataHandler(uint8 *data, uint8 len)
{
  if (len < 1) return;
  
  uint8 sensorType = data[0];
  
  switch (sensorType)
  {
    case SENSOR_TYPE_PMS7003:
    {
      if (len >= 7)
      {
        uint16 pm1_0 = BUILD_UINT16(data[2], data[1]);
        uint16 pm2_5 = BUILD_UINT16(data[4], data[3]);
        uint16 pm10_0 = BUILD_UINT16(data[6], data[5]);
        
        char buffer[64];
        sprintf(buffer, "PMS7003: PM1.0=%d PM2.5=%d PM10=%d\r\n", pm1_0, pm2_5, pm10_0);
        HalUARTWrite(0, (uint8*)buffer, strlen(buffer));
      }
      break;
    }
    
    case SENSOR_TYPE_ADC:
    {
      if (len >= 3)
      {
        uint16 adcValue = BUILD_UINT16(data[2], data[1]);
        float voltage = (adcValue * 3.3) / 4095.0;
        
        char buffer[32];
        sprintf(buffer, "ADC: Value=%d Voltage=%.2fV\r\n", adcValue, voltage);
        HalUARTWrite(0, (uint8*)buffer, strlen(buffer));
      }
      break;
    }
    
    default:
    {
      char buffer[32];
      sprintf(buffer, "Unknown Sensor Type: %d\r\n", sensorType);
      HalUARTWrite(0, (uint8*)buffer, strlen(buffer));
      break;
    }
  }
}