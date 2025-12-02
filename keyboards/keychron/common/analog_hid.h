#pragma once
#include "quantum.h"
#define CUSTOM_HID_CMD 0x23  // Define a custom command ID for your use

// Called when data arrives from the host
void custom_hid_receive(uint8_t *data, uint8_t length);

// Optional helper: send data back to the host
void custom_hid_send(uint8_t *data, uint8_t length);
