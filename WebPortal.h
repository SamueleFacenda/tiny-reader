#pragma once

#include <Arduino.h>

#include "Config.h"

using UploadCompleteCallback = void (*)(const String& path, bool success);

bool webPortalStart(UploadCompleteCallback cb);
void webPortalStop();
void webPortalHandle();
bool webPortalActive();
unsigned long webPortalUptimeMs();
String webPortalIp();
