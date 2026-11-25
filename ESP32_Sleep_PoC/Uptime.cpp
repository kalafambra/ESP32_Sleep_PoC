#include "Uptime.h"

#include <esp_timer.h>

UptimeMs uptimeMs() {
  return static_cast<UptimeMs>(esp_timer_get_time()) / 1000;
}
