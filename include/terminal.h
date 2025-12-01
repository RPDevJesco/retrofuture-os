/**
 * terminal.h - Retro-Future Phosphor Terminal
 * 
 * A CRT-style terminal with:
 *   - Phosphor colors (green, amber, white options)
 *   - Scanline effect
 *   - Customizable font
 *   - Smooth character rendering
 *   - Blinking cursor
 * 
 * Designed for Pentium III - efficient pixel operations
 */

#ifndef TERMINAL_H
#define TERMINAL_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "boot_info.h"

/* ============================================================================
 * Terminal Configuration
 * ============================================================================ */

/* Phosphor Color Schemes */
typedef enum {
    PHOSPHOR_GREEN,         // Classic green CRT
    PHOSPHOR_AMBER,         // Amber monochrome
    PHOSPHOR_WHITE,         // White on black
    PHOSPHOR_CYAN,          // Sci-fi cyan
    PHOSPHOR_CUSTOM         // User-defined
} phosphor_scheme_t;

/* Color values for each scheme (bright, medium, dim, dark) */
typedef struct {
    uint32_t bright;        // Brightest text
    uint32_t medium;        // Normal text
    uint32_t dim;           // Dim text / scanline
    uint32_t dark;          // Background
    uint32_t cursor;        // Cursor color
} phosphor_colors_t;

/* Terminal Configuration */
typedef struct {
    phosphor_scheme_t scheme;
    phosphor_colors_t colors;
    bool scanlines;         // Enable scanline effect
    bool cursor_blink;      // Enable cursor blinking
    uint32_t cursor_rate;   // Blink rate in frames
    uint8_t char_width;     // Character cell width
    uint8_t char_height;    // Character cell height
} terminal_config_t;

/* ============================================================================
 * Terminal State
 * ============================================================================ */

typedef struct {
    /* Framebuffer */
    uint32_t *framebuffer;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;         // In pixels

    /* Character grid */
    uint8_t cols;
    uint8_t rows;
    uint8_t cursor_x;
    uint8_t cursor_y;
    bool cursor_visible;
    
    /* Configuration */
    terminal_config_t config;
    
    /* Font */
    const uint8_t *font;
    uint8_t font_width;
    uint8_t font_height;
    
    /* Internal state */
    uint32_t frame_count;
    uint32_t *backbuffer;   // For double buffering (optional)
    
} terminal_t;

/* ============================================================================
 * Default Color Schemes
 * ============================================================================ */

static const phosphor_colors_t COLORS_GREEN = {
    .bright = 0x00FF66,     // Bright phosphor green
    .medium = 0x00CC44,     // Normal green
    .dim    = 0x003311,     // Dim for scanlines
    .dark   = 0x000800,     // Near-black with green tint
    .cursor = 0x00FF66
};

static const phosphor_colors_t COLORS_AMBER = {
    .bright = 0xFFBB00,     // Bright amber
    .medium = 0xCC9900,     // Normal amber
    .dim    = 0x332200,     // Dim amber
    .dark   = 0x0A0600,     // Dark amber tint
    .cursor = 0xFFBB00
};

static const phosphor_colors_t COLORS_WHITE = {
    .bright = 0xFFFFFF,
    .medium = 0xBBBBBB,
    .dim    = 0x333333,
    .dark   = 0x0A0A0A,
    .cursor = 0xFFFFFF
};

static const phosphor_colors_t COLORS_CYAN = {
    .bright = 0x00FFFF,     // Sci-fi cyan
    .medium = 0x00BBCC,
    .dim    = 0x002233,
    .dark   = 0x000808,
    .cursor = 0x00FFFF
};

/* ============================================================================
 * 8x16 Bitmap Font (partial - first 128 ASCII chars)
 * Each character is 8 pixels wide, 16 pixels tall
 * Stored as 16 bytes per character (1 byte per row)
 * ============================================================================ */

/* Font will be included from separate file or embedded */
extern const uint8_t terminal_font_8x16[];

/* ============================================================================
 * Terminal API
 * ============================================================================ */

/* ============================================================================
 * Implementation
 * ============================================================================ */

/* Fast pixel plotting (inlined for performance) */
static inline void term_plot_pixel(terminal_t *term, uint32_t x, uint32_t y, uint32_t color) {
    if (x < term->width && y < term->height) {
        term->framebuffer[y * term->pitch + x] = color;
    }
}

/* Draw horizontal line */
static inline void term_hline(terminal_t *term, uint32_t x, uint32_t y, uint32_t len, uint32_t color) {
    if (y >= term->height) return;
    uint32_t *row = &term->framebuffer[y * term->pitch + x];
    uint32_t end = (x + len > term->width) ? term->width - x : len;
    for (uint32_t i = 0; i < end; i++) {
        row[i] = color;
    }
}

/* Fill rectangle */
static inline void term_fill_rect(terminal_t *term, uint32_t x, uint32_t y,
                                   uint32_t w, uint32_t h, uint32_t color) {
    for (uint32_t row = y; row < y + h && row < term->height; row++) {
        term_hline(term, x, row, w, color);
    }
}

/**
 * Draw single character at pixel position
 */
static void term_draw_char(terminal_t *term, uint32_t px, uint32_t py,
                           char c, uint32_t fg, uint32_t bg) {
    if ((uint8_t)c >= 128) c = '?';  // ASCII only

    const uint8_t *glyph = &term->font[(uint8_t)c * term->font_height];

    for (uint8_t row = 0; row < term->font_height; row++) {
        uint32_t y = py + row;
        if (y >= term->height) break;

        uint8_t bits = glyph[row];
        uint32_t *pixel = &term->framebuffer[y * term->pitch + px];

        // Apply scanline effect on odd rows
        uint32_t line_fg = fg;
        uint32_t line_bg = bg;
        if (term->config.scanlines && (y & 1)) {
            line_fg = term->config.colors.dim;
            line_bg = term->config.colors.dark;
        }

        for (uint8_t col = 0; col < term->font_width && (px + col) < term->width; col++) {
            *pixel++ = (bits & (0x80 >> col)) ? line_fg : line_bg;
        }
    }
}

/**
 * Draw cursor at current position
 */
static void term_draw_cursor(terminal_t *term) {
    if (!term->cursor_visible) return;

    uint32_t px = term->cursor_x * term->font_width;
    uint32_t py = term->cursor_y * term->font_height;

    // Block cursor on last two rows of character cell
    uint32_t cursor_start = py + term->font_height - 3;
    uint32_t cursor_end = py + term->font_height;

    for (uint32_t y = cursor_start; y < cursor_end && y < term->height; y++) {
        if (term->config.scanlines && (y & 1)) continue;  // Skip scanlines
        term_hline(term, px, y, term->font_width, term->config.colors.cursor);
    }
}

/**
 * Erase cursor at current position (redraw background)
 */
static void term_erase_cursor(terminal_t *term) {
    uint32_t px = term->cursor_x * term->font_width;
    uint32_t py = term->cursor_y * term->font_height;

    uint32_t cursor_start = py + term->font_height - 3;
    uint32_t cursor_end = py + term->font_height;

    for (uint32_t y = cursor_start; y < cursor_end && y < term->height; y++) {
        uint32_t bg = (term->config.scanlines && (y & 1))
                      ? term->config.colors.dark
                      : term->config.colors.dark;
        term_hline(term, px, y, term->font_width, bg);
    }
}

/* ============================================================================
 * Public Implementation
 * ============================================================================ */

static inline void terminal_clear(terminal_t *term) {
    uint32_t bg = term->config.colors.dark;

    for (uint32_t y = 0; y < term->height; y++) {
        uint32_t *row = &term->framebuffer[y * term->pitch];
        for (uint32_t x = 0; x < term->width; x++) {
            row[x] = bg;
        }
    }

    term->cursor_x = 0;
    term->cursor_y = 0;
}

static inline void terminal_scroll(terminal_t *term) {
    uint32_t line_height = term->font_height;
    uint32_t copy_lines = (term->rows - 1) * line_height;

    // Move framebuffer up by one text line
    uint32_t *dst = term->framebuffer;
    uint32_t *src = &term->framebuffer[line_height * term->pitch];

    for (uint32_t y = 0; y < copy_lines; y++) {
        for (uint32_t x = 0; x < term->width; x++) {
            dst[x] = src[x];
        }
        dst += term->pitch;
        src += term->pitch;
    }

    // Clear last line
    uint32_t clear_start = copy_lines;
    for (uint32_t y = clear_start; y < term->height; y++) {
        uint32_t *row = &term->framebuffer[y * term->pitch];
        uint32_t bg = term->config.colors.dark;
        for (uint32_t x = 0; x < term->width; x++) {
            row[x] = bg;
        }
    }
}

static inline void terminal_init(terminal_t *term, boot_info_t *bi, terminal_config_t *cfg) {
    term->framebuffer = (uint32_t *)(uintptr_t)bi->framebuffer;
    term->width = bi->width;
    term->height = bi->height;
    term->pitch = bi->pitch / 4;  // Convert bytes to pixels (32bpp)

    // Set up configuration
    if (cfg) {
        term->config = *cfg;
    } else {
        // Defaults
        term->config.scheme = PHOSPHOR_GREEN;
        term->config.colors = COLORS_GREEN;
        term->config.scanlines = true;
        term->config.cursor_blink = true;
        term->config.cursor_rate = 30;
    }

    // Default 8x16 font
    term->font = terminal_font_8x16;
    term->font_width = 8;
    term->font_height = 16;

    // Calculate grid size
    term->cols = term->width / term->font_width;
    term->rows = term->height / term->font_height;

    // Initialize cursor
    term->cursor_x = 0;
    term->cursor_y = 0;
    term->cursor_visible = true;
    term->frame_count = 0;

    // Clear to background
    terminal_clear(term);
}

static inline void terminal_putchar(terminal_t *term, char c) {
    // Handle control characters
    switch (c) {
        case '\n':
            term_erase_cursor(term);
            term->cursor_x = 0;
            term->cursor_y++;
            break;
        case '\r':
            term_erase_cursor(term);
            term->cursor_x = 0;
            break;
        case '\t':
            term_erase_cursor(term);
            term->cursor_x = (term->cursor_x + 8) & ~7;
            break;
        case '\b':
            if (term->cursor_x > 0) {
                term_erase_cursor(term);
                term->cursor_x--;
                // Erase character
                term_draw_char(term,
                    term->cursor_x * term->font_width,
                    term->cursor_y * term->font_height,
                    ' ', term->config.colors.dark, term->config.colors.dark);
            }
            break;
        default:
            if (c >= 32 && c < 127) {
                term_draw_char(term,
                    term->cursor_x * term->font_width,
                    term->cursor_y * term->font_height,
                    c,
                    term->config.colors.medium,
                    term->config.colors.dark);
                term->cursor_x++;
            }
            break;
    }

    // Handle line wrap
    if (term->cursor_x >= term->cols) {
        term->cursor_x = 0;
        term->cursor_y++;
    }

    // Handle scroll
    if (term->cursor_y >= term->rows) {
        terminal_scroll(term);
        term->cursor_y = term->rows - 1;
    }

    // Redraw cursor
    term_draw_cursor(term);
}

static inline void terminal_puts(terminal_t *term, const char *s) {
    while (*s) {
        terminal_putchar(term, *s++);
    }
}

static inline void terminal_set_cursor(terminal_t *term, uint8_t x, uint8_t y) {
    term_erase_cursor(term);
    term->cursor_x = (x < term->cols) ? x : term->cols - 1;
    term->cursor_y = (y < term->rows) ? y : term->rows - 1;
    term_draw_cursor(term);
}

static inline void terminal_update(terminal_t *term) {
    term->frame_count++;

    if (term->config.cursor_blink) {
        if ((term->frame_count / term->config.cursor_rate) & 1) {
            if (term->cursor_visible) {
                term_erase_cursor(term);
                term->cursor_visible = false;
            }
        } else {
            if (!term->cursor_visible) {
                term->cursor_visible = true;
                term_draw_cursor(term);
            }
        }
    }
}

static inline void terminal_set_scheme(terminal_t *term, phosphor_scheme_t scheme) {
    term->config.scheme = scheme;
    switch (scheme) {
        case PHOSPHOR_GREEN: term->config.colors = COLORS_GREEN; break;
        case PHOSPHOR_AMBER: term->config.colors = COLORS_AMBER; break;
        case PHOSPHOR_WHITE: term->config.colors = COLORS_WHITE; break;
        case PHOSPHOR_CYAN:  term->config.colors = COLORS_CYAN; break;
        default: break;
    }
}

/**
 * Draw retro-style box border
 */
static inline void terminal_draw_border(terminal_t *term, const char *title) {
    // Box drawing characters (we'll use ASCII approximations)
    const char TL = '+', TR = '+', BL = '+', BR = '+';
    const char H = '-', V = '|';

    // Top border
    terminal_set_cursor(term, 0, 0);
    terminal_putchar(term, TL);
    for (uint8_t i = 1; i < term->cols - 1; i++) {
        terminal_putchar(term, H);
    }
    terminal_putchar(term, TR);

    // Title centered
    if (title) {
        uint8_t len = 0;
        while (title[len]) len++;
        uint8_t start = (term->cols - len - 4) / 2;
        terminal_set_cursor(term, start, 0);
        terminal_puts(term, "[ ");
        terminal_puts(term, title);
        terminal_puts(term, " ]");
    }

    // Side borders
    for (uint8_t y = 1; y < term->rows - 1; y++) {
        terminal_set_cursor(term, 0, y);
        terminal_putchar(term, V);
        terminal_set_cursor(term, term->cols - 1, y);
        terminal_putchar(term, V);
    }

    // Bottom border
    terminal_set_cursor(term, 0, term->rows - 1);
    terminal_putchar(term, BL);
    for (uint8_t i = 1; i < term->cols - 1; i++) {
        terminal_putchar(term, H);
    }
    terminal_putchar(term, BR);

    // Position cursor inside border
    terminal_set_cursor(term, 2, 2);
}

#endif /* TERMINAL_H */