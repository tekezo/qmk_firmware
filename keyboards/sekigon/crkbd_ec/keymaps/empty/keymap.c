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
#include QMK_KEYBOARD_H

#include "ec_switch_matrix.h"
#include "pico_cdc.h"
#include "eeprom.h"

#if defined(RGBLIGHT_ENABLE)
#include "rgblight.h"
#elif defined(RGB_MATRIX_ENABLE)
#include "rgb_matrix.h"
extern rgb_config_t rgb_matrix_config;
#endif

// clang-format off
const uint16_t keymaps[DYNAMIC_KEYMAP_LAYER_COUNT][MATRIX_ROWS][MATRIX_COLS] = { 0 };
// clang-format on

layer_state_t layer_state_set_user(layer_state_t state) {
    uint8_t layer = get_highest_layer(state);

    if (layer < DYNAMIC_KEYMAP_LAYER_COUNT) {
#if defined(RGBLIGHT_ENABLE)
        rgblight_update_dword(eeprom_read_dword((const uint32_t *)(VIA_RGBLIGHT_USER_ADDR + 4 * layer)));
#elif defined(RGB_MATRIX_ENABLE)
        rgb_matrix_config.raw = eeprom_read_dword((const uint32_t *)(VIA_RGBLIGHT_USER_ADDR + 4 * layer));
#endif
    }

    return state;
}

bool process_record_user(uint16_t keycode, keyrecord_t *record) { return true; }

void keyboard_post_init_user() { layer_state_set_user(layer_state); }

static bool dprint_matrix = false;

void pico_cdc_on_disconnect(void) { dprint_matrix = false; }

bool pico_cdc_receive_kb(uint8_t const *buf, uint32_t cnt) {
    if (cnt > 0 && buf[0] == 'e') {
        dprint_matrix ^= true;
        return false;
    }
    return true;
}

void matrix_scan_user(void) {
    static int cnt = 0;
    if (dprint_matrix && cnt++ == 30) {
        cnt = 0;
        ecsm_dprint_matrix();
    }
}
