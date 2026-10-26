#include "keys.h"

#include <string.h>

#include "strutil.h"

/* {shift, usage} for each ASCII character on a US keyboard; usage 0 = not typeable. */
static const uint8_t ascii_map[128][2] = {
    ['\t'] = {0, 0x2B}, ['\n'] = {0, 0x28}, [' '] = {0, 0x2C},
    ['!'] = {1, 0x1E}, ['@'] = {1, 0x1F}, ['#'] = {1, 0x20}, ['$'] = {1, 0x21}, ['%'] = {1, 0x22},
    ['^'] = {1, 0x23}, ['&'] = {1, 0x24}, ['*'] = {1, 0x25}, ['('] = {1, 0x26}, [')'] = {1, 0x27},
    ['1'] = {0, 0x1E}, ['2'] = {0, 0x1F}, ['3'] = {0, 0x20}, ['4'] = {0, 0x21}, ['5'] = {0, 0x22},
    ['6'] = {0, 0x23}, ['7'] = {0, 0x24}, ['8'] = {0, 0x25}, ['9'] = {0, 0x26}, ['0'] = {0, 0x27},
    ['-'] = {0, 0x2D}, ['_'] = {1, 0x2D}, ['='] = {0, 0x2E}, ['+'] = {1, 0x2E},
    ['['] = {0, 0x2F}, ['{'] = {1, 0x2F}, [']'] = {0, 0x30}, ['}'] = {1, 0x30},
    ['\\'] = {0, 0x31}, ['|'] = {1, 0x31}, [';'] = {0, 0x33}, [':'] = {1, 0x33},
    ['\''] = {0, 0x34}, ['"'] = {1, 0x34}, ['`'] = {0, 0x35}, ['~'] = {1, 0x35},
    [','] = {0, 0x36}, ['<'] = {1, 0x36}, ['.'] = {0, 0x37}, ['>'] = {1, 0x37},
    ['/'] = {0, 0x38}, ['?'] = {1, 0x38},
};

bool keys_ascii(char c, uint8_t *mod, uint8_t *code)
{
    unsigned char u = (unsigned char)c;
    if (u >= 128) {
        return false;
    }
    if (u >= 'a' && u <= 'z') {
        *mod = 0;
        *code = (uint8_t)(KEY_A + (u - 'a'));
        return true;
    }
    if (u >= 'A' && u <= 'Z') {
        *mod = KMOD_SHIFT;
        *code = (uint8_t)(KEY_A + (u - 'A'));
        return true;
    }
    if (!ascii_map[u][1]) {
        return false;
    }
    *mod = ascii_map[u][0] ? KMOD_SHIFT : 0;
    *code = ascii_map[u][1];
    return true;
}

char keys_to_ascii(uint8_t mod, uint8_t code)
{
    bool shift = (mod & KMOD_SHIFT) != 0;
    if (code >= KEY_A && code < KEY_A + 26) {
        return (char)((shift ? 'A' : 'a') + (code - KEY_A));
    }
    for (int c = 0; c < 128; c++) {
        if (ascii_map[c][1] == code && (ascii_map[c][0] != 0) == shift) {
            return (char)c;
        }
    }
    return 0;
}

static const struct {
    const char *name;
    uint8_t code;
} named_keys[] = {
    {"ENTER", KEY_ENTER}, {"TAB", KEY_TAB}, {"ESC", KEY_ESC},
    {"BACKSPACE", KEY_BACKSPACE}, {"DELETE", KEY_DELETE}, {"SPACE", KEY_SPACE},
    {"UP", KEY_UP}, {"DOWN", KEY_DOWN}, {"LEFT", KEY_LEFT}, {"RIGHT", KEY_RIGHT},
    {"HOME", KEY_HOME}, {"END", KEY_END}, {"PAGEUP", KEY_PAGEUP}, {"PAGEDOWN", KEY_PAGEDOWN},
    {"GRAVE", KEY_GRAVE}, {"SLASH", KEY_SLASH},
};

static const struct {
    const char *name;
    uint8_t bit;
} named_mods[] = {
    {"CTRL", KMOD_CTRL}, {"SHIFT", KMOD_SHIFT}, {"ALT", KMOD_ALT}, {"GUI", KMOD_GUI},
};

/* Usage code for one non-modifier token, or 0. */
static uint8_t token_code(const char *t, size_t n)
{
    for (size_t i = 0; i < sizeof(named_keys) / sizeof(named_keys[0]); i++) {
        if (str_ieq(t, n, named_keys[i].name)) {
            return named_keys[i].code;
        }
    }
    if ((n == 2 || n == 3) && (t[0] == 'F' || t[0] == 'f')) {
        int f = 0;
        for (size_t i = 1; i < n; i++) {
            if (t[i] < '0' || t[i] > '9') {
                return 0;
            }
            f = f * 10 + (t[i] - '0');
        }
        return f >= 1 && f <= 12 ? (uint8_t)(KEY_F1 + f - 1) : 0;
    }
    if (n == 1) {
        char c = t[0];
        if (c >= 'a' && c <= 'z') {
            return (uint8_t)(KEY_A + (c - 'a'));
        }
        if (c >= 'A' && c <= 'Z') {
            return (uint8_t)(KEY_A + (c - 'A'));
        }
        if (c == '0') {
            return KEY_0;
        }
        if (c >= '1' && c <= '9') {
            return (uint8_t)(KEY_1 + (c - '1'));
        }
    }
    return 0;
}

bool keys_parse_combo(const char *s, size_t len, uint8_t *mod, uint8_t *code)
{
    *mod = 0;
    *code = 0;
    size_t start = 0;
    while (start <= len) {
        size_t end = start;
        while (end < len && s[end] != '+') {
            end++;
        }
        const char *t = s + start;
        size_t n = end - start;
        if (n == 0) {
            return false;
        }
        bool is_mod = false;
        for (size_t i = 0; i < sizeof(named_mods) / sizeof(named_mods[0]); i++) {
            if (str_ieq(t, n, named_mods[i].name)) {
                *mod |= named_mods[i].bit;
                is_mod = true;
            }
        }
        if (!is_mod) {
            uint8_t k = token_code(t, n);
            if (!k || *code) {
                return false;
            }
            *code = k;
        }
        start = end + 1;
    }
    return *code != 0;
}

static const char *const rows[] = {"qwertyuiop", "asdfghjkl", "zxcvbnm"};

char keys_neighbour(char c, uint32_t r)
{
    bool upper = c >= 'A' && c <= 'Z';
    char lc = upper ? (char)(c - 'A' + 'a') : c;
    for (int row = 0; row < 3; row++) {
        const char *p = strchr(rows[row], lc);
        if (!p || lc == '\0') {
            continue;
        }
        size_t n = strlen(rows[row]);
        size_t i = (size_t)(p - rows[row]);
        char out;
        if (i == 0) {
            out = rows[row][1];
        } else if (i == n - 1 || (r & 1)) {
            out = rows[row][i - 1];
        } else {
            out = rows[row][i + 1];
        }
        return upper ? (char)(out - 'a' + 'A') : out;
    }
    return 0;
}

int keys_finger(char c)
{
    static const char *const fingers[8] = {
        "`1qaz", "2wsx", "3edc", "45rtfgvb", "67yuhjnm", "8ik,", "9ol.", "0p;/-=[]'\\",
    };
    uint8_t mod, code;
    if (!keys_ascii(c, &mod, &code)) {
        return -1;
    }
    char base = keys_to_ascii(0, code); /* unshifted key carrying c */
    if (!base || base == ' ' || base == '\n' || base == '\t') {
        return -1;
    }
    for (int f = 0; f < 8; f++) {
        if (strchr(fingers[f], base)) {
            return f;
        }
    }
    return -1;
}
