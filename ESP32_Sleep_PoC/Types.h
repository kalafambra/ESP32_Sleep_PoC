#pragma once

#include <stdint.h>

using UptimeMs = uint64_t;

struct Vec3 {
  float x = 0.0f;
  float y = 0.0f;
  float z = 0.0f;
};

enum class Error { None, NotAvailable, I2cWrite, I2cRead, UnexpectedIdentity, ConfigurationMismatch };

template <typename T>
struct Result {
  T value{};
  Error error = Error::NotAvailable;

  bool valid() const { return error == Error::None; }
};
