#define _POSIX_C_SOURCE 200809L  // Ensure POSIX.1-2008 features are available
#include <stdio.h>              // Standard I/O functions
#include <stdlib.h>             // Standard library functions
#include <unistd.h>             // POSIX API for system calls
#include <termios.h>            // Terminal I/O control
#include <errno.h>              // Error number definitions
#include <sys/ioctl.h>          // Terminal I/O control
#include <string.h>             // String manipulation functions

#include "../include/terminal.h" // Terminal-related functions and definitions
#include "../include/globals.h" // Global variables and configurations

static struct termios orig_termios; // Store original terminal attributes

/* ============== Terminal Control Functions ============== */

/*
 * Print error message and exit program
 * @param s: Error message to display
 */
void die(const char *s) {
    perror(s);
    exit(1);
}

/*
 * Restore original terminal attributes
 * Called automatically when program exits
 */
void disableRawMode() {
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios) == -1)
        die("tcsetattr");
}

/*
 * Enable raw mode for terminal input
 * Configures terminal for direct character input without processing
 */
void enableRawMode() {
    // Get current terminal attributes
    if (tcgetattr(STDIN_FILENO, &orig_termios) == -1) die("tcgetattr");
    // Ensure terminal attributes are restored on exit
    atexit(disableRawMode);

    // Configure raw mode settings
    struct termios raw = orig_termios;
    // Input flags: disable break, CR-to-NL, parity check, strip 8th bit, and XON/XOFF flow control
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    // Output flags: disable output processing
    raw.c_oflag &= ~(OPOST);
    // Control flags: set 8-bit characters
    raw.c_cflag |= (CS8);
    // Local flags: disable echo, canonical mode, signals, and extended input processing
    raw.c_lflag &= ~(ECHO | ICANON | ISIG | IEXTEN);
    // Control characters: set minimum bytes and timeout for read()
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1;

    // Apply new terminal attributes
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1)
        die("tcsetattr");
}

/*
 * Get terminal window size
 * @param rows: Pointer to store number of rows
 * @param cols: Pointer to store number of columns
 * @return: 0 on success, -1 on failure
 */
int getWindowSize(int *rows, int *cols) {
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1) {
        return -1;  // Return error if unable to get window size
    } else {
        *cols = ws.ws_col;  // Set columns
        *rows = ws.ws_row;  // Set rows
        return 0;           // Return success
    }
}

/* ============== Buffer Management ============== */

/*
 * Append string to buffer
 * @param ab: Pointer to buffer structure
 * @param s: String to append
 * @param len: Length of string to append
 */
void abAppend(struct abuf *ab, const char *s, int len) {
    // Reallocate buffer to accommodate new data
    char *new = realloc(ab->b, ab->len + len);
    if (!new) return;  // Return if realloc fails
    // Copy new data to end of buffer
    memcpy(&new[ab->len], s, len);
    ab->b = new;       // Update buffer pointer
    ab->len += len;    // Update buffer length
}

/*
 * Free buffer memory
 * @param ab: Pointer to buffer structure
 */
void abFree(struct abuf *ab) {
    free(ab->b);
}
