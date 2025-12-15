/* Copyright 2024 ~ 2025 @ Keychron (https://www.keychron.com)
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

#include QMK_KEYBOARD_H
#include "keychron_common.h"
#include "keychron_raw_hid.h"
#include "raw_hid.h"
#include "version.h"
#include "language.h"
#include "quantum.h"
#include "via.h"         // for id_unhandled
#include "analog_hid.h"
#ifdef FACTORY_TEST_ENABLE
#    include "factory_test.h"
#endif
#ifdef ANANLOG_MATRIX
#    include "analog_matrix.h"
#endif
#ifdef DYNAMIC_DEBOUNCE_ENABLE
#    include "keychron_debounce.h"
#endif
#ifdef SNAP_CLICK_ENABLE
#    include "snap_click.h"
#endif

#ifdef RAW_ENABLE
extern void dfu_info_rx(uint8_t *data, uint8_t length);
extern void nkro_rx(uint8_t *data, uint8_t length);

void get_support_feature(uint8_t *data) {
    data[0] = 0;
    data[1] = FEATURE_DEFAULT_LAYER
#    ifdef ANANLOG_MATRIX
              | FEATURE_ANALOG_MATRIX
#    endif
#    ifdef STATE_NOTIFY_ENABLE
              | FEATURE_STATE_NOTIFY
#    endif
#    ifdef DYNAMIC_DEBOUNCE_ENABLE
              | FEATURE_DYNAMIC_DEBOUNCE
#    endif
#    if defined(SNAP_CLICK_ENABLE) && !defined(ANANLOG_MATRIX)
              | FEATURE_SNAP_CLICK
#    endif
#    ifdef KEYCHRON_RGB_ENABLE
              | FEATURE_KEYCHRON_RGB
#    endif
        ;

    data[2] = (FEATURE_QUICK_START | FEATURE_NKRO) >> 8;
}

void get_firmware_version(uint8_t *data) {
    uint8_t i = 0;
    data[i++] = 'v';
    if ((DEVICE_VER & 0xF000) != 0) itoa((DEVICE_VER >> 12), (char *)&data[i++], 16);
    itoa((DEVICE_VER >> 8) & 0xF, (char *)&data[i++], 16);
    data[i++] = '.';
    itoa((DEVICE_VER >> 4) & 0xF, (char *)&data[i++], 16);
    data[i++] = '.';
    itoa(DEVICE_VER & 0xF, (char *)&data[i++], 16);
    data[i++] = ' ';
    memcpy(&data[i], QMK_BUILDDATE, sizeof(QMK_BUILDDATE));
    i += sizeof(QMK_BUILDDATE);
}

void kc_raw_hid_send(uint8_t src, uint8_t *data, uint8_t len) {
    if (src == RAW_HID_SRC_USB) {
        extern host_driver_t chibios_driver;
        chibios_driver.send_raw_hid(data, len);
    }
}

bool kc_raw_hid_rx(uint8_t src, uint8_t *data, uint8_t length) {
    switch (data[0]) {
#    ifdef VIA_ENABLE
        case id_get_protocol_version:
            if (src != RAW_HID_SRC_USB) {
                data[3] = VENDOR_ID >> 8;
                data[4] = VENDOR_ID & 0xFF;
                data[5] = PRODUCT_ID >> 8;
                data[6] = PRODUCT_ID & 0xFF;
                data[7] = DEVICE_VER >> 8;
                data[8] = DEVICE_VER & 0xFF;
            }
            return false;
#    endif
        case KC_GET_PROTOCOL_VERSION:
            data[1] = PROTOCOL_VERSION;
            data[2] = 0;
            data[3] = QMK_COMMAND_SET;
            break;

        case KC_GET_FIRMWARE_VERSION:
            get_firmware_version(&data[1]);
            break;

        case KC_GET_SUPPORT_FEATURE:
            get_support_feature(&data[1]);
            break;

        case KC_GET_DEFAULT_LAYER:
            data[1] = get_highest_layer(default_layer_state);
            break;

        case KC_MISC_CMD_GROUP:
            switch (data[1]) {
                case MISC_GET_PROTOCOL_VER:
                    data[2] = 0;
                    data[3] = MISC_PROTOCOL_VERSION & 0xFF;
                    data[4] = (MISC_PROTOCOL_VERSION >> 8) & 0xFF;
                    data[5] = MISC_DFU_INFO | MISC_LANGUAGE
#    ifdef DYNAMIC_DEBOUNCE_ENABLE
                              | MISC_DEBOUNCE
#    endif
#    if defined(SNAP_CLICK_ENABLE) && !defined(ANANLOG_MATRIX)
                              | MISC_SNAP_CLICK
#    endif
#    ifdef USB_REPORT_INTERVAL_ENABLE
                              | MISC_REPORT_REATE
#    endif
                              | MISC_QUICK_START
#    ifdef NKRO_ENABLE
                              | MISC_NKRO
#    endif
                        ;
                    break;

                case DFU_INFO_GET:
                    dfu_info_rx(data, length);
                    break;

                case LANGUAGE_GET ... LANGUAGE_SET:
                    language_rx(data, length);
                    break;

#    if defined(DYNAMIC_DEBOUNCE_ENABLE)
                case DEBOUNCE_GET ... DEBOUNCE_SET:
                    debounce_rx(data, length);
                    break;
#    endif
#    if defined(SNAP_CLICK_ENABLE) && !defined(ANANLOG_MATRIX)
                case SNAP_CLICK_GET_INFO ... SNAP_CLICK_SAVE:
                    snap_click_rx(data, length);
                    break;
#    endif
#    if defined(USB_REPORT_INTERVAL_ENABLE)
                case REPORT_RATE_GET ... REPORT_RATE_SET: {
                    extern void report_rate_hid_rx(uint8_t * data, uint8_t length);
                    report_rate_hid_rx(data, length);
                } break;
#    endif
                case NKRO_GET ... NKRO_SET:
                    nkro_rx(data, length);
                    break;

                default:
                    data[0] = 0xFF;
                    data[1] = 0x00;
                    break;
            }
            break;

#    if defined(KEYCHRON_RGB_ENABLE)
        case 0xA8: {
            extern void kc_rgb_matrix_rx(bool usb, uint8_t *data, uint8_t length);
            kc_rgb_matrix_rx(src == RAW_HID_SRC_USB, data, length);
        } break;
#    endif

#    ifdef FACTORY_TEST_ENABLE
        case 0xAB:
            factory_test_rx(src == RAW_HID_SRC_USB, data, length);
            return true;

#    endif
        default:
            return false;
    }

    kc_raw_hid_send(src, data, length);
    return true;
}

#    if defined(VIA_ENABLE)
bool via_command_kb(uint8_t src, uint8_t *data, uint8_t length) {
    // Intercept custom command
    if (data[0] == CUSTOM_HID_CMD) {
        custom_hid_receive(data, length);
        return true;   // stop VIA from processing it
    }
    return kc_raw_hid_rx(src, data, length);
}

/* Override default via_raw_hid_send implement */
void via_raw_hid_send(uint8_t src, uint8_t *data, uint8_t len) {
    kc_raw_hid_send(src, data, len);
}
#    else
void raw_hid_receive(uint8_t src, uint8_t *data, uint8_t length) {
    kc_raw_hid_rx(src, data, length);
}
#    endif
#endif

void raw_hid_receive_kb(uint8_t *data, uint8_t length) {
    // Basic sanity
    if (length == 0) {
        data[0] = id_unhandled;
        return;
    }

    // If our custom prefix is present, forward to the custom handler:
    if (data[0] == CUSTOM_HID_CMD) {
        // custom_hid_receive already expects the same data/length semantics:
        // it will strip the prefix if necessary and send a response with raw_hid_send().
        custom_hid_receive(data, length);
        return;
    }

    // Not handled here — return unhandled so via.c returns id_unhandled to host.
    data[0] = id_unhandled;
}

