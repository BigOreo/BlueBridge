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

#include "commands.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

static bool next_word(const char** p, char* out, size_t size)
{
    while (**p == ' ' || **p == '\t') {
        ++*p;
    }
    size_t n = 0;
    while (**p && **p != ' ' && **p != '\t' && **p != '\r' && **p != '\n') {
        if (n + 1 < size) {
            out[n++] = **p;
        }
        ++*p;
    }
    out[n] = 0;
    return n > 0;
}

static bool number(const char** p, int base, int32_t min, int32_t max, int32_t* out)
{
    char word[16];
    if (!next_word(p, word, sizeof word)) {
        return false;
    }
    char* end = NULL;
    const long value = strtol(word, &end, base);
    if (*end || value < min || value > max) {
        return false;
    }
    *out = (int32_t)value;
    return true;
}

static bool at_end(const char** p)
{
    char word[4];
    return !next_word(p, word, sizeof word);
}

command_t parse_command(const char* line)
{
    command_t cmd = {CMD_NONE, 0, 0};
    const char* p = line;
    char word[12];
    if (!next_word(&p, word, sizeof word)) {
        return cmd;
    }

    bool ok = true;
    if (!strcmp(word, "m")) {
        cmd.type = CMD_MOVE;
        ok = number(&p, 10, -1000000, 1000000, &cmd.a) && number(&p, 10, -1000000, 1000000, &cmd.b);
    } else if (!strcmp(word, "kd") || !strcmp(word, "ku")) {
        cmd.type = word[1] == 'd' ? CMD_KEY_DOWN : CMD_KEY_UP;
        ok = number(&p, 16, 1, 0xFF, &cmd.a);
    } else if (!strcmp(word, "b")) {
        cmd.type = CMD_BUTTONS;
        ok = number(&p, 10, 0, 0x1F, &cmd.a);
    } else if (!strcmp(word, "w")) {
        cmd.type = CMD_WHEEL;
        ok = number(&p, 10, -10000, 10000, &cmd.a) && number(&p, 10, -10000, 10000, &cmd.b);
    } else if (!strcmp(word, "cd") || !strcmp(word, "cu")) {
        cmd.type = word[1] == 'd' ? CMD_MEDIA_DOWN : CMD_MEDIA_UP;
        ok = number(&p, 16, 1, 0x3FF, &cmd.a);
    } else if (!strcmp(word, "target")) {
        cmd.type = CMD_TARGET;
        ok = number(&p, 10, -1, 255, &cmd.a);
    } else if (!strcmp(word, "release")) {
        cmd.type = CMD_RELEASE;
    } else if (!strcmp(word, "hello")) {
        cmd.type = CMD_HELLO;
    } else if (!strcmp(word, "list")) {
        cmd.type = CMD_LIST;
    } else if (!strcmp(word, "pair")) {
        char arg[8];
        if (next_word(&p, arg, sizeof arg)) {
            cmd.type = CMD_PAIR_STOP;
            ok = !strcmp(arg, "stop");
        } else {
            cmd.type = CMD_PAIR;
        }
    } else if (!strcmp(word, "forget")) {
        cmd.type = CMD_FORGET;
        ok = number(&p, 10, 0, 255, &cmd.a);
    } else if (!strcmp(word, "allow")) {
        cmd.type = CMD_ALLOW;
        ok = number(&p, 10, 0, 255, &cmd.a) && number(&p, 10, 0, 1, &cmd.b);
    } else {
        ok = false;
    }

    if (!ok || !at_end(&p)) {
        cmd.type = CMD_INVALID;
    }
    return cmd;
}
