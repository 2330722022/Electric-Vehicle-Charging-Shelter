#ifndef DEBUGUTILS_H
#define DEBUGUTILS_H

#include "ZComDef.h"

void SerialApp_DebugPrint(const char *format, ...);
void SerialApp_DebugPrintLCD(const char *line1, const char *line2);
void SerialApp_DebugPMS7003Data(uint16 pm1_0, uint16 pm2_5, uint16 pm10_0);
void SerialApp_DebugADCData(uint16 adcValue, float voltage);
void SerialApp_DebugNetworkStatus(void);
void SerialApp_DebugSensorConfig(void);
void SerialApp_DebugMessage(const char *message);
void SerialApp_DebugError(const char *error);
void SerialApp_DebugInfo(const char *info);

#endif