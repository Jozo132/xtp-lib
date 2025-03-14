#pragma once


#ifdef OTA_CLASSIC

#include <Arduino.h>
#define OTETHERNET
#include <ArduinoOTA.h>
// OTAStorage InternalStorage;

#else // NOTA

#include <Arduino.h>


#include "xtp_flash.h"

#endif // OTA_CLASSIC