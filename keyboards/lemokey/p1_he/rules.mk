#JOYSTICK_ENABLE = yes
#JOYSTICK_DRIVER = analog
KEYCHRON_RGB_ENABLE = yes
# Inscrease stack size to avoid crushing of eeprom_update_block()
USE_PROCESS_STACKSIZE = 0x2000
USE_FPU = yes

OPT_DEFS += -DSHARED_EP_ENABLE -DKEYBOARD_SHARED_EP
OPT_DEFS += -DXINPUT_ENABLE

include keyboards/keychron/common/analog_matrix/analog_matrix.mk
include keyboards/keychron/common/keychron_common.mk
include keyboards/keychron/common/wireless/wireless.mk

VPATH += $(TOP_DIR)/keyboards/lemokey/common

OPT = 2
CFLAGS += -DJOYSTICK_AXIS_RESOLUTION=16
NKRO_ENABLE = yes
MOUSEKEY_ENABLE = yes
EXTRAKEY_ENABLE = yes
COMMAND_ENABLE = no
EXTRAFLAGS += -O3
#RAW_HID_ENABLE = yes
DIGITIZER_ENABLE = no
MOUSE_ENABLE = no
#JOYSTICK_ENABLE = no
RAW_ENABLE = yes
#LK_WIRELESS_ENABLE = no
#KC_BLUETOOTH_ENABLE = no
VIA_ENABLE = yes
#VIAL_ENABLE = yes
#KC_BLUETOOTH_ENABLE = no
#LK_WIRELESS_ENABLE = no
#ENCODER_ENABLE = no
