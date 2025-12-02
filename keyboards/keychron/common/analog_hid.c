/* custom_hid.c
*
* Exposes analog helper functions over HID.
* Inspired from hall_effect_utilities.h @ https://github.com/BorisTestov/qmk_firmware/blob/feature/hall-effect-utilities/keyboards/keychron/common/hall_effect_utilities.h
*
* Copyright
*/

#include QMK_KEYBOARD_H
#include "raw_hid.h"
#include "analog_hid.h"
#include <string.h>
#include <stdbool.h>
#include "quantum.h"
#include "analog_matrix.h"
#include "profile.h"
#include "action_socd.h"
#include "game_controller_common.h"
#include "xinput_keycodes.h"
#include <stdint.h>
#include "version.h"

#pragma GCC diagnostic ignored "-Wunused-function"
#ifndef RAW_EPSIZE
#    define RAW_EPSIZE 32
#endif

// =============================================================================
// CONSTANTS AND DEFINES
// =============================================================================

// Advanced Mode Constants
// ========================
// These control the advanced behavior modes for individual keys (from profile.c enum)
#define ADV_MODE_CLEAR 0             // Clear/disable advanced mode (normal key behavior)
#define ADV_MODE_OKMC 1              // Dynamic Key Strokes mode (OKMC - One Key Multi Code)
#define ADV_MODE_GAME_CONTROLLER 2   // Game controller/XInput mode for analog axes and buttons
#define ADV_MODE_TOGGLE 3            // Toggle mode - key acts as a toggle switch

// Hall Effect Calibration Constants
// ==================================
// These commands are sent to the analog matrix controller via analog_matrix_rx()
#define AMC_CALIBRATE 0x40            // Command to control calibration mode
#define AMC_GET_CALIBRATE_STATE 0x41  // Query current calibration state
#define AMC_GET_CALIBRATED_VALUE 0x42 // Retrieve calibrated values for a key

// Calibration States
// ==================
// The analog matrix controller maintains these internal states during calibration
#define CALIB_OFF 0                    // Calibration inactive - normal operation
#define CALIB_ZERO_TRAVEL_POWER_ON 1   // Auto zero calibration on power-up (not used in manual flow)
#define CALIB_ZERO_TRAVEL_MANUAL 2     // Manual zero travel calibration active
// LED: Solid RED - Keys should be completely released (not pressed at all)
#define CALIB_FULL_TRAVEL_MANUAL 3     // Manual full travel calibration active
// LED: Solid PURPLE - Press keys to maximum travel (turns GREEN when complete)
#define CALIB_SAVE_AND_EXIT 4          // Save calibration data and exit to normal mode (auto-saved)
#define CALIB_CLEAR 5                  // Clear all stored calibration data

/* Custom opcodes */
enum {
  HID_CMD_PING                     = 0x10,
  HID_CMD_GET_PROFILES_INFO        = 0x11,
  HID_CMD_GET_HE_PROFILE           = 0x12,
  HID_CMD_SWITCH_HE_PROFILE        = 0x13,
  HID_CMD_CYCLE_HE_PROFILE         = 0x14,
  HID_CMD_SAVE_PROFILE             = 0x15,
  HID_CMD_RESET_PROFILE            = 0x16,
  HID_CMD_SET_ADVANCE_MODE         = 0x17,
  HID_CMD_SET_TRAVAL               = 0x18,

  HID_CMD_SET_KEY_ACT_PT           = 0x20,
  HID_CMD_SET_KEY_RAPID            = 0x21,
  HID_CMD_SET_SOCD_LAST            = 0x22,
  HID_CMD_SET_SOCD_SNAP            = 0x23,
  HID_CMD_SET_SOCD_NEUTRAL         = 0x24,
  HID_CMD_SET_SOCD_PRI1            = 0x25,
  HID_CMD_SET_SOCD_PRI2            = 0x26,
  HID_CMD_SET_OKMC                 = 0x27,
  HID_CMD_SET_TOGGLE               = 0x28,

  HID_CMD_SET_GAMEPAD              = 0x30,
  HID_CMD_GET_CURVE                = 0x31,
  HID_CMD_SET_CURVE                = 0x32,
  HID_CMD_GET_GAME_CONTROLLER_MODE = 0x33,
  HID_CMD_SET_GAME_CONTROLLER_MODE = 0x34,

  HID_CMD_START_CALIB              = 0x40,
  HID_CMD_CLEAR_CALIB              = 0x41,
  HID_CMD_GET_CALIB_STATE          = 0x42,
  HID_CMD_GET_KEY_CALIB            = 0x43,

  HID_CMD_GET_XINPUT		   = 0x50,
  HID_CMD_TYPE_ENABLED	     	   = 0x51,

  HID_CMD_GET_FIRMWARE_INFO        = 0x60
};

// Convert two bytes (little-endian) to a 16-bit value
uint16_t le16(const uint8_t *data, size_t offset) {
  return (uint16_t)(data[offset] | (data[offset + 1] << 8));
}
static void debug_dump_packet(const char *tag, const uint8_t *buf, int len) {
  dprintf("%s (%d bytes):", tag, len);
  for (int i = 0; i < len; i++) {
    dprintf(" %02X", buf[i]);
  }
  dprintf("\n");
}

static void send_response_and_log(uint8_t *resp_buf, uint8_t resp_len) {
  if (resp_len == 0) {
    return;
  }
  uint8_t padded[32] = {0};
  padded[0] = CUSTOM_HID_CMD;              // prepend required prefix
  memcpy(&padded[1], resp_buf, resp_len);
  raw_hid_send(padded, 32);
}

void custom_hid_receive(uint8_t *data, uint8_t length) {
  if (length >= 2 && data[0] == CUSTOM_HID_CMD) {
    memmove(data, data + 1, length - 1);
    length -= 1;
  }

  if (length == 0) {
    return;
  }

  uint8_t cmd = data[0];
  uint8_t resp[RAW_EPSIZE];
  memset(resp, 0, sizeof(resp));
  uint8_t resp_len = 0;

  switch (cmd) {

    case HID_CMD_PING: {
      resp[0] = HID_CMD_PING;
      resp[1] = 0xAA; /* pong ack */
      resp_len = 2;
      send_response_and_log(resp, resp_len);
      return;
    }

    case HID_CMD_GET_PROFILES_INFO: {
      // Directly implement the logic from the analog_matrix_rx function
      uint8_t current_profile_index = profile_get_current_index();
      uint8_t profile_count = PROFILE_COUNT;
      uint16_t profile_size = PROFILE_SIZE;
      uint8_t okmc_count = OKMC_COUNT;
      uint8_t socd_count = SOCD_COUNT;
      // Populate the response buffer
      resp[0] = HID_CMD_GET_PROFILES_INFO;  // Command identifier
      resp[1] = 0x01;  // Success (ACK)
      resp[2] = current_profile_index;  // Current profile index
      resp[3] = profile_count;          // Profile count
      resp[4] = profile_size & 0xFF;    // Low byte of profile size
      resp[5] = (profile_size >> 8) & 0xFF;  // High byte of profile size
      resp[6] = okmc_count;             // OKMC count
      resp[7] = socd_count;             // SOCD count
      resp_len = 8;  // Response length
      send_response_and_log(resp, resp_len);  // Send the response
      return;
    }

    /**
     * Returns the currently active HE profile index (0-2).
     */
    case HID_CMD_GET_HE_PROFILE: {
      // Directly implement the logic from the hall effect file
      uint8_t profile = profile_get_current_index();  // Get the current profile index
      resp[0] = HID_CMD_GET_HE_PROFILE;
      resp[1] = 0x01;  // Success response
      resp[2] = profile;  // Send the current profile as payload
      resp_len = 3;
      send_response_and_log(resp, resp_len);
      return;
    }

    /* Switches to a different Hall Effect profile (0-2).
     * All subsequent configuration functions will operate on this profile.
     * This is similar to QMK layer switching but for HE analog settings.
     * Like TO(1) which switches to a layer persistently, this switches the active HE profile.*/
    case HID_CMD_SWITCH_HE_PROFILE: {
      uint8_t idx = length > 1 ? data[1] : 0xFF;

      // Directly implement the logic from the hall effect file
      if (idx >= PROFILE_COUNT) {
        resp[0] = HID_CMD_SWITCH_HE_PROFILE;
        resp[1] = 0x00;  // Error response
        resp_len = 2;
      } else {
        profile_select(idx, true);  // Select the profile and indicate with LED
        resp[0] = HID_CMD_SWITCH_HE_PROFILE;
        resp[1] = 0x01;  // Success response
        resp_len = 2;
      }
      send_response_and_log(resp, resp_len);
      return;
    }

    /**
     * Cycles to the next HE profile in sequence: 0 → 1 → 2 → 0 → ...
     * @note LED indication is enabled when switching profiles
     */
    case HID_CMD_CYCLE_HE_PROFILE: {
      // Directly implement the logic from the hall effect file
      uint8_t current = profile_get_current_index();
      uint8_t next = (current + 1) % PROFILE_COUNT;
      profile_select(next, true);  // Select the next profile and indicate with LED
      resp[0] = HID_CMD_CYCLE_HE_PROFILE;
      resp[1] = 0x01;  // Success response
      resp_len = 2;
      send_response_and_log(resp, resp_len);
      return;
    }

    case HID_CMD_SAVE_PROFILE: {
      // Save the profile using the profile_save function
      bool success = profile_save(data[2]);
      // Set the response status based on success or failure
      data[2] = success ? 0 : 1;  // Success = 0, Failure = 1
      // Send the response back to the host
      resp[0] = HID_CMD_SAVE_PROFILE;
      resp[1] = data[2];  // Status: success or failure
      resp_len = 2;
      send_response_and_log(resp, resp_len);
      return;
    }

    case HID_CMD_RESET_PROFILE: {
      // Reset the profile using the profile_reset function
      bool success = profile_reset(data[2]);
      // Update travel configurations
      update_travel_configs();
      // Set the response status based on success or failure
      data[2] = success ? 0 : 1;  // Success = 0, Failure = 1
      // Send the response back to the host
      resp[0] = HID_CMD_RESET_PROFILE;
      resp[1] = data[2];  // Status: success or failure
      resp_len = 2;
      send_response_and_log(resp, resp_len);
      return;
    }

    // <profile index> <mode> <row> <col> <index>
    case HID_CMD_SET_ADVANCE_MODE: {
      // Directly implement the logic from the analog_matrix_rx function
      bool success = profile_set_adv_mode(&data[2]);  // Call the function with the data starting at index 2
      // Set the response status based on success or failure
      data[2] = success ? 0 : 1;  // Success = 0, Failure = 1
      // Send the response back to the host
      resp[0] = HID_CMD_SET_ADVANCE_MODE;
      resp[1] = data[2];  // Status: success or failure
      resp_len = 2;
      send_response_and_log(resp, resp_len);
      return;
    }

    //<profile> <mode> <act_pt> <sens> <rls_sens> <entire> <row> <col>
    case HID_CMD_SET_TRAVAL: {
      // Retrieve the necessary arguments from the data array
      uint8_t profile = data[2];  // Profile index
      uint8_t mode = data[3];     // Mode (e.g., full or partial travel)
      // Combine bytes 4 and 5 into a 16-bit actuation point (act_pt)
      uint16_t act_pt = ((uint16_t)data[5] << 8) | data[4];
      // Combine bytes 6+7 and 8+9 into 16-bit sensitivities
      uint16_t sens = ((uint16_t)data[7] << 8) | data[6];
      uint16_t rls_sens = ((uint16_t)data[9] << 8) | data[8];
      // Whether the entire matrix is affected or just specific rows
      bool entire = data[10];
      // Prepare the row mask for specific rows if 'entire' is false
      uint32_t row_mask[MATRIX_ROWS] = {0};
      if (!entire) {
        uint8_t row = data[11];
        uint8_t col = data[12];
        row_mask[row] = (1 << col);  // Set the column bit for the specified row
      }
      // Directly implement the logic from the hall effect file
      bool success = profile_set_traval(profile, mode, act_pt, sens, rls_sens, entire, row_mask);
      // Set the response data based on success or failure
      data[2] = success ? 0 : 1;  // Success = 0, Failure = 1
      // Send the response back to the host
      resp[0] = HID_CMD_SET_TRAVAL;
      resp[1] = data[2];  // Status: success or failure
      resp_len = 2;
      send_response_and_log(resp, resp_len);
      return;
    }

    //set single key actuation point
    //<row> <col> <act_pt 0-1023>
    case HID_CMD_SET_KEY_ACT_PT: {
      if (length >= 5) {
        uint8_t row = data[1];
        uint8_t col = data[2];
        uint16_t act_pt = (uint16_t)data[3] | ((uint16_t)data[4] << 8);
        // Directly implement the logic from the hall effect file
        analog_matrix_profile_t *cur_prof = profile_get_current();
        analog_key_config_t *p_key_cfg = &cur_prof->key_config[row][col];
        // Set custom actuation point (0 = use global setting)
        p_key_cfg->act_pt = act_pt;
        // Apply new key config
        update_key_config(row, col);  // Applying the new key config
        resp[0] = HID_CMD_SET_KEY_ACT_PT;
        resp[1] = 0x01;
        resp_len = 2;
      } else {
        resp[0] = HID_CMD_SET_KEY_ACT_PT;
        resp[1] = 0x00;
        resp_len = 2;
      }
      send_response_and_log(resp, resp_len);
      return;
    }

    //<row> <col> <act 0-1023> <deact 0-1023>
    case HID_CMD_SET_KEY_RAPID: {
      if (length >= 7) {
        uint8_t row = data[1];
        uint8_t col = data[2];
        uint16_t press = le16(data, 3);
        uint16_t release = le16(data, 5);
        // Directly implement the logic from the hall effect file
        analog_matrix_profile_t *cur_prof = profile_get_current();
        analog_key_config_t *p_key_cfg = &cur_prof->key_config[row][col];
        // Set rapid trigger mode
        p_key_cfg->mode = AKM_RAPID;
        // Set sensitivities
        p_key_cfg->rpd_trig_sen = press;
        p_key_cfg->rpd_trig_sen_deact = release;
        // Apply new key config
        update_key_config(row, col);  // Apply the new key config
        resp[0] = HID_CMD_SET_KEY_RAPID;
        resp[1] = 0x01;
        resp_len = 2;
      } else {
        resp[0] = HID_CMD_SET_KEY_RAPID;
        resp[1] = 0x00;
        resp_len = 2;
      }
      send_response_and_log(resp, resp_len);
      return;
    }

    //<row1> <col1> <row2> <col2> <slot 0-20>
    case HID_CMD_SET_SOCD_LAST: {
      if (length >= 6) {
        uint8_t row1 = data[1];
        uint8_t col1 = data[2];
        uint8_t row2 = data[3];
        uint8_t col2 = data[4];
        uint8_t socd_index = data[5];
        // Directly implement the logic from the hall effect file
        uint8_t cur_prof_idx = profile_get_current_index();
        if (socd_index >= SOCD_COUNT) return;  // Prevent array overflow
        // Configure SOCD entry - this modifies profile_get_current()->socd[socd_index]
        uint8_t data_socd[7] = {
          cur_prof_idx,              // Profile index (which profile to modify)
          row1,                       // Key 1 row position
          col1,                       // Key 1 col position
          row2,                       // Key 2 row position
          col2,                       // Key 2 col position
          socd_index,                // Which array slot to use (0 to SOCD_COUNT-1)
          SOCD_PRI_LAST_KEYSTROKE    // Last key priority type
        };
        profile_set_socd(data_socd);  // Apply SOCD entry to the profile
        resp[0] = HID_CMD_SET_SOCD_LAST;
        resp[1] = 0x01;
      } else {
        resp[0] = HID_CMD_SET_SOCD_LAST;
        resp[1] = 0x00;
      }
      resp_len = 2;
      send_response_and_log(resp, resp_len);
      return;
    }

    //<row1> <col1> <row2> <col2> <slot 0-20> <single/not 0/1>
    case HID_CMD_SET_SOCD_SNAP: {
      if (length >= 7) {
        bool single = data[6] ? true : false;
        // Directly implement the logic from the hall effect file
        uint8_t cur_prof_idx = profile_get_current_index();
        if (data[5] >= SOCD_COUNT) return;  // Prevent array overflow
        // Configure SOCD entry - this modifies profile_get_current()->socd[socd_index]
        uint8_t data_socd[7] = {
          cur_prof_idx,  // Profile index
          data[1],       // Key 1 row position
          data[2],       // Key 1 col position
          data[3],       // Key 2 row position
          data[4],       // Key 2 col position
          data[5],       // Which array slot to use (0 to SOCD_COUNT-1)
          single ? SOCD_PRI_DEEPER_TRAVEL_SINGLE : SOCD_PRI_DEEPER_TRAVEL  // Snap click type
        };
        profile_set_socd(data_socd);  // Apply SOCD entry to the profile
        resp[0] = HID_CMD_SET_SOCD_SNAP;
        resp[1] = 0x01;
      } else {
        resp[0] = HID_CMD_SET_SOCD_SNAP;
        resp[1] = 0x00;
      }
      resp_len = 2;
      send_response_and_log(resp, resp_len);
      return;
    }

    //<row1> <col1> <row2> <col2> <slot 0-20>
    case HID_CMD_SET_SOCD_NEUTRAL: {
      if (length >= 6) {
        // Directly implement the logic from the hall effect file
        uint8_t cur_prof_idx = profile_get_current_index();
        if (data[5] >= SOCD_COUNT) return;
        uint8_t data_socd[7] = {
          cur_prof_idx,              // Profile index
          data[1],                   // Key 1 row position
          data[2],                   // Key 1 col position
          data[3],                   // Key 2 row position
          data[4],                   // Key 2 col position
          data[5],                   // Which array slot to use
          SOCD_PRI_NEUTRAL           // Neutral SOCD type
        };
        profile_set_socd(data_socd);  // Apply SOCD entry to the profile
        resp[0] = HID_CMD_SET_SOCD_NEUTRAL;
        resp[1] = 0x01;
      } else {
        resp[0] = HID_CMD_SET_SOCD_NEUTRAL;
        resp[1] = 0x00;
      }
      resp_len = 2;
      send_response_and_log(resp, resp_len);
      return;
    }

    //<row1> <col1> <row2> <col2> <slot 0-20>
    case HID_CMD_SET_SOCD_PRI1: {
      if (length >= 6) {
        // Directly implement the logic from the hall effect file
        uint8_t cur_prof_idx = profile_get_current_index();
        if (data[5] >= SOCD_COUNT) return;
        uint8_t data_socd[7] = {
          cur_prof_idx,              // Profile index
          data[1],                   // Key 1 row position
          data[2],                   // Key 1 col position
          data[3],                   // Key 2 row position
          data[4],                   // Key 2 col position
          data[5],                   // Which array slot to use
          SOCD_PRI_KEY_1             // Key 1 priority type
        };
        profile_set_socd(data_socd);  // Apply SOCD entry to the profile
        resp[0] = HID_CMD_SET_SOCD_PRI1;
        resp[1] = 0x01;
      } else {
        resp[0] = HID_CMD_SET_SOCD_PRI1;
        resp[1] = 0x00;
      }
      resp_len = 2;
      send_response_and_log(resp, resp_len);
      return;
    }

    //<row1> <col1> <row2> <col2> <slot 0-20>
    case HID_CMD_SET_SOCD_PRI2: {
      if (length >= 6) {
        // Directly implement the logic from the hall effect file
        uint8_t cur_prof_idx = profile_get_current_index();
        if (data[5] >= SOCD_COUNT) return;
        uint8_t data_socd[7] = {
          cur_prof_idx,              // Profile index
          data[1],                   // Key 1 row position
          data[2],                   // Key 1 col position
          data[3],                   // Key 2 row position
          data[4],                   // Key 2 col position
          data[5],                   // Which array slot to use
          SOCD_PRI_KEY_2             // Key 2 priority type
        };
        profile_set_socd(data_socd);  // Apply SOCD entry to the profile
        resp[0] = HID_CMD_SET_SOCD_PRI2;
        resp[1] = 0x01;
      } else {
        resp[0] = HID_CMD_SET_SOCD_PRI2;
        resp[1] = 0x00;
      }
      resp_len = 2;
      send_response_and_log(resp, resp_len);
      return;
    }

    //<row><col><id><shallow_act><shallow_deact><deep_act><deep_deact><key1><key2><key3><key4><a1><a2><a3><a4>
    //ex 1   5   0      250         220            950       940       0x04  0x16 0x11   0x22 0x.. 0x.. 0x.. 0x..
    case HID_CMD_SET_OKMC: {
      if (length >= 25) {
        uint8_t row = data[1];
        uint8_t col = data[2];
        uint8_t okmc_index = data[3];   // FIXED — idx is here
        uint16_t shallow_act   = le16(data, 4);
        uint16_t shallow_deact = le16(data, 6);
        uint16_t deep_act      = le16(data, 8);
        uint16_t deep_deact    = le16(data,10);
        uint16_t key1 = le16(data,12);
        uint16_t key2 = le16(data,14);
        uint16_t key3 = le16(data,16);
        uint16_t key4 = le16(data,18);
        uint8_t action1 = data[20];
        uint8_t action2 = data[21];
        uint8_t action3 = data[22];
        uint8_t action4 = data[23];
        uint8_t cur_prof_idx = profile_get_current_index();
        if (okmc_index >= OKMC_COUNT) return;
        uint8_t data_socd[29] = {0};
        data_socd[0] = cur_prof_idx;
        data_socd[1] = ADV_MODE_OKMC;
        data_socd[2] = row;
        data_socd[3] = col;
        data_socd[4] = okmc_index;
        data_socd[5]  = shallow_act & 0xFF;
        data_socd[6]  = shallow_act >> 8;
        data_socd[7]  = shallow_deact & 0xFF;
        data_socd[8]  = shallow_deact >> 8;
        data_socd[9]  = deep_act & 0xFF;
        data_socd[10] = deep_act >> 8;
        data_socd[11] = deep_deact & 0xFF;
        data_socd[12] = deep_deact >> 8;
        data_socd[13] = key1 & 0xFF; data_socd[14] = key1 >> 8;
        data_socd[15] = key2 & 0xFF; data_socd[16] = key2 >> 8;
        data_socd[17] = key3 & 0xFF; data_socd[18] = key3 >> 8;
        data_socd[19] = key4 & 0xFF; data_socd[20] = key4 >> 8;
        // 4 action bytes encoded into shallow/deep nibbles
        data_socd[21] = ((action1 & 0x01) ? 0x02 : 0) | (((action1 & 0x02) ? 0x02 : 0) << 4);
        data_socd[22] = ((action1 & 0x04) ? 0x02 : 0) | (((action1 & 0x08) ? 0x02 : 0) << 4);
        data_socd[23] = ((action2 & 0x01) ? 0x02 : 0) | (((action2 & 0x02) ? 0x02 : 0) << 4);
        data_socd[24] = ((action2 & 0x04) ? 0x02 : 0) | (((action2 & 0x08) ? 0x02 : 0) << 4);
        data_socd[25] = ((action3 & 0x01) ? 0x02 : 0) | (((action3 & 0x02) ? 0x02 : 0) << 4);
        data_socd[26] = ((action3 & 0x04) ? 0x02 : 0) | (((action3 & 0x08) ? 0x02 : 0) << 4);
        data_socd[27] = ((action4 & 0x01) ? 0x02 : 0) | (((action4 & 0x02) ? 0x02 : 0) << 4);
        data_socd[28] = ((action4 & 0x04) ? 0x02 : 0) | (((action4 & 0x08) ? 0x02 : 0) << 4);
        profile_set_adv_mode(data_socd);
        resp[0] = HID_CMD_SET_OKMC;
        resp[1] = 0x01;
      } else {
        resp[0] = HID_CMD_SET_OKMC;
        resp[1] = 0x00;
      }
      resp_len = 2;
      send_response_and_log(resp, resp_len);
      return;
    }

    //<row><col> set key permanent toggled
    case HID_CMD_SET_TOGGLE: {
      if (length >= 3) {
        uint8_t row = data[1];
        uint8_t col = data[2];
        // Directly implement the logic from the hall effect file
        uint8_t cur_prof_idx = profile_get_current_index();
        // Configure key for toggle mode
        uint8_t data_toggle[5] = {
          cur_prof_idx,           // Profile index
          ADV_MODE_TOGGLE,        // Mode = Toggle mode
          row,                    // Key row position
          col,                    // Key col position
          0                       // Index (unused for toggle mode)
        };
        profile_set_adv_mode(data_toggle);  // Set the toggle mode for the key
        resp[0] = HID_CMD_SET_TOGGLE;
        resp[1] = 0x01;
      } else {
        resp[0] = HID_CMD_SET_TOGGLE;
        resp[1] = 0x00;
      }
      resp_len = 2;
      send_response_and_log(resp, resp_len);
      return;
    }

    //<row><col><xinput_keycode>
    case HID_CMD_SET_GAMEPAD: {
      if (length >= 5) {
        uint8_t row = data[1];
        uint8_t col = data[2];
        uint16_t xinput = le16(data, 3);
        // Directly implement the logic from the hall effect file
        uint8_t cur_prof_idx = profile_get_current_index();
        // Extract axis index from xinput_keycode
        uint8_t axis_index = (xinput >> 5) & 0x1F;  // Extract bits 5-9 (axis index)
        // Configure key for game controller mode
        uint8_t data_gamepad[5] = {
          cur_prof_idx,               // Profile index
          ADV_MODE_GAME_CONTROLLER,   // Mode = Game controller mode
          row,                        // Key row position
          col,                        // Key col position
          axis_index                  // Axis/button index
        };
        profile_set_adv_mode(data_gamepad);  // Set the game controller mode for the key
        resp[0] = HID_CMD_SET_GAMEPAD;
        resp[1] = 0x01;
      } else {
        resp[0] = HID_CMD_SET_GAMEPAD;
        resp[1] = 0x00;
      }
      resp_len = 2;
      send_response_and_log(resp, resp_len);
      return;
    }

    //get controller curve points
    case HID_CMD_GET_CURVE: {
      // Get the curve points and store them in the data buffer
      bool success = game_controller_get_curve(data);  // No need to change this, we already know the data is correct
      // Prepare the response buffer
      if (success) {
        resp[0] = HID_CMD_GET_CURVE;  // Command identifier
        resp[1] = 0x01;               // Success response
        // Calculate response length correctly
        resp_len = CURVE_POINTS_COUNT * 4 + 2;  // 4 bytes per curve point, plus 2 bytes for command and success byte
        // Add curve data to response buffer
        memcpy(&resp[2], data, CURVE_POINTS_COUNT * 4);  // Copy the full curve data from data[] into resp[]
        send_response_and_log(resp, resp_len);  // Send the response
      } else {
        resp[0] = HID_CMD_GET_CURVE;  // Command identifier
        resp[1] = 0x00;               // Failure response
        resp_len = 2;
        send_response_and_log(resp, resp_len);  // Send the error response
      }
      return;
    }

    //set controller curve points
    // 0 0 256 8191 767 24575 1023 32767
    case HID_CMD_SET_CURVE: {
      if (length >= 17) {
        uint16_t x1 = le16(data, 1);
        uint16_t y1 = le16(data, 3);
        uint16_t x2 = le16(data, 5);
        uint16_t y2 = le16(data, 7);
        uint16_t x3 = le16(data, 9);
        uint16_t y3 = le16(data, 11);
        uint16_t x4 = le16(data, 13);
        uint16_t y4 = le16(data, 15);
        // Directly implement the logic from the hall effect file
        point_t curve_points[4] = {
          {x1, y1},
          {x2, y2},
          {x3, y3},
          {x4, y4}
        };
        // Encode them into 16-bit little-endian bytes
        uint8_t encoded[CURVE_POINTS_COUNT * 4];
        for (int i = 0; i < CURVE_POINTS_COUNT; i++) {
          encoded[i * 4 + 0] = (uint8_t)(curve_points[i].x & 0xFF);
          encoded[i * 4 + 1] = (uint8_t)((curve_points[i].x >> 8) & 0xFF);
          encoded[i * 4 + 2] = (uint8_t)(curve_points[i].y & 0xFF);
          encoded[i * 4 + 3] = (uint8_t)((curve_points[i].y >> 8) & 0xFF);
        }
        // Send the encoded curve data to the game controller
        game_controller_set_curve(encoded);
        resp[0] = HID_CMD_SET_CURVE;
        resp[1] = 0x01;
      } else {
        resp[0] = HID_CMD_SET_CURVE;
        resp[1] = 0x00;
      }
      resp_len = 2;
      send_response_and_log(resp, resp_len);
      return;
    }

    //get current mode, can be 0/1/2/3
    case HID_CMD_GET_GAME_CONTROLLER_MODE: {
      // Retrieve the current game controller mode and store it correctly in data[1]
      bool success = game_controller_mode_get(&data[1]);  // Pass data[1] for mode value
      // Use a separate variable for success flag
      uint8_t status = success ? 0 : 1;  // 0 = success, 1 = failure
      // Respond with the correct mode value in data[1]
      resp[0] = HID_CMD_GET_GAME_CONTROLLER_MODE;  // Command ID
      resp[1] = data[2];  // Correctly send the mode value from data[1]
      resp[2] = status;   // Send the success flag (0 = success, 1 = failure)
      resp_len = 3;  // We're now sending 3 bytes: Command, Mode, Status
      send_response_and_log(resp, resp_len);
      return;
    }

    // set 0/1/2/3 depending on xinput/typing while controller mode
    //0 no xinput or typing while in controller mode
    //1 xinput in controller mode is active
    //2 typing in controller mode is active
    //3 both xinput and typing are active in controller mode
    case HID_CMD_SET_GAME_CONTROLLER_MODE: {
      // Set the game controller mode
      bool success = game_controller_mode_set(data[2]);
      // Send the success or failure response back to the host
      data[2] = success ? 0 : 1;  // Success = 0, Failure = 1
      // Log for debugging
      // Send the response back to the host
      resp[0] = HID_CMD_SET_GAME_CONTROLLER_MODE;
      resp[1] = data[2];  // Status: success or failure
      resp_len = 2;
      send_response_and_log(resp, resp_len);
      return;
    }

    // start calibration of keys
    case HID_CMD_START_CALIB: {
      // Directly implement the logic from the hall effect file
      wait_ms(1000);  // 1 second delay to ensure all keys are released before calibration
      uint8_t data[4] = {0xA9, AMC_CALIBRATE, CALIB_ZERO_TRAVEL_MANUAL, 0};
      analog_matrix_rx(data, 4);  // Send the calibration command
      resp[0] = HID_CMD_START_CALIB;
      resp[1] = 0x01;
      resp_len = 2;
      send_response_and_log(resp, resp_len);
      return;
    }

    //not currently implemented
    case HID_CMD_CLEAR_CALIB: {
      // Directly implement the logic from the hall effect file
      uint8_t data[4] = {0xA9, AMC_CALIBRATE, CALIB_CLEAR, 0};
      analog_matrix_rx(data, 4);  // Send the clear calibration command
      resp[0] = HID_CMD_CLEAR_CALIB;
      resp[1] = 0x01;
      resp_len = 2;
      send_response_and_log(resp, resp_len);
      return;
    }

    //returns 0 when not in calibration mode
    case HID_CMD_GET_CALIB_STATE: {
      // Directly implement the logic from the hall effect file
      uint8_t data[20] = {0xA9, AMC_GET_CALIBRATE_STATE, 0};
      analog_matrix_rx(data, 20);  // Send the get calibration state command
      uint8_t st = data[3];  // Read the state from the response
      resp[0] = HID_CMD_GET_CALIB_STATE;
      resp[1] = st;
      resp_len = 2;
      send_response_and_log(resp, resp_len);
      return;
    }

    //returns key calibration data
    case HID_CMD_GET_KEY_CALIB: {
      if (length >= 3) {
        uint16_t zero = 0, full = 0;
        uint8_t row = data[1];
        uint8_t col = data[2];
        // Check if row/col valid
        if (row >= MATRIX_ROWS || col >= MATRIX_COLS) {
          resp[0] = HID_CMD_GET_KEY_CALIB;
          resp[1] = 0x00;
          resp_len = 2;
          send_response_and_log(resp, resp_len);
          return;
        }
        // Request calibrated values
        uint8_t data_rx[20] = {0xA9, AMC_GET_CALIBRATED_VALUE, row, col, 0};
        analog_matrix_rx(data_rx, 20);
        if (data_rx[4] == 0) {
          zero = (data_rx[6] << 8) | data_rx[5];
          full = (data_rx[8] << 8) | data_rx[7];
          uint16_t travel = (zero > full) ? (zero - full) : 0;
          resp[0] = HID_CMD_GET_KEY_CALIB;
          resp[1] = 1;  // success
          // travel
          resp[2] = travel & 0xFF;
          resp[3] = travel >> 8;
          // zero
          resp[4] = zero & 0xFF;
          resp[5] = zero >> 8;
          // full
          resp[6] = full & 0xFF;
          resp[7] = full >> 8;
          resp_len = 8;
        } else {
          resp[0] = HID_CMD_GET_KEY_CALIB;
          resp[1] = 0x00;
          resp_len = 2;
        }
      } else {
        resp[0] = HID_CMD_GET_KEY_CALIB;
        resp[1] = 0x00;
        resp_len = 2;
      }
      send_response_and_log(resp, resp_len);
      return;
    }

    //checks if xinput mask is active
    case HID_CMD_GET_XINPUT: {
      bool enabled = game_controller_xinput_enabled();
      data[2] = enabled ? 1 : 0;
      // Send the response back to the host
      resp[0] = HID_CMD_GET_XINPUT;
      resp[1] = data[2];
      resp_len = 2;
      send_response_and_log(resp, resp_len);
      return;
    }

    // Case for checking if the typing mode is enabled in controller mode
    case HID_CMD_TYPE_ENABLED: {
      // Check if typing mode is enabled in game controller mode
      bool enabled = game_controller_type_enabled();
      // Set the response data based on whether typing mode is enabled or not
      data[2] = enabled ? 1 : 0;  // 0 = enabled, 1 = disabled
      // Send the response back to the host
      resp[0] = HID_CMD_TYPE_ENABLED;
      resp[1] = data[2];  // Status: 0 (enabled) or 1 (disabled)
      resp_len = 2;
      send_response_and_log(resp, resp_len);
      return;
    }

    case HID_CMD_GET_FIRMWARE_INFO: {
      uint8_t i = 0;
      data[i++] = HID_CMD_GET_FIRMWARE_INFO;  // Command ID first
      data[i++] = 'v';
      if ((DEVICE_VER & 0xF000) != 0)
      itoa((DEVICE_VER >> 12), (char *)&data[i++], 16);
      itoa((DEVICE_VER >> 8) & 0xF, (char *)&data[i++], 16);
      data[i++] = '.';
      itoa((DEVICE_VER >> 4) & 0xF, (char *)&data[i++], 16);
      data[i++] = '.';
      itoa(DEVICE_VER & 0xF, (char *)&data[i++], 16);
      // (Build date removed)
      send_response_and_log(data, i);
      return;
    }

    default: {
      resp[0] = cmd;
      resp[1] = 0xFF;
      resp_len = 2;
      send_response_and_log(resp, resp_len);
      return;
    }
  } /* end switch */
}
