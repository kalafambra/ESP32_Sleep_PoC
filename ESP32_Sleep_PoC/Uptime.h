#pragma once

#include <inttypes.h>
#include <stdio.h>

#include "Types.h"

constexpr size_t UPTIME_TEXT_SIZE = 25;

inline void formatUptime(UptimeMs uptimeMs, char (&output)[UPTIME_TEXT_SIZE]) {
  const UptimeMs totalSeconds = uptimeMs / 1000;
  snprintf(output, sizeof(output), "%02" PRIu64 ":%02" PRIu64 ":%02" PRIu64 ".%03" PRIu64,
           totalSeconds / 3600, totalSeconds / 60 % 60, totalSeconds % 60, uptimeMs % 1000);
}

UptimeMs uptimeMs();
