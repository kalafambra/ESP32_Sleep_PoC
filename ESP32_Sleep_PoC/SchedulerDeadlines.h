#pragma once

#include "Types.h"

struct SchedulerDeadlines {
  enum Work { Boot, Arming, Zeroing, ZeroingSample, Tilt, Burst, Conversion, Motion, Count };
  UptimeMs at[Count] = {NO_DEADLINE_MS, NO_DEADLINE_MS, NO_DEADLINE_MS, NO_DEADLINE_MS,
                        NO_DEADLINE_MS, NO_DEADLINE_MS, NO_DEADLINE_MS, NO_DEADLINE_MS};
};
