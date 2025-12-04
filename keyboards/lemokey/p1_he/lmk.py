#!/usr/bin/env python3
"""
lmk.py - Short, positional-arg CLI for QMK custom HID commands.

Usage examples (positional-only):
  lmk ping
  lmk get-profile
  lmk set-profile 0/1/2
  lmk get-profiles-info
  lmk cycle-profile
  lmk save-profile 0/1/2
  lmk reset-profile 0/1/2
  lmk set-advance-mode <profile index> <mode> <row> <col> <index>
  lmk set-traval <profile 0-3> <mode0-4> <act_pt> <sens> <rls_sens> <entire 0/1> <row> <col> #act_pt/sens/rls_sens 0-1023
  lmk set-act 0 1 300                # row/col/set actuation (0-1023)
  lmk set-rapid 0 1 10 20            # row/col/press/release (0-1023)
  lmk set-socd-last r1 c1 r2 c2 s    # row1/col1/row2/col2/slot 0-20
  lmk set-socd-snap r1 c1 r2 c2 s 1  # last arg 1 = single activation, 0 = not
  lmk set-socd-neutral r1 c1 r2 c2 s
  lmk set-socd-pri1 r1 c1 r2 c2 s
  lmk set-socd-pri2 r1 c1 r2 c2 s
  lmk set-okmc row col idx shallow_act shallow_deact deep_act deep_deact key1 key2 key3 key4 a1 a2 a3 a4
  lmk set-toggle row col
  lmk set-gamepad row col xinput_code
  lmk get-gamecontroller-mode
  lmk set-gamecontroller-mode 0/1/2/3
  lmk get-curve
  lmk set-curve x1 y1 x2 y2 x3 y3 x4 y4  (X 0-1023 / Y 0-32767)
  lmk calib-start
  lmk calib-clear #not yet implemented
  lmk get-calib-state #returns 0 when not in calib mode
  lmk get-key-calib row col
  lmk get-firmware-info

All numeric values that represent travel/X curve/rapid/actuation use 10-bit range 0..1023.
"""
import sys
import time
import struct

try:
    import hid
except Exception as e:
    print("Python package 'hid' (hidapi) is required. Install with: pip install hidapi")
    sys.exit(1)

# Device identifiers - update if your keyboard uses different IDs.
VENDOR_ID  = 0x362D #Lemokey, different for Keychron
PRODUCT_ID = 0x0611 #P1 HE, different for other models
USAGE_PAGE = 0xFF60 #do not change unless you know what you're doing
USAGE      = 0x61   #same
REPORT_LEN = 32     #do not change

MATRIX_ROWS = 6  # Total number of rows in your matrix
MATRIX_COLS = 15  # Total number of columns in your matrix

CUSTOM_HID_CMD = 0x23

# --- OPCODES (match firmware) ---
HID_CMD_PING                     = 0x10
HID_CMD_GET_PROFILES_INFO        = 0x11
HID_CMD_GET_HE_PROFILE           = 0x12
HID_CMD_SWITCH_HE_PROFILE        = 0x13
HID_CMD_CYCLE_HE_PROFILE         = 0x14
HID_CMD_SAVE_PROFILE             = 0x15
HID_CMD_RESET_PROFILE            = 0x16
HID_CMD_SET_ADVANCE_MODE         = 0x17
HID_CMD_SET_TRAVAL               = 0x18

HID_CMD_SET_KEY_ACT_PT           = 0x20
HID_CMD_SET_KEY_RAPID            = 0x21
HID_CMD_SET_SOCD_LAST            = 0x22
HID_CMD_SET_SOCD_SNAP            = 0x23
HID_CMD_SET_SOCD_NEUTRAL         = 0x24
HID_CMD_SET_SOCD_PRI1            = 0x25
HID_CMD_SET_SOCD_PRI2            = 0x26
HID_CMD_SET_OKMC                 = 0x27
HID_CMD_SET_TOGGLE               = 0x28

HID_CMD_SET_GAMEPAD              = 0x30
HID_CMD_GET_CURVE                = 0x31
HID_CMD_SET_CURVE                = 0x32
HID_CMD_GET_GAME_CONTROLLER_MODE = 0x33
HID_CMD_SET_GAME_CONTROLLER_MODE = 0x34

HID_CMD_START_CALIB              = 0x40
HID_CMD_CLEAR_CALIB              = 0x41
HID_CMD_GET_CALIB_STATE          = 0x42
HID_CMD_GET_KEY_CALIB            = 0x43

HID_CMD_GET_XINPUT               = 0x50
HID_CMD_GET_TYPE                 = 0x51

HID_CMD_GET_FIRMWARE_INFO        = 0x60
# ---------------------------
# Device helpers
# ---------------------------

XINPUT_KEYCODES = {
    "XB_A": 0x01B0,
    "XB_B": 0x01D0,
    "XB_X": 0x01F0,
    "XB_Y": 0x0210,
    "XB_LB": 0x0230,
    "XB_RB": 0x0250,
    "XB_VIEW": 0x0270,
    "XB_MEMU": 0x0290,
    "XB_L3": 0x02B0,
    "XB_R3": 0x02D0,
    "XB_UP": 0x02F0,
    "XB_DOWN": 0x0310,
    "XB_LEFT": 0x0330,
    "XB_RGHT": 0x0350,
    "XB_XBOX": 0x0370,

    "XB_LT": 0x0090,
    "XB_RT": 0x00B0,

    "LS_LEFT": 0x0010,
    "LS_RGHT": 0x0030,
    "LS_UP": 0x0070,
    "LS_DOWN": 0x0050,

    "RS_LEFT": 0x00D0,
    "RS_RGHT": 0x00F0,
    "RS_UP": 0x0130,
    "RS_DOWN": 0x0110,
}

def parse_xinput_keycode(xinput_name):
    """ Convert a human-readable XInput key name to its numeric value. """
    # Debugging: Print the input to see what exactly is being passed
    print(f"Parsing XInput keycode for: {xinput_name}")

    # Normalize input to avoid any issues with whitespace or case
    xinput_name = xinput_name.strip().upper()

    # Check if the key exists in the dictionary and print out the result
    keycode = XINPUT_KEYCODES.get(xinput_name)
    if keycode is None:
        raise ValueError(f"Invalid XInput keycode name: {xinput_name}")
    return keycode


def find_raw_hid():
    for d in hid.enumerate(VENDOR_ID, PRODUCT_ID):
        if d.get('usage_page') == USAGE_PAGE and d.get('usage') == USAGE:
            return d['path']
    return None

def open_device():
    path = find_raw_hid()
    if not path:
        return None
    return hid.Device(path=path)

def drain(dev, timeout_ms=5):
    """Drain any pending input reports."""
    while True:
        try:
            r = dev.read(REPORT_LEN, timeout_ms)
        except Exception:
            break
        if not r:
            break

def _strip_report_id(bts):
    if not bts:
        return bts
    if len(bts) == REPORT_LEN + 1 and bts[0] == 0x00:
        return bts[1:]
    return bts

def _safe_write(dev, packet):
    """Try writing with report-id prefix then fallback to bare packet."""
    try:
        prefixed = bytes([0x00]) + packet
        dev.write(prefixed)
        return True
    except Exception:
        try:
            dev.write(packet)
            return True
        except Exception:
            return False

def send_report(dev, data, timeout_ms=1000):
    original = bytes(data)
    data = bytes([CUSTOM_HID_CMD]) + original

    if len(data) > REPORT_LEN:
        raise ValueError("Payload too long")

    packet = data + bytes(REPORT_LEN - len(data))

    try:
        drain(dev)
    except Exception:
        pass

    ok = _safe_write(dev, packet)
    if not ok:
        raise RuntimeError("Failed to write to HID device")

    time.sleep(0.02)

    try:
        resp = dev.read(REPORT_LEN + 1, timeout_ms)
    except Exception:
        return None

    if not resp:
        return None

    resp = bytes(resp)
    stripped = _strip_report_id(resp)

    if stripped and stripped[0] == CUSTOM_HID_CMD:
        return stripped[1:]

    return stripped



# ---------------------------
# Low-level helpers
# ---------------------------

def _ok_from_status_byte(b):
    if b is None:
        return False
    if isinstance(b, int) and b in (0x01, 0xAA, 0x00):
        return True
    return False

# Low-level send functions
def cmd_ping(dev): return send_report(dev, bytes([HID_CMD_PING]))
def cmd_switch_he_profile(dev, idx): return send_report(dev, bytes([HID_CMD_SWITCH_HE_PROFILE, idx]))
def cmd_cycle_he_profile(dev): return send_report(dev, bytes([HID_CMD_CYCLE_HE_PROFILE]))
def cmd_get_he_profile(dev): return send_report(dev, bytes([HID_CMD_GET_HE_PROFILE]))
def cmd_get_profiles_info(dev): return send_report(dev, bytes([HID_CMD_GET_PROFILES_INFO]))
def cmd_save_profile(dev, prof_index):
    payload = bytearray([HID_CMD_SAVE_PROFILE, prof_index])
    return send_report(dev, bytes(payload))
def cmd_reset_profile(dev, prof_index):
    payload = bytearray([HID_CMD_RESET_PROFILE, prof_index])
    return send_report(dev, bytes(payload))
def cmd_set_advance_mode(dev, prof_idx, mode, row, col, index):
    payload = bytearray([HID_CMD_SET_ADVANCE_MODE, prof_idx, mode, row, col, index])
    return send_report(dev, bytes(payload))
def cmd_set_traval(dev, profile, mode, act_pt, sens, rls_sens, entire, row=None, col=None):
    payload = bytearray([24, 0, profile, mode])  # Command, reserved byte, profile, mode

    # Add 16-bit values for actuation point, sensitivity, and release sensitivity
    payload += struct.pack('<H', act_pt)  # Actuation point
    payload += struct.pack('<H', sens)    # Sensitivity
    payload += struct.pack('<H', rls_sens)  # Release sensitivity

    # Add the 'entire' flag (1 byte)
    payload.append(1 if entire else 0)

    # If 'entire' is False, add row and column
    if not entire:
        if row is None or col is None:
            raise ValueError("When 'entire' is 0, you must specify row and col.")
        payload.append(row)  # Add row
        payload.append(col)  # Add column
    else:
        # If entire is True, use a bitmask for the entire matrix
        # Row mask is all rows, so the bitmask will be set accordingly
        row_mask = [0xFFFFFFFF] * MATRIX_ROWS  # All rows are affected

        # Convert the row_mask to bytes (this needs to match the size expected by QMK)
        for i in range(MATRIX_ROWS):
            payload.append(row_mask[i] & 0xFF)  # Lower 8 bits of the mask for each row
            payload.append((row_mask[i] >> 8) & 0xFF)  # Upper 8 bits of the mask for each row

    # Send the payload to the device
    return send_report(dev, bytes(payload))

def cmd_set_key_act_pt(dev, row, col, act_pt):
    payload = bytearray([HID_CMD_SET_KEY_ACT_PT, row, col])
    payload += struct.pack('<H', act_pt)  # 16-bit
    return send_report(dev, bytes(payload))

def cmd_set_key_rapid(dev, row, col, press, release):
    payload = bytearray([HID_CMD_SET_KEY_RAPID, row, col])
    payload += struct.pack('<H', press)
    payload += struct.pack('<H', release)
    return send_report(dev, bytes(payload))

def cmd_set_socd_last(dev, r1,c1,r2,c2,slot): return send_report(dev, bytes([HID_CMD_SET_SOCD_LAST, r1,c1,r2,c2,slot]))
def cmd_set_socd_snap(dev, r1,c1,r2,c2,slot,single): return send_report(dev, bytes([HID_CMD_SET_SOCD_SNAP, r1,c1,r2,c2,slot, 1 if single else 0]))
def cmd_set_socd_neutral(dev, r1,c1,r2,c2,slot): return send_report(dev, bytes([HID_CMD_SET_SOCD_NEUTRAL, r1,c1,r2,c2,slot]))
def cmd_set_socd_pri1(dev, r1,c1,r2,c2,slot): return send_report(dev, bytes([HID_CMD_SET_SOCD_PRI1, r1,c1,r2,c2,slot]))
def cmd_set_socd_pri2(dev, r1,c1,r2,c2,slot): return send_report(dev, bytes([HID_CMD_SET_SOCD_PRI2, r1,c1,r2,c2,slot]))

def cmd_set_okmc(dev, row, col, idx,
                 shallow_act, shallow_deact, deep_act, deep_deact,
                 key1, key2, key3, key4,
                 a1, a2, a3, a4):

    payload = bytearray([HID_CMD_SET_OKMC, row, col, idx])

    payload += struct.pack('<H', shallow_act)
    payload += struct.pack('<H', shallow_deact)
    payload += struct.pack('<H', deep_act)
    payload += struct.pack('<H', deep_deact)

    payload += struct.pack('<H', key1)
    payload += struct.pack('<H', key2)
    payload += struct.pack('<H', key3)
    payload += struct.pack('<H', key4)

    payload += bytes([a1, a2, a3, a4])

    # YOU DO NOT PAD HERE — send_report pads to 32 already
    return send_report(dev, bytes(payload))


def cmd_set_toggle(dev, row, col): return send_report(dev, bytes([HID_CMD_SET_TOGGLE, row, col]))
def cmd_get_game_controller_mode(dev):
    return send_report(dev, bytes([HID_CMD_GET_GAME_CONTROLLER_MODE]))
def cmd_get_xinput(dev):
    return send_report(dev, bytes([HID_CMD_GET_XINPUT]))
def cmd_get_type(dev):
    return send_report(dev, bytes([HID_CMD_GET_TYPE]))
def cmd_set_game_controller_mode(dev, mode):
    """
    Sends HID_CMD_SET_GAME_CONTROLLER_MODE command to set the game controller mode.
    Arguments:
    dev -- the HID device object
    mode -- the game controller mode (0 for success, 1 for failure)
    Returns:
    response (bytes) from the device
    """
    # Prepare the data to send: [HID_CMD_SET_GAME_CONTROLLER_MODE, mode]
    data = [
        HID_CMD_SET_GAME_CONTROLLER_MODE,  # Command byte
        0,  # Padding or reserved byte (data[0] could be used for other purposes)
        mode  # Game controller mode (0 = success, 1 = failure)
    ]

    # Send the data to the device
    return send_report(dev, bytes(data))
def cmd_set_gamepad(dev, row, col, xinput_value):
    # xinput_value is ALREADY numeric here — DO NOT parse again!
    payload = bytearray([HID_CMD_SET_GAMEPAD, row, col])
    payload += struct.pack('<H', xinput_value)
    return send_report(dev, bytes(payload))

def cmd_get_curve(dev):
    """Send the HID_CMD_GET_CURVE command to the device."""
    payload = bytearray([HID_CMD_GET_CURVE])
    return send_report(dev, bytes(payload))

def cmd_set_curve(dev, pts):
    payload = bytearray([HID_CMD_SET_CURVE])

    # Each point is packed as 2 bytes (16-bit)
    for pt in pts:
        payload += struct.pack('<H', pt)  # Ensure 16-bit packing

    return send_report(dev, bytes(payload))


def cmd_start_calib(dev): return send_report(dev, bytes([HID_CMD_START_CALIB]))
def cmd_clear_calib(dev): return send_report(dev, bytes([HID_CMD_CLEAR_CALIB]))
def cmd_get_calib_state(dev): return send_report(dev, bytes([HID_CMD_GET_CALIB_STATE]))
def cmd_get_key_calib(dev, row, col): return send_report(dev, bytes([HID_CMD_GET_KEY_CALIB, row, col]))
def cmd_get_firmware_info(dev): return send_report(dev, bytes([HID_CMD_GET_FIRMWARE_INFO]))

# ---------------------------
# Pretty wrapper: print short human-readable messages (D-1 + values when present)
# ---------------------------

def pretty_ping(dev):
    r = cmd_ping(dev)
    if r and len(r) >= 2 and _ok_from_status_byte(r[1]):
        print("Ping: OK")
    else:
        print("Ping: Error")

def pretty_get_profiles_info(dev):
    r = cmd_get_profiles_info(dev)
    if r and len(r) >= 8:
        current_profile_index = r[2]
        profile_count = r[3]
        profile_size = r[4] | (r[5] << 8)  # Combine low and high byte
        okmc_count = r[6]
        socd_count = r[7]
        print(f"Profiles Info:")
        print(f"  Current Profile Index: {current_profile_index}")
        print(f"  Profile Count: {profile_count}")
        print(f"  Profile Size: {profile_size}")
        print(f"  OKMC Count: {okmc_count}")
        print(f"  SOCD Count: {socd_count}")
    else:
        print("Get profiles info: Error")

def pretty_get_profile(dev):
    r = cmd_get_he_profile(dev)
    if r and len(r) >= 3 and r[1] == 0x01:
        print(f"Profile: {r[2]}")
        return
    if r and len(r) >= 2 and _ok_from_status_byte(r[1]):
        print("Profile: Unknown")
        return
    print("Profile: Error")

def pretty_set_profile(dev, idx):
    r = cmd_switch_he_profile(dev, idx)
    if r and len(r) >= 2 and _ok_from_status_byte(r[1]):
        print("Set profile: OK")
    else:
        print("Set profile: Error")

def pretty_cycle_profile(dev):
    r = cmd_cycle_he_profile(dev)
    if r and len(r) >= 2 and _ok_from_status_byte(r[1]):
        print("Cycle profile: OK")
    else:
        print("Cycle profile: Error")

def pretty_save_profile(dev, prof_index):
    r = cmd_save_profile(dev, prof_index)
    if r and len(r) >= 2:
        if r[1] == 0:
            print(f"Profile {prof_index} saved successfully.")
        else:
            print(f"Failed to save profile {prof_index}.")
    else:
        print("Save profile: Error")

def pretty_reset_profile(dev, prof_index):
    r = cmd_reset_profile(dev, prof_index)
    if r and len(r) >= 2:
        if r[1] == 0:
            print(f"Profile {prof_index} reset successfully.")
        else:
            print(f"Failed to reset profile {prof_index}.")
    else:
        print("Reset profile: Error")

def pretty_set_advance_mode(dev, prof_idx, mode, row, col, index):
    r = cmd_set_advance_mode(dev, prof_idx, mode, row, col, index)
    if r and len(r) >= 2:
        if r[1] == 0:
            print(f"Advanced mode set successfully for profile {prof_idx} at row {row}, col {col}.")
        else:
            print(f"Failed to set advanced mode for profile {prof_idx} at row {row}, col {col}.")
    else:
        print("Set advanced mode: Error")

def pretty_set_traval(dev, profile, mode, act_pt, sens, rls_sens, entire, row_col=None):
    # Default to None if no row_col is passed
    row, col = None, None

    # Check if row_col is provided and unpack
    if row_col:
        row, col = parse_row_col(row_col)

    print(f"Preparing to set travel for profile {profile} with:")
    print(f"Mode: {mode}, Actuation Point: {act_pt}, Sensitivity: {sens}, Release Sensitivity: {rls_sens}, Entire: {entire}")
    print(f"Row: {row}, Col: {col}")  # This should show which key if entire=0

    # Call cmd_set_traval without passing row and col as separate arguments
    r = cmd_set_traval(dev, profile, mode, act_pt, sens, rls_sens, entire, row, col)

    # Handle response
    if r and len(r) >= 2:
        if r[1] == 0:
            print(f"Travel settings updated successfully for profile {profile}.")
        else:
            print(f"Failed to update travel settings for profile {profile}.")
    else:
        print("Set travel: Error")


def pretty_set_act(dev, row, col, act):
    r = cmd_set_key_act_pt(dev, row, col, act)
    if r and len(r) >= 2 and _ok_from_status_byte(r[1]):
        print(f"Set actuation point ({row},{col}): OK")
    else:
        print(f"Set actuation point ({row},{col}): Error")

def pretty_set_rapid(dev, row, col, press, release):
    r = cmd_set_key_rapid(dev, row, col, press, release)
    if r and len(r) >= 2 and _ok_from_status_byte(r[1]):
        print(f"Set rapid trigger ({row},{col}): OK")
    else:
        print(f"Set rapid trigger ({row},{col}): Error")

def pretty_socd_simple(fn, dev, *args):
    r = fn(dev, *args)
    if r and len(r) >= 2 and _ok_from_status_byte(r[1]):
        print("Set SOCD: OK")
    else:
        print("Set SOCD: Error")

def pretty_set_okmc(dev, args):
    r = cmd_set_okmc(dev, *args)
    if r and len(r) >= 2 and _ok_from_status_byte(r[1]):
        print("Set OKMC: OK")
    else:
        print("Set OKMC: Error")

def pretty_set_toggle(dev, row, col):
    r = cmd_set_toggle(dev, row, col)
    if r and len(r) >= 2 and _ok_from_status_byte(r[1]):
        print(f"Set toggle ({row},{col}): OK")
    else:
        print(f"Set toggle ({row},{col}): Error")
def pretty_get_game_controller_mode(dev):
    r = cmd_get_game_controller_mode(dev)
    if r and len(r) >= 3:
        mode = r[1]  # The second byte contains the actual game controller mode (0 or 1)
        status = r[2]  # The third byte contains the status (0 for success, 1 for failure)

        # Check if the status is 0 (success) or 1 (failure)
        if status == 0:
            print(f"Game controller mode: {mode}")  # Display the actual mode (0 or 1)
        else:
            print("Failed to retrieve game controller mode.")
    else:
        print("Error: Invalid response from device when retrieving game controller mode.")
def pretty_get_xinput_mode(dev):
    r = cmd_get_xinput(dev)
    if r and len(r) >= 3:
        mode = r[1]  # The second byte contains the actual game controller mode (0 or 1)
        status = r[2]  # The third byte contains the status (0 for success, 1 for failure)

        # Check if the status is 0 (success) or 1 (failure)
        if status == 0:
            print(f"xinput mode: {mode}")  # Display the actual mode (0 or 1)
        else:
            print("Failed to retrieve xinput controller mode.")
    else:
        print("Error: Invalid response from device when retrieving xinput mode.")
def pretty_get_type(dev):
    r = cmd_get_type(dev)
    if r and len(r) >= 3:
        mode = r[1]  # The second byte contains the actual game controller mode (0 or 1)
        status = r[2]  # The third byte contains the status (0 for success, 1 for failure)

        # Check if the status is 0 (success) or 1 (failure)
        if status == 0:
            print(f"type enable: {mode}")  # Display the actual mode (0 or 1)
        else:
            print("Failed to retrieve type enable status.")
    else:
        print("Error: Invalid response from device when retrieving type status.")
def pretty_set_game_controller_mode(dev, mode):
    r = cmd_set_game_controller_mode(dev, mode)

    if r:
        if len(r) >= 2:
            status = r[1]
            if status == 0x00:  # Mode changed successfully
                print("Set Game Controller Mode: Success")
            elif status == 0x01:  # Mode already set (no change needed)
                print("Set Game Controller Mode: No change needed (Success)")
            else:
                print(f"Unexpected response: {r[1]}")
        else:
            print("Error: Invalid response length.")
    else:
        print("Failed to send game controller mode command.")

def pretty_set_gamepad(dev, row, col, xinput):
    r = cmd_set_gamepad(dev, row, col, xinput)
    if r and len(r) >= 2 and _ok_from_status_byte(r[1]):
        print("Set gamepad map: OK")
    else:
        print("Set gamepad map: Error")
import struct

def pretty_get_curve(dev):
    """Wrapper function to display the result for get-curve."""
    r = cmd_get_curve(dev)
    if r and len(r) >= 2 and _ok_from_status_byte(r[1]):
        # Extract curve data from response
        curve_data = r[2:]  # The rest of the response contains the curve points
        print("Curve data received:")

        # Calculate the number of points (4 bytes per point)
        num_points = len(curve_data) // 4  # Each point has 4 bytes (2 for x, 2 for y)

        # Only show the first four points
        for i in range(min(num_points, 4)):
            # Unpack the x and y values (2 bytes for each in little-endian)
            x = struct.unpack('<H', curve_data[i * 4:i * 4 + 2])[0]
            y = struct.unpack('<H', curve_data[i * 4 + 2:i * 4 + 4])[0]

            # Print the point
            print(f"X/Y {i + 1}: {x} {y}")
    else:
        print("Failed to get curve data.")

def pretty_set_curve(dev, pts):
    r = cmd_set_curve(dev, pts)
    if r and len(r) >= 2 and _ok_from_status_byte(r[1]):
        print("Set curve: OK")
    else:
        print("Set curve: Error")

def pretty_start_calib(dev):
    r = cmd_start_calib(dev)
    if r and len(r) >= 2 and _ok_from_status_byte(r[1]):
        print("Start calibration: OK")
    else:
        print("Start calibration: Error")

def pretty_clear_calib(dev):
    r = cmd_clear_calib(dev)
    if r and len(r) >= 2 and _ok_from_status_byte(r[1]):
        print("Clear calibration: OK")
    else:
        print("Clear calibration: Error")

def pretty_get_calib_state(dev):
    r = cmd_get_calib_state(dev)
    if r and len(r) >= 2:
        print(f"Calibration state: {r[1]}")
    else:
        print("Get calibration state: Error")

def pretty_get_key_calib(dev, row, col):
    r = cmd_get_key_calib(dev, row, col)

    # Expecting: [cmd, 1, travel_lo, travel_hi, zero_lo, zero_hi, full_lo, full_hi]
    if r and len(r) >= 8 and r[1] == 1:
        travel = r[2] | (r[3] << 8)
        zero   = r[4] | (r[5] << 8)
        full   = r[6] | (r[7] << 8)

        print(f"Key ({row},{col}) calibration data: travel={travel}  zero={zero}  full={full}")
    else:
        print(f"Get key calibration ({row},{col}): Error")

def pretty_get_firmware(dev):
    r = cmd_get_firmware_info(dev)
    if r and len(r) >= 2 and _ok_from_status_byte(r[1]):
        print("Firmware: Present")
    else:
        print("Firmware: Not available")

# ---------------------------
# CLI dispatch (positional-only)
# ---------------------------
def parse_int(s, name=None, minv=None, maxv=None):
    try:
        v = int(s, 0)
    except Exception:
        raise ValueError(f"Invalid integer for {name or 'value'}: {s}")
    if minv is not None and v < minv:
        raise ValueError(f"{name or 'value'} too small (min {minv})")
    if maxv is not None and v > maxv:
        raise ValueError(f"{name or 'value'} too large (max {maxv})")
    return v
def parse_row_col(row_col_str):
    # Check if row_col_str is already a tuple, if so, just return it.
    if isinstance(row_col_str, tuple):
        return row_col_str

    # If it's a string, split it into row and col
    row, col = map(int, row_col_str.split(','))
    return row, col

def usage_and_exit():
    print("Usage: lmk <command> [positional args]")
    print("Run without args to see a short example.")
    sys.exit(1)

def main(argv):
    if len(argv) < 2:
        print("Example: lmk ping")
        print("Example: lmk set-act 0 1 300")
        usage_and_exit()

    cmd = argv[1].lower()
    dev = open_device()
    if dev is None:
        print("Could not find the keyboard. Make sure it is connected and Raw HID is enabled.")
        sys.exit(1)

    try:
        if cmd == "ping":
            pretty_ping(dev)
        elif cmd in ("get-profiles-info", "profiles-info"):
            pretty_get_profiles_info(dev)
        elif cmd in ("get-profile", "profile-get"):
            pretty_get_profile(dev)
        elif cmd in ("set-profile", "profile-set"):
            if len(argv) < 3: raise SystemExit("profile index missing")
            idx = parse_int(argv[2], "profile", 0, 255)
            pretty_set_profile(dev, idx)
        elif cmd in ("save-profile", "profile-save"):
            if len(argv) < 3:
                raise SystemExit("Profile index missing")
            prof_index = parse_int(argv[2], "profile index", 0, 255)
            pretty_save_profile(dev, prof_index)
        elif cmd in ("reset-profile", "profile-reset"):
            if len(argv) < 3:
                raise SystemExit("Profile index missing")
            prof_index = parse_int(argv[2], "profile index", 0, 255)
            pretty_reset_profile(dev, prof_index)
        elif cmd in ("cycle-profile", "profile-cycle"):
            pretty_cycle_profile(dev)
        elif cmd == "set-advance-mode":
            if len(argv) < 7:
                raise SystemExit("usage: lmk set-advance-mode prof_idx mode row col index")
            prof_idx = parse_int(argv[2], "profile index", 0, 255)
            mode = parse_int(argv[3], "mode", 0, 255)
            row = parse_int(argv[4], "row", 0, 255)
            col = parse_int(argv[5], "col", 0, 255)
            index = parse_int(argv[6], "index", 0, 255)
            pretty_set_advance_mode(dev, prof_idx, mode, row, col, index)

        elif cmd == "set-traval":
            if len(argv) < 8:
                raise SystemExit("usage: lmk set-traval profile mode act_pt sens rls_sens entire row col")

            profile = parse_int(argv[2], "profile", 0, 255)
            mode = parse_int(argv[3], "mode", 0, 255)
            act_pt = parse_int(argv[4], "actuation point", 0, 1023)
            sens = parse_int(argv[5], "sensitivity", 0, 1023)
            rls_sens = parse_int(argv[6], "release sensitivity", 0, 1023)
            entire = bool(parse_int(argv[7], "entire flag", 0, 1))

            row = col = None
            if not entire:
                if len(argv) < 10:
                    raise SystemExit("When 'entire' is 0, row and col must be specified")
                row = parse_int(argv[8], "row", 0, MATRIX_ROWS - 1)
                col = parse_int(argv[9], "col", 0, MATRIX_COLS - 1)

            pretty_set_traval(dev, profile, mode, act_pt, sens, rls_sens, entire, row_col=(row, col) if not entire else None)

        elif cmd == "set-act" or cmd == "act":
            if len(argv) < 5: raise SystemExit("usage: lmk set-act ROW COL VALUE")
            row = parse_int(argv[2], "row", 0, 255)
            col = parse_int(argv[3], "col", 0, 255)
            val = parse_int(argv[4], "actuation", 0, 1023)
            pretty_set_act(dev, row, col, val)
        elif cmd in ("set-rapid", "rapid"):
            if len(argv) < 6: raise SystemExit("usage: lmk set-rapid ROW COL PRESS RELEASE")
            row = parse_int(argv[2], "row", 0, 255)
            col = parse_int(argv[3], "col", 0, 255)
            press = parse_int(argv[4], "press", 0, 1023)
            release = parse_int(argv[5], "release", 0, 1023)
            pretty_set_rapid(dev, row, col, press, release)
        elif cmd == "set-socd-last":
            if len(argv) < 7: raise SystemExit("usage: lmk set-socd-last r1 c1 r2 c2 slot")
            args = [parse_int(x, None, 0, 255) for x in argv[2:7]]
            pretty_socd_simple(cmd_set_socd_last, dev, *args)
        elif cmd == "set-socd-snap":
            if len(argv) < 8: raise SystemExit("usage: lmk set-socd-snap r1 c1 r2 c2 slot single")
            args = [parse_int(x, None, 0, 255) for x in argv[2:7]]
            single = parse_int(argv[7], "single", 0, 1)
            pretty_socd_simple(cmd_set_socd_snap, dev, *args, bool(single))
        elif cmd == "set-socd-neutral":
            if len(argv) < 7: raise SystemExit("usage: lmk set-socd-neutral r1 c1 r2 c2 slot")
            args = [parse_int(x, None, 0, 255) for x in argv[2:7]]
            pretty_socd_simple(cmd_set_socd_neutral, dev, *args)
        elif cmd == "set-socd-pri1":
            if len(argv) < 7: raise SystemExit("usage: lmk set-socd-pri1 r1 c1 r2 c2 slot")
            args = [parse_int(x, None, 0, 255) for x in argv[2:7]]
            pretty_socd_simple(cmd_set_socd_pri1, dev, *args)
        elif cmd == "set-socd-pri2":
            if len(argv) < 7: raise SystemExit("usage: lmk set-socd-pri2 r1 c1 r2 c2 slot")
            args = [parse_int(x, None, 0, 255) for x in argv[2:7]]
            pretty_socd_simple(cmd_set_socd_pri2, dev, *args)
        elif cmd == "set-okmc":
            # exactly 15 parameters after the command
            if len(argv) != 17:
                raise SystemExit(
                    "usage: lmk set-okmc row col idx shallowA shallowD deepA deepD "
                    "key1 key2 key3 key4 a1 a2 a3 a4"
                )

            row       = parse_int(argv[2],  "row",       0, 255)
            col       = parse_int(argv[3],  "col",       0, 255)
            idx       = parse_int(argv[4],  "idx",       0, 255)

            shallowA  = parse_int(argv[5],  "shallowA",   0, 65535)
            shallowD  = parse_int(argv[6],  "shallowD",   0, 65535)
            deepA     = parse_int(argv[7],  "deepA",      0, 65535)
            deepD     = parse_int(argv[8],  "deepD",      0, 65535)

            # KEYCODES — 16-bit, MUST accept hex like 0x04, 0x7004, 0xE001, etc.
            key1 = parse_int(argv[9],  "key1", 0, 65535)
            key2 = parse_int(argv[10], "key2", 0, 65535)
            key3 = parse_int(argv[11], "key3", 0, 65535)
            key4 = parse_int(argv[12], "key4", 0, 65535)

            # ACTION BYTES — MUST be hex (0x01 etc)
            a1 = parse_int(argv[13], "a1", 0, 255)
            a2 = parse_int(argv[14], "a2", 0, 255)
            a3 = parse_int(argv[15], "a3", 0, 255)
            a4 = parse_int(argv[16], "a4", 0, 255)

            pretty_set_okmc(dev, [
                row, col, idx,
                shallowA, shallowD, deepA, deepD,
                key1, key2, key3, key4,
                a1, a2, a3, a4
            ])

        elif cmd == "set-toggle":
            if len(argv) < 4: raise SystemExit("usage: lmk set-toggle ROW COL")
            row = parse_int(argv[2], "row", 0, 255)
            col = parse_int(argv[3], "col", 0, 255)
            pretty_set_toggle(dev, row, col)
        elif cmd == "list-xinput":
            print("Available XInput key names:\n")
            for name in sorted(XINPUT_KEYCODES.keys()):
                print(" ", name)
            return
        elif cmd in ("get-game-controller-mode", "game-controller-mode"):
            pretty_get_game_controller_mode(dev)
        elif cmd in ("get-xinput-mode"):
            pretty_get_xinput_mode(dev)
        elif cmd in ("get-type"):
            pretty_get_type(dev)
        elif cmd == "set-game-controller-mode":
            if len(argv) < 3:
                raise SystemExit("usage: lmk set-game-controller-mode MODE")

            mode = parse_int(argv[2], "mode", 0, 3)  # Mode should be 0 or 1
            pretty_set_game_controller_mode(dev, mode)
        elif cmd == "set-gamepad":
            if len(argv) < 5:
                raise SystemExit("usage: lmk set-gamepad ROW COL XINPUT")
            row = parse_int(argv[2], "row", 0, 255)
            col = parse_int(argv[3], "col", 0, 255)
            xinput_name = argv[4]  # Get the xinput key name as a string
            print(f"xinput_name: {xinput_name}")
            xi = parse_xinput_keycode(xinput_name)  # Convert the string to numeric XInput value
            if xi is None:
                raise SystemExit(f"Invalid XInput key name: {xinput_name}")  # Handle invalid XInput names
            pretty_set_gamepad(dev, row, col, xi)
        elif cmd in ("get-curve", "curve"):
            pretty_get_curve(dev)
        elif cmd in ("set-curve", "curve"):
            if len(argv) < 10: raise SystemExit("usage: lmk set-curve x1 y1 x2 y2 x3 y3 x4 y4")
            pts = [parse_int(x, None, 0, 32767) for x in argv[2:10]]
            pretty_set_curve(dev, pts)
        elif cmd in ("calib-start","start-calib"):
            pretty_start_calib(dev)
        elif cmd in ("calib-clear","clear-calib"):
            pretty_clear_calib(dev)
        elif cmd in ("get-calib-state","calib-state"):
            pretty_get_calib_state(dev)
        elif cmd in ("get-key-calib","key-calib"):
            if len(argv) < 4: raise SystemExit("usage: lmk get-key-calib ROW COL")
            row = parse_int(argv[2], "row", 0, 255)
            col = parse_int(argv[3], "col", 0, 255)
            pretty_get_key_calib(dev, row, col)
        elif cmd in ("get-firmware-info", "firmware"):
            pretty_get_firmware(dev)
        else:
            print("Unknown command:", cmd)
            usage_and_exit()
    finally:
        try:
            dev.close()
        except Exception:
            pass

if __name__ == "__main__":
    main(sys.argv)
