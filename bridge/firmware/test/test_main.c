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

// Tests for the parts of the firmware that don't need the board:
//   cc -I../main ../main/reports.c ../main/commands.c test_main.c && ./a.out

#include "commands.h"
#include "reports.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;

#define CHECK(cond)                                                    \
    do {                                                               \
        if (!(cond)) {                                                 \
            printf("%s:%d: failed: %s\n", __FILE__, __LINE__, #cond);  \
            ++failures;                                                \
        }                                                              \
    } while (0)

static void parses_commands(void)
{
    command_t c = parse_command("m -12 340");
    CHECK(c.type == CMD_MOVE && c.a == -12 && c.b == 340);
    c = parse_command("kd e1\r");
    CHECK(c.type == CMD_KEY_DOWN && c.a == 0xE1);
    c = parse_command("ku 04");
    CHECK(c.type == CMD_KEY_UP && c.a == 4);
    c = parse_command("cd e9");
    CHECK(c.type == CMD_MEDIA_DOWN && c.a == 0xE9);
    c = parse_command("b 5");
    CHECK(c.type == CMD_BUTTONS && c.a == 5);
    c = parse_command("w -1 0");
    CHECK(c.type == CMD_WHEEL && c.a == -1 && c.b == 0);
    c = parse_command("target -1");
    CHECK(c.type == CMD_TARGET && c.a == -1);
    c = parse_command("allow 3 0");
    CHECK(c.type == CMD_ALLOW && c.a == 3 && c.b == 0);
    CHECK(parse_command("pair").type == CMD_PAIR);
    CHECK(parse_command("pair stop").type == CMD_PAIR_STOP);
    CHECK(parse_command("  hello  ").type == CMD_HELLO);
    CHECK(parse_command("").type == CMD_NONE);
    CHECK(parse_command("I (312) boot: something").type == CMD_INVALID);
    CHECK(parse_command("m 1").type == CMD_INVALID);
    CHECK(parse_command("m 1 2 3").type == CMD_INVALID);
    CHECK(parse_command("kd 0").type == CMD_INVALID);
    CHECK(parse_command("kd 100").type == CMD_INVALID);
    CHECK(parse_command("b 32").type == CMD_INVALID);
    CHECK(parse_command("pair now").type == CMD_INVALID);
    CHECK(parse_command("allow 1 2").type == CMD_INVALID);
}

static void keeps_keys(void)
{
    keyboard_state_t k;
    memset(&k, 0, sizeof k);
    uint8_t r[KEYBOARD_REPORT_SIZE];

    CHECK(keyboard_press(&k, 0xE1));   // left shift
    CHECK(keyboard_press(&k, 0x04));   // a
    CHECK(!keyboard_press(&k, 0x04));  // already down
    keyboard_report(&k, r);
    const uint8_t shift_a[] = {0x02, 0, 0x04, 0, 0, 0, 0, 0};
    CHECK(!memcmp(r, shift_a, sizeof r));

    CHECK(keyboard_press(&k, 0x05));
    CHECK(keyboard_release(&k, 0x04));
    CHECK(!keyboard_release(&k, 0x04));
    CHECK(keyboard_release(&k, 0xE1));
    keyboard_report(&k, r);
    const uint8_t b_only[] = {0, 0, 0x05, 0, 0, 0, 0, 0};
    CHECK(!memcmp(r, b_only, sizeof r));

    for (uint8_t key = 0x10; key < 0x16; ++key) {
        keyboard_press(&k, key);
    }
    CHECK(k.keys[0] == 0x10 && k.keys[5] == 0x15);  // the oldest, 0x05, made room
}

static void builds_mouse_reports(void)
{
    uint8_t r[MOUSE_REPORT_SIZE];
    mouse_report(1, -2, 300, -1, 0, r);
    const uint8_t expected[] = {0x01, 0xFE, 0xFF, 0x2C, 0x01, 0xFF, 0x00};
    CHECK(!memcmp(r, expected, sizeof r));

    int32_t pending = 40000;
    CHECK(take_movement(&pending) == 32767);
    CHECK(pending == 40000 - 32767);
    CHECK(take_movement(&pending) == 40000 - 32767);
    CHECK(pending == 0);
    pending = -300;
    CHECK(take_wheel(&pending) == -127 && pending == -173);
}

static void report_map_is_balanced(void)
{
    int depth = 0;
    for (size_t i = 0; i < hid_report_map_size;) {
        const uint8_t prefix = hid_report_map[i];
        const int size = (prefix & 3) == 3 ? 4 : (prefix & 3);
        if ((prefix & 0xFC) == 0xA0) {
            ++depth;
        } else if (prefix == 0xC0) {
            --depth;
        }
        i += 1 + (size_t)size;
        CHECK(i <= hid_report_map_size);
    }
    CHECK(depth == 0);
}

int main(void)
{
    parses_commands();
    keeps_keys();
    builds_mouse_reports();
    report_map_is_balanced();
    if (failures) {
        printf("%d failed\n", failures);
        return 1;
    }
    printf("all passed\n");
    return 0;
}
