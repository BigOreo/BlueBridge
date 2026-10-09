/*
    GlideKVM -- mouse and keyboard sharing utility
    Copyright (C) GlideKVM contributors

    This package is free software; you can redistribute it and/or
    modify it under the terms of the GNU General Public License
    found in the file LICENSE that should have accompanied this file.

    This package is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

// GlideKVM Bridge: takes the keyboard and mouse from the main computer over
// USB and passes them on, over Bluetooth, to the phone or tablet the mouse
// is on. The commands are described in commands.h.

#include "bridge.h"
#include "commands.h"
#include "reports.h"

#include "driver/uart.h"
#include "driver/usb_serial_jtag.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs_flash.h"

#include <stdio.h>
#include <string.h>

#define SERIAL_PORT UART_NUM_0
#define SERIAL_BAUD 921600
#define COMMAND_LINE_MAX 96
// how often gathered mouse movement goes out
#define MOUSE_PERIOD_MS 8

static SemaphoreHandle_t s_output_lock;
static SemaphoreHandle_t s_input_lock;
static bool s_usb_serial;

// what the main computer asked for, kept here so a report can be rebuilt
static keyboard_state_t s_keys;
static uint8_t s_buttons;
static int32_t s_dx, s_dy, s_wheel, s_pan;
static uint16_t s_media;

void bridge_say(const char* format, ...)
{
    char line[128];
    va_list args;
    va_start(args, format);
    int n = vsnprintf(line, sizeof line - 1, format, args);
    va_end(args);
    if (n < 0) {
        return;
    }
    if (n > (int)sizeof line - 2) {
        n = sizeof line - 2;
    }
    line[n++] = '\n';
    xSemaphoreTake(s_output_lock, portMAX_DELAY);
    uart_write_bytes(SERIAL_PORT, line, n);
    if (s_usb_serial) {
        usb_serial_jtag_write_bytes(line, n, 0);
    }
    xSemaphoreGive(s_output_lock);
}

static void send_keyboard(void)
{
    uint8_t report[KEYBOARD_REPORT_SIZE];
    keyboard_report(&s_keys, report);
    bridge_send(REPORT_ID_KEYBOARD, report, sizeof report);
}

static void send_media(void)
{
    uint8_t report[CONSUMER_REPORT_SIZE];
    consumer_report(s_media, report);
    bridge_send(REPORT_ID_CONSUMER, report, sizeof report);
}

// Sends the movement gathered so far, with the buttons as they are now.
static void send_mouse(bool even_if_still)
{
    do {
        if (!even_if_still && !s_dx && !s_dy && !s_wheel && !s_pan) {
            return;
        }
        int32_t dx = s_dx, dy = s_dy, wheel = s_wheel, pan = s_pan;
        const int16_t x = take_movement(&dx);
        const int16_t y = take_movement(&dy);
        const int8_t w = take_wheel(&wheel);
        const int8_t p = take_wheel(&pan);
        uint8_t report[MOUSE_REPORT_SIZE];
        mouse_report(s_buttons, x, y, w, p, report);
        if (!bridge_send(REPORT_ID_MOUSE, report, sizeof report)) {
            // the radio is busy: keep it for the next go, unless nobody is
            // there to take it
            if (!bridge_target_ready()) {
                s_dx = s_dy = s_wheel = s_pan = 0;
            }
            return;
        }
        s_dx = dx;
        s_dy = dy;
        s_wheel = wheel;
        s_pan = pan;
        even_if_still = false;
    } while (s_dx || s_dy || s_wheel || s_pan);
}

static void release_all(void)
{
    s_dx = s_dy = s_wheel = s_pan = 0;
    memset(&s_keys, 0, sizeof s_keys);
    s_buttons = 0;
    s_media = 0;
    send_keyboard();
    send_mouse(true);
    send_media();
}

// keeps what waits to be sent within reason while the radio is slow
static int32_t clamp(int32_t value)
{
    return value > 1000000 ? 1000000 : value < -1000000 ? -1000000 : value;
}

static void handle_line(const char* line)
{
    const command_t cmd = parse_command(line);
    switch (cmd.type) {
    case CMD_NONE:
        break;
    case CMD_INVALID:
        bridge_say("@error can't read: %.40s", line);
        break;
    case CMD_HELLO:
        bridge_say("@hello glidekvm-bridge %d %s %d", BRIDGE_PROTOCOL_VERSION, BRIDGE_FIRMWARE_VERSION,
                   BRIDGE_SLOTS);
        break;
    case CMD_LIST:
        bridge_list();
        break;
    case CMD_PAIR:
        bridge_pair(true);
        break;
    case CMD_PAIR_STOP:
        bridge_pair(false);
        break;
    case CMD_FORGET:
        bridge_forget(cmd.a);
        break;
    case CMD_ALLOW:
        bridge_allow(cmd.a, cmd.b != 0);
        break;
    case CMD_TARGET:
        // nothing stays held down on the device the mouse leaves
        release_all();
        bridge_target(cmd.a);
        break;
    case CMD_MOVE:
        s_dx = clamp(s_dx + cmd.a);
        s_dy = clamp(s_dy + cmd.b);
        break;
    case CMD_WHEEL:
        // HID wheels count up as away from the person
        s_wheel = clamp(s_wheel - cmd.a);
        s_pan = clamp(s_pan + cmd.b);
        break;
    case CMD_BUTTONS:
        if (s_buttons != (uint8_t)cmd.a) {
            // move first, then press, where the pointer is now
            send_mouse(false);
            s_buttons = (uint8_t)cmd.a;
            send_mouse(true);
        }
        break;
    case CMD_KEY_DOWN:
        if (keyboard_press(&s_keys, (uint8_t)cmd.a)) send_keyboard();
        break;
    case CMD_KEY_UP:
        if (keyboard_release(&s_keys, (uint8_t)cmd.a)) send_keyboard();
        break;
    case CMD_MEDIA_DOWN:
        s_media = (uint16_t)cmd.a;
        send_media();
        break;
    case CMD_MEDIA_UP:
        if (s_media == cmd.a) {
            s_media = 0;
            send_media();
        }
        break;
    case CMD_RELEASE:
        release_all();
        break;
    }
}

typedef struct {
    char line[COMMAND_LINE_MAX];
    int length;
    bool overflow;
} line_buffer_t;

static void take_bytes(line_buffer_t* buffer, const uint8_t* data, int size)
{
    for (int i = 0; i < size; ++i) {
        const char c = (char)data[i];
        if (c == '\n' || c == '\r') {
            buffer->line[buffer->length] = 0;
            if (!buffer->overflow && buffer->length) {
                xSemaphoreTake(s_input_lock, portMAX_DELAY);
                handle_line(buffer->line);
                xSemaphoreGive(s_input_lock);
            }
            buffer->length = 0;
            buffer->overflow = false;
        } else if (buffer->length < COMMAND_LINE_MAX - 1) {
            buffer->line[buffer->length++] = c;
        } else {
            buffer->overflow = true;
        }
    }
}

static void uart_task(void* arg)
{
    static line_buffer_t buffer;
    uint8_t data[128];
    for (;;) {
        const int n = uart_read_bytes(SERIAL_PORT, data, sizeof data, pdMS_TO_TICKS(20));
        if (n > 0) {
            take_bytes(&buffer, data, n);
        }
    }
}

static void usb_task(void* arg)
{
    static line_buffer_t buffer;
    uint8_t data[128];
    for (;;) {
        const int n = usb_serial_jtag_read_bytes(data, sizeof data, pdMS_TO_TICKS(20));
        if (n > 0) {
            take_bytes(&buffer, data, n);
        }
    }
}

// Movement arrives far more often than Bluetooth can carry it: gather it and
// send it at a steady pace.
static void mouse_task(void* arg)
{
    TickType_t wake = xTaskGetTickCount();
    for (;;) {
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(MOUSE_PERIOD_MS));
        xSemaphoreTake(s_input_lock, portMAX_DELAY);
        send_mouse(false);
        xSemaphoreGive(s_input_lock);
    }
}

void app_main(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    s_output_lock = xSemaphoreCreateMutex();
    s_input_lock = xSemaphoreCreateMutex();

    // Boards reach the computer either through a USB serial chip on the
    // first serial port or through the chip's own USB: listen on both.
    const uart_config_t uart = {
        .baud_rate = SERIAL_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    ESP_ERROR_CHECK(uart_driver_install(SERIAL_PORT, 2048, 2048, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(SERIAL_PORT, &uart));
    usb_serial_jtag_driver_config_t usb = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    s_usb_serial = usb_serial_jtag_driver_install(&usb) == ESP_OK;

    bridge_start();

    xTaskCreate(uart_task, "uart", 4096, NULL, 10, NULL);
    if (s_usb_serial) {
        xTaskCreate(usb_task, "usb", 4096, NULL, 10, NULL);
    }
    xTaskCreate(mouse_task, "mouse", 4096, NULL, 9, NULL);
}
