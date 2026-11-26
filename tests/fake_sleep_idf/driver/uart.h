#pragma once

#include <esp_err.h>

constexpr int UART_NUM_0 = 0;
esp_err_t uart_wait_tx_done(int port, unsigned ticks);
