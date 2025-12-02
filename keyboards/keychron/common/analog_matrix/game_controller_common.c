/* Copyright 2024 @ Keychron (https://www.keychron.com)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "quantum.h"
#include "game_controller_common.h"
//#include "eeconfig.h"
#include "eeprom.h"
#include "nvm_eeprom_eeconfig_internal.h"
#include "analog_matrix.h"

uint8_t      game_controller_mode;
point_t      curve[CURVE_POINTS_COUNT];
float        slope[CURVE_POINTS_COUNT - 1];
matrix_row_t game_controller_matrix[MATRIX_ROWS] = {0};

void game_controller_curve_init(point_t *pt) {
    // Copy the curve points from input
    memcpy(curve, pt, CURVE_POINTS_COUNT * SIZE_OF_POINT_T);

    // Check if the curve has valid points or if it needs to be replaced with the default
    int all_zeroes = 1;
    for (int i = 0; i < CURVE_POINTS_COUNT; i++) {
        if (curve[i].y != 0) {
            all_zeroes = 0;
            break;
        }
    }

    // If all curve points are zero, load the default curve
    if (all_zeroes) {
        // Using the new x/y points and the default slope of 32f
        point_t default_curve[CURVE_POINTS_COUNT] = {
            {0,    0},    // Point 0
            {256,  8191}, // Point 1
            {767,  24575},// Point 2
            {1024, 32767} // Point 3
        };
        memcpy(curve, default_curve, sizeof(default_curve));
        slope[0] = slope[1] = slope[2] = 32.0f;
    }
}

void game_controller_mode_init(uint8_t mode) {
    game_controller_mode = mode;
}

bool game_controller_mode_get(uint8_t *data) {
    data[1] = game_controller_mode;
    return true;
}

bool game_controller_mode_set(uint8_t mode) {
    if (game_controller_mode == mode) return false;

    game_controller_mode = mode;

    // Save
    if (!eeconfig_is_kb_datablock_valid()) eeprom_update_dword(EECONFIG_KEYBOARD, (EECONFIG_KB_DATA_VERSION));

    analog_matrix_eeprom_update(&game_controller_mode, (uint8_t *)OFFSET_GAME_CONTROLLER_MODE_START, 1);
    // usbDisconnectBus(&USBD1);
    // usbStop(&USBD1);
    // wait_ms(1000);
    // usb_start(&USBD1);

    return true;
}

bool game_controller_set_curve(uint8_t *raw) {
    point_t temp[CURVE_POINTS_COUNT];

    for (int i = 0; i < CURVE_POINTS_COUNT; i++) {
        uint16_t x = (uint16_t)raw[i * 4 + 0] | ((uint16_t)raw[i * 4 + 1] << 8);
        uint16_t y = (uint16_t)raw[i * 4 + 2] | ((uint16_t)raw[i * 4 + 3] << 8);
        temp[i].x = x;
        temp[i].y = y;
    }

    // Check X monotonicity
    if (temp[0].x > temp[1].x || temp[1].x > temp[2].x || temp[2].x > temp[3].x) {
        return false;
    }

    memcpy(curve, temp, sizeof(temp));

    for (int i = 0; i < CURVE_POINTS_COUNT - 1; i++) {
        float dx = (float)(curve[i + 1].x - curve[i].x);
        float dy = (float)(curve[i + 1].y - curve[i].y);
        slope[i] = (dx != 0.0f) ? (dy / dx) : 0.0f;
    }
    analog_matrix_eeprom_update(curve, (uint8_t *)OFFSET_CURVE_PTS_START, CURVE_POINTS_COUNT * SIZE_OF_POINT_T);

    return true;
}

bool game_controller_get_curve(uint8_t *data) {
    // Encode 16-bit x/y pairs in little-endian order
    for (int i = 0; i < CURVE_POINTS_COUNT; i++) {
        data[i * 4 + 0] = (uint8_t)(curve[i].x & 0xFF);
        data[i * 4 + 1] = (uint8_t)((curve[i].x >> 8) & 0xFF);
        data[i * 4 + 2] = (uint8_t)(curve[i].y & 0xFF);
        data[i * 4 + 3] = (uint8_t)((curve[i].y >> 8) & 0xFF);
    }
    return true;
}

bool game_controller_xinput_enabled(void) {
    return game_controller_mode & GC_MASK_XINPUT;
}

bool game_controller_type_enabled(void) {
    return game_controller_mode & GC_MASK_TYPING;
}

void game_controller_clear(void) {
    memset(game_controller_matrix, 0, sizeof(game_controller_matrix));

#ifdef JOYSTICK_ENABLE
#    ifdef XINPUT_ENABLE
    if (!game_controller_xinput_enabled())
#    endif
    {
        extern void joystick_clear(void);
        joystick_clear();
    }
#endif
#ifdef XINPUT_ENABLE
#    ifdef JOYSTICK_ENABLE
    if (game_controller_xinput_enabled())
#    endif
    {
        extern void xinput_clear(void);
        xinput_clear();
    }
#endif
}
