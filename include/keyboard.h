/**
 * keyboard.h - PS/2 Keyboard Driver
 * 
 * Features:
 *   - Scancode Set 1 (default BIOS mode)
 *   - Ring buffer for key events
 *   - EventChains integration
 *   - Modifier key tracking (Shift, Ctrl, Alt)
 *   - Full punctuation support
 *
 * Target: PS/2 keyboard (Compaq Armada E500)
 */

#ifndef KEYBOARD_H
#define KEYBOARD_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "idt.h"

/* ============================================================================
 * Keyboard Port Constants
 * ============================================================================ */

#define KB_DATA_PORT    0x60    /* Read scancode */
#define KB_STATUS_PORT  0x64    /* Read status */
#define KB_CMD_PORT     0x64    /* Write command */

/* Status register bits */
#define KB_STATUS_OUTPUT    0x01    /* Output buffer full (can read) */
#define KB_STATUS_INPUT     0x02    /* Input buffer full (wait before write) */
#define KB_STATUS_SYSTEM    0x04    /* System flag */
#define KB_STATUS_CMD       0x08    /* Command/data (0=data, 1=command) */
#define KB_STATUS_TIMEOUT   0x40    /* Timeout error */
#define KB_STATUS_PARITY    0x80    /* Parity error */

/* Special scancodes */
#define SC_EXTENDED         0xE0    /* Extended key prefix */
#define SC_RELEASE          0x80    /* Release bit (OR'd with scancode) */

/* ============================================================================
 * Key Codes - Internal representation
 * ============================================================================ */

typedef enum {
    KEY_UNKNOWN = 0,

    /* Special keys */
    KEY_ESCAPE = 1,
    KEY_BACKSPACE,
    KEY_TAB,
    KEY_ENTER,
    KEY_LCTRL,
    KEY_LSHIFT,
    KEY_RSHIFT,
    KEY_LALT,
    KEY_CAPSLOCK,
    KEY_NUMLOCK,
    KEY_SCROLLLOCK,
    KEY_F1, KEY_F2, KEY_F3, KEY_F4, KEY_F5, KEY_F6,
    KEY_F7, KEY_F8, KEY_F9, KEY_F10, KEY_F11, KEY_F12,
    KEY_HOME, KEY_UP, KEY_PAGEUP,
    KEY_LEFT, KEY_RIGHT,
    KEY_END, KEY_DOWN, KEY_PAGEDOWN,
    KEY_INSERT, KEY_DELETE,

    /* Printable keys - use ASCII values for easy mapping */
    KEY_SPACE = ' ',
    KEY_QUOTE = '\'',
    KEY_COMMA = ',',
    KEY_MINUS = '-',
    KEY_PERIOD = '.',
    KEY_SLASH = '/',
    KEY_0 = '0', KEY_1, KEY_2, KEY_3, KEY_4, KEY_5, KEY_6, KEY_7, KEY_8, KEY_9,
    KEY_SEMICOLON = ';',
    KEY_EQUALS = '=',
    KEY_A = 'A', KEY_B, KEY_C, KEY_D, KEY_E, KEY_F, KEY_G, KEY_H, KEY_I, KEY_J,
    KEY_K, KEY_L, KEY_M, KEY_N, KEY_O, KEY_P, KEY_Q, KEY_R, KEY_S, KEY_T,
    KEY_U, KEY_V, KEY_W, KEY_X, KEY_Y, KEY_Z,
    KEY_LBRACKET = '[',
    KEY_BACKSLASH = '\\',
    KEY_RBRACKET = ']',
    KEY_BACKTICK = '`',

    KEY_MAX
} keycode_t;

/* Modifier flags */
#define MOD_LSHIFT      0x01
#define MOD_RSHIFT      0x02
#define MOD_SHIFT       (MOD_LSHIFT | MOD_RSHIFT)
#define MOD_LCTRL       0x04
#define MOD_RCTRL       0x08
#define MOD_CTRL        (MOD_LCTRL | MOD_RCTRL)
#define MOD_LALT        0x10
#define MOD_RALT        0x20
#define MOD_ALT         (MOD_LALT | MOD_RALT)
#define MOD_CAPSLOCK    0x40
#define MOD_NUMLOCK     0x80

/* Key event */
typedef struct {
    keycode_t   keycode;        /* Our internal key code */
    uint8_t     scancode;       /* Raw scancode */
    uint8_t     modifiers;      /* Active modifiers */
    char        ascii;          /* ASCII character (0 if none) */
    bool        pressed;        /* true=press, false=release */
    bool        extended;       /* Extended key (E0 prefix) */
} key_event_t;

/* ============================================================================
 * Ring Buffer for Key Events
 * ============================================================================ */

#define KB_BUFFER_SIZE 64

typedef struct {
    key_event_t events[KB_BUFFER_SIZE];
    volatile uint8_t head;
    volatile uint8_t tail;
} kb_buffer_t;

static inline void kb_buffer_init(kb_buffer_t *buf) {
    buf->head = 0;
    buf->tail = 0;
}

static inline bool kb_buffer_empty(kb_buffer_t *buf) {
    return buf->head == buf->tail;
}

static inline bool kb_buffer_full(kb_buffer_t *buf) {
    return ((buf->head + 1) % KB_BUFFER_SIZE) == buf->tail;
}

static inline bool kb_buffer_push(kb_buffer_t *buf, key_event_t *event) {
    if (kb_buffer_full(buf)) return false;
    buf->events[buf->head] = *event;
    buf->head = (buf->head + 1) % KB_BUFFER_SIZE;
    return true;
}

static inline bool kb_buffer_pop(kb_buffer_t *buf, key_event_t *event) {
    if (kb_buffer_empty(buf)) return false;
    *event = buf->events[buf->tail];
    buf->tail = (buf->tail + 1) % KB_BUFFER_SIZE;
    return true;
}

/* ============================================================================
 * Scancode to ASCII Translation (Direct)
 *
 * This maps PS/2 Scancode Set 1 directly to ASCII.
 * Much simpler and more reliable than keycode intermediate.
 * ============================================================================ */

/* Unshifted ASCII for each scancode (0x00-0x58) */
static const char scancode_to_ascii_unshift[0x60] = {
/*        0     1     2     3     4     5     6     7     8     9     A     B     C     D     E     F  */
/* 0x00 */ 0,    0,   '1', '2',  '3', '4',  '5', '6',  '7', '8',  '9', '0',  '-', '=',  '\b', '\t',
/* 0x10 */'q',  'w',  'e', 'r',  't', 'y',  'u', 'i',  'o', 'p',  '[', ']',  '\n', 0,   'a',  's',
/* 0x20 */'d',  'f',  'g', 'h',  'j', 'k',  'l', ';', '\'', '`',   0,  '\\', 'z', 'x',  'c',  'v',
/* 0x30 */'b',  'n',  'm', ',',  '.', '/',   0,  '*',   0,  ' ',   0,   0,    0,   0,    0,    0,
/* 0x40 */ 0,    0,    0,   0,    0,   0,    0,  '7',  '8', '9',  '-', '4',  '5', '6',  '+',  '1',
/* 0x50 */'2',  '3',  '0', '.',   0,   0,    0,   0,    0,   0,    0,   0,    0,   0,    0,    0,
};

/* Shifted ASCII for each scancode (0x00-0x58) */
static const char scancode_to_ascii_shift[0x60] = {
/*        0     1     2     3     4     5     6     7     8     9     A     B     C     D     E     F  */
/* 0x00 */ 0,    0,   '!', '@',  '#', '$',  '%', '^',  '&', '*',  '(', ')',  '_', '+',  '\b', '\t',
/* 0x10 */'Q',  'W',  'E', 'R',  'T', 'Y',  'U', 'I',  'O', 'P',  '{', '}',  '\n', 0,   'A',  'S',
/* 0x20 */'D',  'F',  'G', 'H',  'J', 'K',  'L', ':', '"',  '~',   0,  '|',  'Z', 'X',  'C',  'V',
/* 0x30 */'B',  'N',  'M', '<',  '>', '?',   0,  '*',   0,  ' ',   0,   0,    0,   0,    0,    0,
/* 0x40 */ 0,    0,    0,   0,    0,   0,    0,  '7',  '8', '9',  '-', '4',  '5', '6',  '+',  '1',
/* 0x50 */'2',  '3',  '0', '.',   0,   0,    0,   0,    0,   0,    0,   0,    0,   0,    0,    0,
};

/* Scancode to keycode (for special key handling) */
static const keycode_t scancode_to_keycode[0x60] = {
/*        0              1              2       3       4       5       6       7       8       9       A             B             C            D              E              F  */
/* 0x00 */ KEY_UNKNOWN,  KEY_ESCAPE,    KEY_1,  KEY_2,  KEY_3,  KEY_4,  KEY_5,  KEY_6,  KEY_7,  KEY_8,  KEY_9,        KEY_0,        KEY_MINUS,   KEY_EQUALS,    KEY_BACKSPACE, KEY_TAB,
/* 0x10 */ KEY_Q,        KEY_W,         KEY_E,  KEY_R,  KEY_T,  KEY_Y,  KEY_U,  KEY_I,  KEY_O,  KEY_P,  KEY_LBRACKET, KEY_RBRACKET, KEY_ENTER,   KEY_LCTRL,     KEY_A,         KEY_S,
/* 0x20 */ KEY_D,        KEY_F,         KEY_G,  KEY_H,  KEY_J,  KEY_K,  KEY_L,  KEY_SEMICOLON, KEY_QUOTE, KEY_BACKTICK, KEY_LSHIFT, KEY_BACKSLASH, KEY_Z,      KEY_X,         KEY_C,         KEY_V,
/* 0x30 */ KEY_B,        KEY_N,         KEY_M,  KEY_COMMA, KEY_PERIOD, KEY_SLASH, KEY_RSHIFT, KEY_UNKNOWN, KEY_LALT, KEY_SPACE, KEY_CAPSLOCK, KEY_F1, KEY_F2, KEY_F3,        KEY_F4,        KEY_F5,
/* 0x40 */ KEY_F6,       KEY_F7,        KEY_F8, KEY_F9, KEY_F10, KEY_NUMLOCK, KEY_SCROLLLOCK, KEY_HOME, KEY_UP, KEY_PAGEUP, KEY_UNKNOWN, KEY_LEFT, KEY_UNKNOWN, KEY_RIGHT, KEY_UNKNOWN, KEY_END,
/* 0x50 */ KEY_DOWN,     KEY_PAGEDOWN,  KEY_INSERT, KEY_DELETE, KEY_UNKNOWN, KEY_UNKNOWN, KEY_UNKNOWN, KEY_F11, KEY_F12, KEY_UNKNOWN, KEY_UNKNOWN, KEY_UNKNOWN, KEY_UNKNOWN, KEY_UNKNOWN, KEY_UNKNOWN, KEY_UNKNOWN,
};

/* ============================================================================
 * Keyboard State
 * ============================================================================ */

typedef struct {
    kb_buffer_t     buffer;
    uint8_t         modifiers;
    bool            extended;       /* E0 prefix received */
    bool            initialized;
} keyboard_t;

/* Global keyboard state */
static keyboard_t g_keyboard;

/* Event callback - set by kernel to fire EventChain events */
static void (*g_kb_event_callback)(char ascii, uint8_t scancode, uint8_t modifiers, bool pressed) = NULL;

static inline void keyboard_set_event_callback(
    void (*callback)(char ascii, uint8_t scancode, uint8_t modifiers, bool pressed)) {
    g_kb_event_callback = callback;
}

/* ============================================================================
 * Keyboard Driver Implementation
 * ============================================================================ */

/* Convert scancode to key event */
static void kb_process_scancode(keyboard_t *kb, uint8_t scancode) {
    key_event_t event = {0};

    /* Check for extended prefix */
    if (scancode == SC_EXTENDED) {
        kb->extended = true;
        return;
    }

    /* Determine press/release */
    event.pressed = !(scancode & SC_RELEASE);
    event.scancode = scancode & 0x7F;
    event.extended = kb->extended;
    kb->extended = false;

    /* Get keycode */
    if (event.scancode < 0x60) {
        event.keycode = scancode_to_keycode[event.scancode];
    } else {
        event.keycode = KEY_UNKNOWN;
    }

    /* Handle extended keys (arrows, etc.) */
    if (event.extended) {
        switch (event.scancode) {
            case 0x48: event.keycode = KEY_UP; break;
            case 0x4B: event.keycode = KEY_LEFT; break;
            case 0x4D: event.keycode = KEY_RIGHT; break;
            case 0x50: event.keycode = KEY_DOWN; break;
            case 0x47: event.keycode = KEY_HOME; break;
            case 0x4F: event.keycode = KEY_END; break;
            case 0x49: event.keycode = KEY_PAGEUP; break;
            case 0x51: event.keycode = KEY_PAGEDOWN; break;
            case 0x52: event.keycode = KEY_INSERT; break;
            case 0x53: event.keycode = KEY_DELETE; break;
        }
    }

    /* Update modifier state */
    switch (event.keycode) {
        case KEY_LSHIFT:
            if (event.pressed) kb->modifiers |= MOD_LSHIFT;
            else kb->modifiers &= ~MOD_LSHIFT;
            break;
        case KEY_RSHIFT:
            if (event.pressed) kb->modifiers |= MOD_RSHIFT;
            else kb->modifiers &= ~MOD_RSHIFT;
            break;
        case KEY_LCTRL:
            if (event.pressed) kb->modifiers |= MOD_LCTRL;
            else kb->modifiers &= ~MOD_LCTRL;
            break;
        case KEY_LALT:
            if (event.pressed) kb->modifiers |= MOD_LALT;
            else kb->modifiers &= ~MOD_LALT;
            break;
        case KEY_CAPSLOCK:
            if (event.pressed) kb->modifiers ^= MOD_CAPSLOCK;
            break;
        case KEY_NUMLOCK:
            if (event.pressed) kb->modifiers ^= MOD_NUMLOCK;
            break;
        default:
            break;
    }

    event.modifiers = kb->modifiers;

    /* Translate to ASCII - direct from scancode! */
    event.ascii = 0;
    if (event.pressed && event.scancode < 0x60) {
        bool shift = (kb->modifiers & MOD_SHIFT) != 0;
        bool caps = (kb->modifiers & MOD_CAPSLOCK) != 0;

        /* Get base ASCII */
        char c;
        if (shift) {
            c = scancode_to_ascii_shift[event.scancode];
        } else {
            c = scancode_to_ascii_unshift[event.scancode];
        }

        /* Handle caps lock for letters only */
        if (caps && c >= 'a' && c <= 'z') {
            c -= 32;  /* To uppercase */
        } else if (caps && c >= 'A' && c <= 'Z') {
            c += 32;  /* To lowercase (caps inverts shift for letters) */
        }

        event.ascii = c;
    }

    /* Add to buffer */
    kb_buffer_push(&kb->buffer, &event);

    /* Fire EventChain events via callback */
    if (g_kb_event_callback) {
        g_kb_event_callback(event.ascii, event.scancode, event.modifiers, event.pressed);
    }
}

/* IRQ1 handler */
static void kb_irq_handler(int_frame_t *frame) {
    (void)frame;

    /* Read scancode */
    uint8_t scancode = inb(KB_DATA_PORT);

    /* Process it */
    kb_process_scancode(&g_keyboard, scancode);
}

/* Initialize keyboard */
static inline void keyboard_init(void) {
    // Initialize state
    kb_buffer_init(&g_keyboard.buffer);
    g_keyboard.modifiers = 0;
    g_keyboard.extended = false;
    g_keyboard.initialized = true;

    // Register IRQ handler
    irq_register(IRQ_KEYBOARD, kb_irq_handler);

    // Enable keyboard IRQ
    pic_enable_irq(IRQ_KEYBOARD);

    // Flush any pending data
    while (inb(KB_STATUS_PORT) & KB_STATUS_OUTPUT) {
        inb(KB_DATA_PORT);
    }
}

/* Get next key event (non-blocking) */
static inline bool keyboard_get_event(key_event_t *event) {
    return kb_buffer_pop(&g_keyboard.buffer, event);
}

/* Get character (blocking) */
static inline char keyboard_getchar(void) {
    key_event_t event;

    while (1) {
        /* Enable interrupts while waiting */
        __asm__ volatile ("sti");
        __asm__ volatile ("hlt");

        /* Check for key */
        while (kb_buffer_pop(&g_keyboard.buffer, &event)) {
            if (event.pressed && event.ascii != 0) {
                return event.ascii;
            }
        }
    }
}

/* Check if key available (non-blocking) */
static inline bool keyboard_has_key(void) {
    return !kb_buffer_empty(&g_keyboard.buffer);
}

#endif /* KEYBOARD_H */