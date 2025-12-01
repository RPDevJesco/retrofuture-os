/**
 * keyboard.h - PS/2 Keyboard Driver
 * 
 * Features:
 *   - Scancode Set 1 (default BIOS mode)
 *   - Ring buffer for key events
 *   - EventChains integration
 *   - Modifier key tracking (Shift, Ctrl, Alt)
 *   - Key repeat handling
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

#define KB_DATA_PORT    0x60    // Read scancode
#define KB_STATUS_PORT  0x64    // Read status
#define KB_CMD_PORT     0x64    // Write command

/* Status register bits */
#define KB_STATUS_OUTPUT    0x01    // Output buffer full (can read)
#define KB_STATUS_INPUT     0x02    // Input buffer full (wait before write)
#define KB_STATUS_SYSTEM    0x04    // System flag
#define KB_STATUS_CMD       0x08    // Command/data (0=data, 1=command)
#define KB_STATUS_TIMEOUT   0x40    // Timeout error
#define KB_STATUS_PARITY    0x80    // Parity error

/* Special scancodes */
#define SC_EXTENDED         0xE0    // Extended key prefix
#define SC_RELEASE          0x80    // Release bit (OR'd with scancode)

/* ============================================================================
 * Key Event Structure
 * ============================================================================ */

/* Key codes (our internal representation) */
typedef enum {
    KEY_UNKNOWN = 0,
    
    // Printable keys (ASCII-like)
    KEY_SPACE = 32,
    KEY_0 = '0', KEY_1, KEY_2, KEY_3, KEY_4, KEY_5, KEY_6, KEY_7, KEY_8, KEY_9,
    KEY_A = 'A', KEY_B, KEY_C, KEY_D, KEY_E, KEY_F, KEY_G, KEY_H, KEY_I, KEY_J,
    KEY_K, KEY_L, KEY_M, KEY_N, KEY_O, KEY_P, KEY_Q, KEY_R, KEY_S, KEY_T,
    KEY_U, KEY_V, KEY_W, KEY_X, KEY_Y, KEY_Z,
    
    // Special keys (high values)
    KEY_ESCAPE = 256,
    KEY_BACKSPACE,
    KEY_TAB,
    KEY_ENTER,
    KEY_LCTRL,
    KEY_LSHIFT,
    KEY_RSHIFT,
    KEY_LALT,
    KEY_CAPSLOCK,
    KEY_F1, KEY_F2, KEY_F3, KEY_F4, KEY_F5, KEY_F6,
    KEY_F7, KEY_F8, KEY_F9, KEY_F10, KEY_F11, KEY_F12,
    KEY_NUMLOCK,
    KEY_SCROLLLOCK,
    KEY_HOME,
    KEY_UP,
    KEY_PAGEUP,
    KEY_LEFT,
    KEY_RIGHT,
    KEY_END,
    KEY_DOWN,
    KEY_PAGEDOWN,
    KEY_INSERT,
    KEY_DELETE,
    
    // Punctuation
    KEY_MINUS,
    KEY_EQUALS,
    KEY_LBRACKET,
    KEY_RBRACKET,
    KEY_SEMICOLON,
    KEY_QUOTE,
    KEY_BACKTICK,
    KEY_BACKSLASH,
    KEY_COMMA,
    KEY_PERIOD,
    KEY_SLASH,
    
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
    keycode_t   keycode;        // Our internal key code
    uint8_t     scancode;       // Raw scancode
    uint8_t     modifiers;      // Active modifiers
    char        ascii;          // ASCII character (0 if none)
    bool        pressed;        // true=press, false=release
    bool        extended;       // Extended key (E0 prefix)
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
 * Scancode to Keycode Translation Tables
 * ============================================================================ */

/* Scancode Set 1 - Regular keys */
static const keycode_t scancode_to_keycode[128] = {
    KEY_UNKNOWN,    // 0x00
    KEY_ESCAPE,     // 0x01
    KEY_1,          // 0x02
    KEY_2,          // 0x03
    KEY_3,          // 0x04
    KEY_4,          // 0x05
    KEY_5,          // 0x06
    KEY_6,          // 0x07
    KEY_7,          // 0x08
    KEY_8,          // 0x09
    KEY_9,          // 0x0A
    KEY_0,          // 0x0B
    KEY_MINUS,      // 0x0C
    KEY_EQUALS,     // 0x0D
    KEY_BACKSPACE,  // 0x0E
    KEY_TAB,        // 0x0F
    KEY_Q,          // 0x10
    KEY_W,          // 0x11
    KEY_E,          // 0x12
    KEY_R,          // 0x13
    KEY_T,          // 0x14
    KEY_Y,          // 0x15
    KEY_U,          // 0x16
    KEY_I,          // 0x17
    KEY_O,          // 0x18
    KEY_P,          // 0x19
    KEY_LBRACKET,   // 0x1A
    KEY_RBRACKET,   // 0x1B
    KEY_ENTER,      // 0x1C
    KEY_LCTRL,      // 0x1D
    KEY_A,          // 0x1E
    KEY_S,          // 0x1F
    KEY_D,          // 0x20
    KEY_F,          // 0x21
    KEY_G,          // 0x22
    KEY_H,          // 0x23
    KEY_J,          // 0x24
    KEY_K,          // 0x25
    KEY_L,          // 0x26
    KEY_SEMICOLON,  // 0x27
    KEY_QUOTE,      // 0x28
    KEY_BACKTICK,   // 0x29
    KEY_LSHIFT,     // 0x2A
    KEY_BACKSLASH,  // 0x2B
    KEY_Z,          // 0x2C
    KEY_X,          // 0x2D
    KEY_C,          // 0x2E
    KEY_V,          // 0x2F
    KEY_B,          // 0x30
    KEY_N,          // 0x31
    KEY_M,          // 0x32
    KEY_COMMA,      // 0x33
    KEY_PERIOD,     // 0x34
    KEY_SLASH,      // 0x35
    KEY_RSHIFT,     // 0x36
    KEY_UNKNOWN,    // 0x37 (Keypad *)
    KEY_LALT,       // 0x38
    KEY_SPACE,      // 0x39
    KEY_CAPSLOCK,   // 0x3A
    KEY_F1,         // 0x3B
    KEY_F2,         // 0x3C
    KEY_F3,         // 0x3D
    KEY_F4,         // 0x3E
    KEY_F5,         // 0x3F
    KEY_F6,         // 0x40
    KEY_F7,         // 0x41
    KEY_F8,         // 0x42
    KEY_F9,         // 0x43
    KEY_F10,        // 0x44
    KEY_NUMLOCK,    // 0x45
    KEY_SCROLLLOCK, // 0x46
    KEY_HOME,       // 0x47 (Keypad 7)
    KEY_UP,         // 0x48 (Keypad 8)
    KEY_PAGEUP,     // 0x49 (Keypad 9)
    KEY_UNKNOWN,    // 0x4A (Keypad -)
    KEY_LEFT,       // 0x4B (Keypad 4)
    KEY_UNKNOWN,    // 0x4C (Keypad 5)
    KEY_RIGHT,      // 0x4D (Keypad 6)
    KEY_UNKNOWN,    // 0x4E (Keypad +)
    KEY_END,        // 0x4F (Keypad 1)
    KEY_DOWN,       // 0x50 (Keypad 2)
    KEY_PAGEDOWN,   // 0x51 (Keypad 3)
    KEY_INSERT,     // 0x52 (Keypad 0)
    KEY_DELETE,     // 0x53 (Keypad .)
    KEY_UNKNOWN,    // 0x54
    KEY_UNKNOWN,    // 0x55
    KEY_UNKNOWN,    // 0x56
    KEY_F11,        // 0x57
    KEY_F12,        // 0x58
    // Rest are KEY_UNKNOWN
};

/* ASCII lookup - unshifted */
static const char keycode_to_ascii[128] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,     // 0x00-0x0F
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,     // 0x10-0x1F
    ' ', 0, 0, 0, 0, 0, 0, '\'', 0, 0, 0, 0, ',', '-', '.', '/',  // 0x20-0x2F
    '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 0, ';', 0, '=', 0, 0,  // 0x30-0x3F
    0, 'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n', 'o',  // 0x40-0x4F
    'p', 'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z', '[', '\\', ']', 0, 0,  // 0x50-0x5F
    '`', 'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm', 'n', 'o',  // 0x60-0x6F
    'p', 'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z', 0, 0, 0, 0, 0   // 0x70-0x7F
};

/* ASCII lookup - shifted */
static const char keycode_to_ascii_shift[128] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,     // 0x00-0x0F
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,     // 0x10-0x1F
    ' ', 0, 0, 0, 0, 0, 0, '"', 0, 0, 0, 0, '<', '_', '>', '?',  // 0x20-0x2F
    ')', '!', '@', '#', '$', '%', '^', '&', '*', '(', 0, ':', 0, '+', 0, 0,  // 0x30-0x3F
    0, 'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M', 'N', 'O',  // 0x40-0x4F
    'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z', '{', '|', '}', 0, 0,  // 0x50-0x5F
    '~', 'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M', 'N', 'O',  // 0x60-0x6F
    'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z', 0, 0, 0, 0, 0   // 0x70-0x7F
};

/* ============================================================================
 * Keyboard State
 * ============================================================================ */

typedef struct {
    kb_buffer_t     buffer;
    uint8_t         modifiers;
    bool            extended;       // E0 prefix received
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

    // Check for extended prefix
    if (scancode == SC_EXTENDED) {
        kb->extended = true;
        return;
    }

    // Determine press/release
    event.pressed = !(scancode & SC_RELEASE);
    event.scancode = scancode & 0x7F;
    event.extended = kb->extended;
    kb->extended = false;

    // Translate to keycode
    if (event.scancode < 128) {
        event.keycode = scancode_to_keycode[event.scancode];
    } else {
        event.keycode = KEY_UNKNOWN;
    }

    // Handle extended keys (arrows, etc.)
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

    // Update modifier state
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

    // Translate to ASCII
    event.ascii = 0;
    if (event.pressed) {
        // Handle special keys first (keycodes > 128)
        if (event.keycode == KEY_ENTER) event.ascii = '\n';
        else if (event.keycode == KEY_TAB) event.ascii = '\t';
        else if (event.keycode == KEY_BACKSPACE) event.ascii = '\b';
        else if (event.keycode < 128) {
            // Regular printable keys
            bool shift = (kb->modifiers & MOD_SHIFT) != 0;
            bool caps = (kb->modifiers & MOD_CAPSLOCK) != 0;

            // For letters, caps lock inverts shift
            if (event.keycode >= KEY_A && event.keycode <= KEY_Z) {
                shift ^= caps;
            }

            if (shift) {
                event.ascii = keycode_to_ascii_shift[event.keycode];
            } else {
                event.ascii = keycode_to_ascii[event.keycode];
            }
        }
    }

    // Add to buffer
    kb_buffer_push(&kb->buffer, &event);

    // Fire EventChain events via callback
    if (g_kb_event_callback) {
        g_kb_event_callback(event.ascii, event.scancode, event.modifiers, event.pressed);
    }
}

/* IRQ1 handler */
static void kb_irq_handler(int_frame_t *frame) {
    (void)frame;

    // Read scancode
    uint8_t scancode = inb(KB_DATA_PORT);

    // Process it
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

/* Check if key event available */
static inline bool keyboard_has_event(void) {
    return !kb_buffer_empty(&g_keyboard.buffer);
}

/* Get next key event (non-blocking) */
static inline bool keyboard_get_event(key_event_t *event) {
    return kb_buffer_pop(&g_keyboard.buffer, event);
}

/* Get next character (blocking) */
static inline char keyboard_getchar(void) {
    key_event_t event;

    while (1) {
        // Wait for event (IRQ will fill the buffer)
        while (!keyboard_has_event()) {
            __asm__ volatile ("hlt");  // Sleep until interrupt
        }

        // Get event from buffer
        if (keyboard_get_event(&event)) {
            // Return ASCII if it's a key press with printable char
            if (event.pressed && event.ascii != 0) {
                return event.ascii;
            }
        }
    }
}

/* Non-blocking getchar (returns 0 if no char available) */
static inline char keyboard_getchar_nb(void) {
    key_event_t event;

    while (keyboard_get_event(&event)) {
        if (event.pressed && event.ascii != 0) {
            return event.ascii;
        }
    }

    return 0;
}

/* Get current modifier state */
static inline uint8_t keyboard_get_modifiers(void) {
    return g_keyboard.modifiers;
}

#endif /* KEYBOARD_H */