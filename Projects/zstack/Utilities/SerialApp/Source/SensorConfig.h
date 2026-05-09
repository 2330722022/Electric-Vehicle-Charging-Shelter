#ifndef SENSORCONFIG_H
#define SENSORCONFIG_H

#include "ZComDef.h"
#include "hal_adc.h"

// ADC Configuration
#define SENSOR_ADC_ENABLE            TRUE
#define SENSOR_ADC_CHANNEL           HAL_ADC_CHANNEL_0
#define SENSOR_ADC_RESOLUTION        HAL_ADC_RESOLUTION_12
#define SENSOR_ADC_INTERVAL_MS       5000

// ADC Voltage Reference
#define ADC_VOLTAGE_REFERENCE        3.3f
#define ADC_MAX_VALUE               4095.0f

// PMS7003 Configuration
#define SENSOR_PMS7003_ENABLE        TRUE
#define PMS7003_UART_BAUDRATE        HAL_UART_BR_9600
#define PMS7003_HEADER               0x42
#define PMS7003_HEADER_SECOND        0x4D
#define PMS7003_FRAME_LENGTH         32

// Sensor Data Types
#define SENSOR_TYPE_PMS7003          0x01
#define SENSOR_TYPE_ADC              0x02
#define SENSOR_TYPE_TEMPERATURE      0x03
#define SENSOR_TYPE_HUMIDITY         0x04

// Data Transmission Configuration
#define SENSOR_DATA_AUTO_TRANSMIT    TRUE
#define SENSOR_DATA_RETRY_COUNT      3
#define SENSOR_DATA_RETRY_DELAY_MS   100

#endif