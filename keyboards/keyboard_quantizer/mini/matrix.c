

#include QMK_KEYBOARD_H
#include "mini.h"

#include <string.h>

#include "report_descriptor_parser.h"
#include "report_parser.h"

#include "quantum.h"

#include "tusb.h"

extern matrix_row_t* matrix_dest;
matrix_row_t* matrix_mouse_dest;

static int32_t led_count = -1;
#define LED_BLINK_TIME_MS 50

static uint8_t kbd_addr;
static uint8_t kbd_instance;
static uint8_t hid_report_buffer[64];
static uint8_t hid_report_size;
static uint8_t hid_instance;
static bool    hid_disconnect_flag;
static uint8_t pre_keyreport[8];

bool report_parser(uint8_t instance, uint8_t const* buf, uint16_t len,
                   matrix_row_t* current_matrix);
int  send_led_report(uint8_t* leds);

void matrix_init_custom(void) {
    setPinOutput(KQ_PIN_LED);
    writePinHigh(KQ_PIN_LED);
}

bool matrix_scan_custom(matrix_row_t current_matrix[]) {
    matrix_dest = current_matrix;

    static uint8_t keyboard_led;
    if (keyboard_led != host_keyboard_leds()) {
        keyboard_led = host_keyboard_leds();
        send_led_report(&keyboard_led);
    }

    if (led_count > 0) {
        if (timer_elapsed(led_count) < LED_BLINK_TIME_MS) {
            writePinLow(KQ_PIN_LED);
        } else if (timer_elapsed(led_count) < 2 * LED_BLINK_TIME_MS) {
            writePinHigh(KQ_PIN_LED);
        } else {
            led_count = -1;
        }
    }

    matrix_mouse_dest = &current_matrix[MATRIX_MSBTN_ROW];

    if (hid_disconnect_flag) {
        bool matrix_change = false;
        for (uint8_t rowIdx = 0; rowIdx < MATRIX_ROWS; rowIdx++) {
            if (current_matrix[rowIdx] != 0) {
                matrix_change          = true;
                current_matrix[rowIdx] = 0;
            }
        }

        hid_disconnect_flag = false;

        return matrix_change;
    }

    if (hid_report_size > 0) {
        bool matrix_change = report_parser(hid_instance, hid_report_buffer,
                                           hid_report_size, current_matrix);
        hid_report_size    = 0;
        return matrix_change;
    } else {
        return false;
    }
}

void tuh_hid_mount_cb(uint8_t dev_addr, uint8_t instance,
                      uint8_t const* desc_report, uint16_t desc_len) {
    if (led_count < 0) {
        led_count = timer_read();
    }
    report_descriptor_parser_user(instance, desc_report, desc_len);

    uint8_t const itf_protocol = tuh_hid_interface_protocol(dev_addr, instance);
    if (itf_protocol == HID_ITF_PROTOCOL_KEYBOARD) {
        kbd_addr     = dev_addr;
        kbd_instance = instance;
    }

    tuh_hid_set_protocol(dev_addr, instance, HID_PROTOCOL_REPORT);

    tuh_hid_receive_report(dev_addr, instance);
}

void tuh_hid_umount_cb(uint8_t dev_addr, uint8_t instance) {
    on_disconnect_device_user(instance);
    hid_disconnect_flag = true;
}

void tuh_hid_report_received_cb(uint8_t dev_addr, uint8_t instance,
                                uint8_t const* report, uint16_t len) {
    if (led_count < 0) {
        led_count = timer_read();
    }

    while (hid_report_size > 0) {
        continue;
    }
    hid_instance = instance;
    memcpy(hid_report_buffer, report, len);
    hid_report_size = len;

    tuh_hid_receive_report(dev_addr, instance);
}

void report_descriptor_parser_user(uint8_t dev_num, uint8_t const* buf,
                                   uint16_t len) {
    if (keyboard_config.parser_type == 1) {
        // no descriptor parser
    } else {
        parse_report_descriptor(dev_num, buf, len);
    }
}

bool report_parser(uint8_t instance, uint8_t const* buf, uint16_t len,
                   matrix_row_t* current_matrix) {
    if (keyboard_config.parser_type == 1) {
        return report_parser_fixed(buf, len, pre_keyreport, current_matrix);
    } else {
        return parse_report(instance, buf, len);
    }
}

void on_disconnect_device_user(uint8_t device) {
    memset(pre_keyreport, 0, sizeof(pre_keyreport));
}

int send_led_report(uint8_t* leds) {
    if (kbd_addr != 0) {
        tuh_hid_set_report(kbd_addr, kbd_instance, 0, HID_REPORT_TYPE_OUTPUT,
                           leds, sizeof(*leds));
    }

    return 0;
}
