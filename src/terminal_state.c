// terminal_state.c
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <X11/Xft/Xft.h>
#include <X11/Xlib.h>
#include <X11/X.h>  // For GC and other X11 types
#include <utf8proc.h> // unicode

#include "terminal_state.h"
#include "draw.h"

// External globals from draw.c
extern Display *global_display;
extern XftFont *xft_font;
extern XftColor xft_color;
TerminalState term_state;
TerminalCell terminal_buffer[TERMINAL_ROWS][TERMINAL_COLS];
int scrollback_position = 0; // Initialize scrollback position

// prototypes
void handle_csi_sequence(const char *params, char cmd, TerminalState *state, Display *display);
void handle_erase_display(int mode, TerminalState *state);
void handle_erase_line(int mode, TerminalState *state);
void allocate_color(Display *display, TerminalState *state, int color_code);

// Add MAX and MIN macros
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define MIN(a, b) ((a) < (b) ? (a) : (b))

typedef enum {
    STATE_GROUND,    // Normal character processing
    STATE_ESCAPE,    // ESC received
    STATE_CSI,       // ESC[ received (Control Sequence Introducer)
    STATE_OSC,       // ESC] received (Operating System Command)
} ParserState;

typedef struct {
    ParserState state;
    char params[32]; // Buffer for CSI parameters
    size_t param_idx;
} AnsiParser;

void init_ansi_parser(AnsiParser *parser) {
    parser->state = STATE_GROUND;
    parser->param_idx = 0;
    memset(parser->params, 0, sizeof(parser->params));
}

void handle_ansi_char(AnsiParser *parser, char c, TerminalState *state, Display *display) {
    switch (parser->state) {
        case STATE_GROUND:
            if (c == '\x1B') {
                parser->state = STATE_ESCAPE;
            } else {
                put_char(c, state);
            }
            break;

        case STATE_ESCAPE:
            if (c == '[') {
                parser->state = STATE_CSI;
                parser->param_idx = 0;
            } else if (c == ']') {
                parser->state = STATE_OSC;
            } else {
                parser->state = STATE_GROUND;
            }
            break;

        case STATE_CSI:
            if ((c >= '0' && c <= '9') || c == ';') {
                if (parser->param_idx < sizeof(parser->params) - 1) {
                    parser->params[parser->param_idx++] = c;
                }
            } else {
                // Process the complete CSI sequence
                parser->params[parser->param_idx] = '\0';
                handle_csi_sequence(parser->params, c, state, display);
                parser->state = STATE_GROUND;
            }
            break;

        case STATE_OSC:
            // Handle OSC sequences (not implemented yet)
            if (c == '\x07' || c == '\x1B') { // BEL or ESC ends OSC
                parser->state = STATE_GROUND;
            }
            break;
    }
}

void handle_csi_sequence(const char *params, char cmd, TerminalState *state, Display *display) {
    int param_values[10] = {0};
    int param_count = 0;
    char *token = strtok((char *)params, ";");
    
    while (token != NULL && param_count < 10) {
        param_values[param_count++] = atoi(token);
        token = strtok(NULL, ";");
    }

    switch (cmd) {
        case 'm': // SGR - Select Graphic Rendition
            for (int i = 0; i < param_count; i++) {
                int code = param_values[i];
                if (code >= 30 && code <= 37) {
                    allocate_color(display, state, code);
                }
                // Handle other SGR codes (bold, underline, etc.)
            }
            break;

        case 'A': // Cursor Up
            state->row = MAX(0, state->row - (param_count ? param_values[0] : 1));
            break;

        case 'B': // Cursor Down
            state->row = MIN(TERMINAL_ROWS - 1, state->row + (param_count ? param_values[0] : 1));
            break;

        case 'C': // Cursor Forward
            state->col = MIN(TERMINAL_COLS - 1, state->col + (param_count ? param_values[0] : 1));
            break;

        case 'D': // Cursor Backward
            state->col = MAX(0, state->col - (param_count ? param_values[0] : 1));
            break;

        case 'H': // Cursor Position
            state->row = param_count > 0 ? MAX(0, MIN(param_values[0] - 1, TERMINAL_ROWS - 1)) : 0;
            state->col = param_count > 1 ? MAX(0, MIN(param_values[1] - 1, TERMINAL_COLS - 1)) : 0;
            break;

        case 'J': // Erase in Display
            handle_erase_display(param_count ? param_values[0] : 0, state);
            break;

        case 'K': // Erase in Line
            handle_erase_line(param_count ? param_values[0] : 0, state);
            break;

        // Add more CSI commands as needed
    }
}

void handle_erase_display(int mode, TerminalState *state) {
    switch (mode) {
        case 0: // Clear from cursor to end of screen
            for (int r = state->row; r < TERMINAL_ROWS; r++) {
                int start_col = (r == state->row) ? state->col : 0;
                for (int c = start_col; c < TERMINAL_COLS; c++) {
                    memset(terminal_buffer[r][c].c, 0, sizeof(terminal_buffer[r][c].c));
                }
            }
            break;

        case 1: // Clear from cursor to beginning of screen
            for (int r = 0; r <= state->row; r++) {
                int end_col = (r == state->row) ? state->col : TERMINAL_COLS;
                for (int c = 0; c < end_col; c++) {
                    memset(terminal_buffer[r][c].c, 0, sizeof(terminal_buffer[r][c].c));
                }
            }
            break;

        case 2: // Clear entire screen
            for (int r = 0; r < TERMINAL_ROWS; r++) {
                memset(terminal_buffer[r], 0, sizeof(terminal_buffer[r]));
            }
            break;
    }
}

void handle_erase_line(int mode, TerminalState *state) {
    int r = state->row;
    switch (mode) {
        case 0: // Clear from cursor to end of line
            for (int c = state->col; c < TERMINAL_COLS; c++) {
                memset(terminal_buffer[r][c].c, 0, sizeof(terminal_buffer[r][c].c));
            }
            break;

        case 1: // Clear from beginning of line to cursor
            for (int c = 0; c <= state->col; c++) {
                memset(terminal_buffer[r][c].c, 0, sizeof(terminal_buffer[r][c].c));
            }
            break;

        case 2: // Clear entire line
            memset(terminal_buffer[r], 0, sizeof(terminal_buffer[r]));
            break;
    }
}

// Initialize terminal state with default color and font
void initialize_terminal_state(TerminalState *state, XftColor default_color, XftFont *default_font) {
    state->row = 0;
    state->col = 0;
    state->current_color = default_color;
    state->current_font = default_font;

    // Initialize terminal_buffer
    for (int r = 0; r < TERMINAL_ROWS; r++) {
        memset(terminal_buffer[r], 0, sizeof(terminal_buffer[r]));
    }
}

// Reset terminal attributes to default
void reset_attributes(TerminalState *state, XftColor default_color, XftFont *default_font) {
    state->current_color = default_color;
    state->current_font = default_font;
    // Reset other attributes if added
}

void allocate_color(Display *display, TerminalState *state, int color_code) {
    XRenderColor xr;
    switch (color_code) {
        case 30: xr = (XRenderColor){0x0000, 0x0000, 0x0000, 0xFFFF}; break; // Black
        case 31: xr = (XRenderColor){0xFFFF, 0x0000, 0x0000, 0xFFFF}; break; // Red
        case 32: xr = (XRenderColor){0x0000, 0xFFFF, 0x0000, 0xFFFF}; break; // Green
        case 33: xr = (XRenderColor){0xFFFF, 0xFFFF, 0x0000, 0xFFFF}; break; // Yellow
        case 34: xr = (XRenderColor){0x0000, 0x0000, 0xFFFF, 0xFFFF}; break; // Blue
        case 35: xr = (XRenderColor){0xFFFF, 0x0000, 0xFFFF, 0xFFFF}; break; // Magenta
        case 36: xr = (XRenderColor){0x0000, 0xFFFF, 0xFFFF, 0xFFFF}; break; // Cyan
        case 37: xr = (XRenderColor){0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF}; break; // White
        default: return;
    }

    if (!XftColorAllocValue(display, DefaultVisual(display, DefaultScreen(display)),
                            DefaultColormap(display, DefaultScreen(display)), &xr, &state->current_color)) {
        fprintf(stderr, "Failed to allocate color for code %d.\n", color_code);
        // Optionally, set to a default color or handle the error as needed
    }
}

void allocate_background_color(Display *display, TerminalState *state, int color_code) {
    (void)display; // Suppress unused parameter warning
    (void)state;   // Suppress unused parameter warning
    (void)color_code; // Suppress unused parameter warning

    // TODO: Implement background color allocation
}

// Function to place a character in the terminal buffer
void put_char(char c, TerminalState *state) {
    static uint8_t utf8_buf[MAX_UTF8_CHAR_SIZE + 1] = {0}; // Buffer for UTF-8 decoding
    static int utf8_len = 0;

    // Handle special control characters
    if (c == '\b' || c == 0x7F) { // Handle backspace
        if (state->col > 0) {
            state->col--;
            memset(terminal_buffer[state->row][state->col].c, 0, MAX_UTF8_CHAR_SIZE + 1); // Clear character
        }
        return;
    }

    if (c == '\n') { 
        state->col = 0;  // Move cursor to start of next line
        state->row++;

        // Scroll if at the bottom
        if (state->row >= TERMINAL_ROWS) {
            // Scroll up
            for (int r = 1; r < TERMINAL_ROWS; r++) {
                memcpy(terminal_buffer[r - 1], terminal_buffer[r], sizeof(TerminalCell) * TERMINAL_COLS);
            }
            memset(terminal_buffer[TERMINAL_ROWS - 1], 0, sizeof(TerminalCell) * TERMINAL_COLS);
            state->row = TERMINAL_ROWS - 1;
        }
        return;
    }

    if (c == '\r') {
        state->col = 0;  // Carriage return resets to beginning of line
        return;
    }

    // Handle UTF-8 decoding
    utf8_buf[utf8_len++] = (uint8_t)c;
    utf8proc_int32_t codepoint;
    ssize_t result = utf8proc_iterate(utf8_buf, utf8_len, &codepoint);

    if (result > 0) { // If valid UTF-8 sequence is detected
        utf8_buf[utf8_len] = '\0';  // Null-terminate for storage

        if (state->col >= TERMINAL_COLS) {
            state->row++;
            state->col = 0;
            if (state->row >= TERMINAL_ROWS) {
                // Scroll up
                for (int r = 1; r < TERMINAL_ROWS; r++) {
                    memcpy(terminal_buffer[r - 1], terminal_buffer[r], sizeof(TerminalCell) * TERMINAL_COLS);
                }
                memset(terminal_buffer[TERMINAL_ROWS - 1], 0, sizeof(TerminalCell) * TERMINAL_COLS);
                state->row = TERMINAL_ROWS - 1;
            }
        }

        // Store the UTF-8 character in the terminal buffer
        memcpy(terminal_buffer[state->row][state->col].c, (char *)utf8_buf, MAX_UTF8_CHAR_SIZE);
        terminal_buffer[state->row][state->col].c[MAX_UTF8_CHAR_SIZE] = '\0';
        terminal_buffer[state->row][state->col].color = state->current_color;
        terminal_buffer[state->row][state->col].font = state->current_font;
        state->col++;

        utf8_len = 0;  // Reset buffer for next character
    }
}

void scroll_up(int lines) {
    GC gc = DefaultGC(global_display, DefaultScreen(global_display));
    scrollback_position = (scrollback_position + lines > SCROLLBACK_LINES) ? 
                          SCROLLBACK_LINES : scrollback_position + lines;
    draw_text(global_display, global_window, gc);
}