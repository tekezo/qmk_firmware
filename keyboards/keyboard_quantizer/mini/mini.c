/* Copyright 2021 sekigon-gonnoc
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

#include "keyboard_quantizer.h"
#include "mini.h"
#include "report_descriptor_parser.h"
#include "report_parser.h"

#include <string.h>

#include "quantum.h"
#include "debug.h"
#include "process_combo.h"
#include "via.h"
#include "eeprom.h"

#include "host_os_eeconfig.h"
#include "use_layer_as_combo_config.h"

#include "pico_cdc.h"
#include "hardware/uart.h"
#include "hardware/irq.h"
#include "hardware/resets.h"
#include "cdc_device.h"

#include "RP2040.h"
#include "core_cm0plus.h"

#define LEN(x) (sizeof(x) / sizeof(x[0]))

extern bool    ch559_update_mode;
extern uint8_t device_cnt;
extern uint8_t hid_info_cnt;

keyboard_config_t keyboard_config;

void pico_cdc_receive_cb(uint8_t const *buf, uint32_t cnt) {
     if (cnt > 0) {
        printf("%c\n", buf[0]);

        switch (buf[0]) {
            case 'd':
                if (debug_enable) {
                    puts("Disable debug output\n");
                    debug_enable = false;
                } else {
                    puts("Enable debug output\n");
                    debug_enable = true;
                }
                break;
            case 'b':
                bootloader_jump();
                break;
        }
    }
}

void keyboard_post_init_kb_rev(void) {
    debug_enable = false;

    if (keyboard_config.layer_to_combo) {
        convert_layer_to_combo();
    } else {
        combo_disable();
    }

    keyboard_post_init_user();
}

void dynamic_keymap_reset() {
    for (int idx = 0; idx < DYNAMIC_KEYMAP_LAYER_COUNT * MATRIX_COLS_DEFAULT *
                                MATRIX_ROWS_DEFAULT;
         idx++) {
        const uint8_t layer = idx / (MATRIX_COLS_DEFAULT * MATRIX_ROWS_DEFAULT);
        const uint8_t offset =
            idx % (MATRIX_COLS_DEFAULT * MATRIX_ROWS_DEFAULT);
        const uint8_t row = offset / MATRIX_COLS_DEFAULT;
        const uint8_t col = offset % MATRIX_COLS_DEFAULT;

        if (row < MATRIX_MODIFIER_ROW) {
            dynamic_keymap_set_keycode(
                layer, row, col,
                idx % (MATRIX_COLS_DEFAULT * MATRIX_ROWS_DEFAULT));
        } else if (row == MATRIX_MODIFIER_ROW) {
            dynamic_keymap_set_keycode(layer, row, col, KC_LCTRL + col);
        } else if (row == MATRIX_MSBTN_ROW) {
            dynamic_keymap_set_keycode(layer, row, col, KC_BTN1 + col);
        } else if (row == MATRIX_MSGES_ROW) {
            if (col < MATRIX_MSWHEEL_COL) {
                dynamic_keymap_set_keycode(layer, row, col, KC_NO);
            } else {
                dynamic_keymap_set_keycode(
                    layer, row, col, KC_MS_WH_UP + col - MATRIX_MSWHEEL_COL);
            }
        }
    }
}

uint8_t  encoder_modifier            = 0;
uint16_t encoder_modifier_pressed_ms = 0;
bool     is_encoder_action           = false;
int      reset_flag                  = 0;

#ifndef ENCODER_MODIFIER_TIMEOUT_MS
#    define ENCODER_MODIFIER_TIMEOUT_MS 500
#endif

void on_host_os_eeconfig_update(void) {
    keyboard_config.raw = eeconfig_read_kb();
    set_key_override(keyboard_config.override_mode);
}

void eeconfig_init_kb(void) {
    if (keyboard_config.os_eeconfig) {
        host_os_eeconfig_init();
    }

    keyboard_config.raw = 0;
    eeconfig_update_kb(keyboard_config.raw);

    eeconfig_init_user();
}

void matrix_init_kb(void) {
    keyboard_config.raw = eeconfig_read_kb();
    set_key_override(keyboard_config.override_mode);

    matrix_init_user();
}

void matrix_scan_kb(void) {
    if (encoder_modifier != 0 && timer_elapsed(encoder_modifier_pressed_ms) >
                                     ENCODER_MODIFIER_TIMEOUT_MS) {
        unregister_mods(encoder_modifier);
        encoder_modifier = 0;
    }

    if (keyboard_config.os_eeconfig) {
        host_os_eeconfig_update(get_usb_host_os_type());
    }

    if (reset_flag > 0) {
        if (--reset_flag == 0) {
            // Reset USB to detect host os properly after reset
            reset_block(RESETS_RESET_USBCTRL_BITS);
            // Reset MCU
            __NVIC_SystemReset();
        }
    }

    matrix_scan_user();
}

bool process_record_kb(uint16_t keycode, keyrecord_t *record) {
    if (encoder_modifier != 0 && !is_encoder_action) {
        unregister_mods(encoder_modifier);
        encoder_modifier = 0;
    }

    switch (keycode) {
        case QK_MODS ... QK_MODS_MAX:
            if (is_encoder_action) {
                if (record->event.pressed) {
                    uint8_t current_mods        = keycode >> 8;
                    encoder_modifier_pressed_ms = timer_read();
                    if (current_mods != encoder_modifier) {
                        del_mods(encoder_modifier);
                        encoder_modifier = current_mods;
                        add_mods(encoder_modifier);
                    }
                    register_code(keycode & 0xff);
                } else {
                    unregister_code(keycode & 0xff);
                }
                return false;
            } else {
                return true;
            }
            break;
    }

    if (record->event.pressed) {
        switch (keycode) {
            case DISABLE_KEY_OVERRIDES:
            case KEY_OVERRIDE_OFF: {
                println("Disable key overrides");
                keyboard_config.override_mode = DISABLE_OVERRIDE;
                set_key_override(DISABLE_OVERRIDE);
                eeconfig_update_kb(keyboard_config.raw);
                return true;
            }
            case ENABLE_US_KEY_ON_JP_OS_OVERRIDE: {
                println(
                    "Perform as an US keyboard on the OS configured for JP");
                keyboard_config.override_mode = US_KEY_ON_JP_OS_OVERRIDE;
                set_key_override(US_KEY_ON_JP_OS_OVERRIDE);
                eeconfig_update_kb(keyboard_config.raw);
                return false;
            } break;
            case ENABLE_JP_KEY_ON_US_OS_OVERRIDE: {
                println("Perform as a JP keyboard on the OS configured for US");
                keyboard_config.override_mode = JP_KEY_ON_US_OS_OVERRIDE;
                set_key_override(JP_KEY_ON_US_OS_OVERRIDE);
                eeconfig_update_kb(keyboard_config.raw);
                return false;
            } break;
            case CMB_ON:
            case CMB_OFF:
            case CMB_TOG: {
                if (keyboard_config.layer_to_combo) {
                    convert_layer_to_combo();
                }
                return true;
            } break;
        }
    }

    return process_record_user(keycode, record);
}

enum via_keyboard_value_id_kb { id_keyboard_quantizer = 0x99 };

enum via_kq_value_id_kb {
    id_config_ver = 0x01,
    id_bootloader,
    id_reset,
    id_eeprom,  // read/write eeprom
    id_host_os,      // get os
};

static void raw_hid_get_kb(uint8_t *data, uint8_t length) {
    uint8_t *command_id   = &(data[0]);
    uint8_t *command_data = &(data[1]);

    switch (*command_id) {
        case id_config_ver: {
            command_data[0] = 0x01;
        } break;
        case id_eeprom: {
            uint32_t offset = (command_data[0] << 8) | command_data[1];
            uint16_t size   = command_data[2];
            size            = size < (length - 4) ? size : (length - 4);
            eeprom_read_block(&command_data[3], (const void *)offset, size);
        } break;
        case id_host_os: {
            command_data[0] = get_usb_host_os_type();
        }
    }
}

static void raw_hid_set_kb(uint8_t *data, uint8_t length) {
    uint8_t *command_id   = &(data[0]);
    uint8_t *command_data = &(data[1]);

    switch (*command_id) {
        case id_bootloader: {
            bootloader_jump();
        } break;
        case id_reset: {
            reset_flag = 100;
        } break;
        case id_eeprom: {
            uint32_t offset = (command_data[0] << 8) | command_data[1];
            uint16_t size   = command_data[2];
            size            = size < (length - 4) ? size : (length - 4);
            eeprom_update_block(&command_data[3], (void *)offset, size);
        } break;
    }
}

void raw_hid_receive_kb(uint8_t *data, uint8_t length) {
    uint8_t *command_id   = &(data[0]);
    uint8_t *command_data = &(data[1]);
    switch (*command_id) {
        case id_get_keyboard_value: {
            if (command_data[0] == id_keyboard_quantizer) {
                raw_hid_get_kb(data + 2, length - 2);
            }
        } break;
        case id_set_keyboard_value: {
            if (command_data[0] == id_keyboard_quantizer) {
                raw_hid_set_kb(data + 2, length - 2);
            }
        } break;
    }
}
