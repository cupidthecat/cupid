#include <stdlib.h>      // Standard library functions
#include <string.h>      // String manipulation functions
#include "../include/clipboard.h"  // Clipboard-related functions and definitions
#include "../include/editor.h"     // Editor configuration and operations
#include "../include/globals.h"    // Global variables and configurations

static char *clipboard = NULL;      // Pointer to clipboard buffer
static size_t clipboard_len = 0;    // Length of clipboard content

/* ============== Local Copy/Paste Implementation ============== */

/*
 * Copy selected text to local clipboard
 * Extracts text from current selection and stores it in clipboard buffer
 * Handles both forward and backward selections
 * Updates status message with result
 */
void copySelection() {
    if (!E.selActive) {
        editorSetStatusMessage("No selection to copy");
        return;
    }

    /* Calculate start/end row/col of selection */
    int srow = E.selStartY, scol = E.selStartX;
    int erow = E.cy, ecol = E.cx;

    /* Normalize if user selected backwards */
    if (srow > erow || (srow == erow && scol > ecol)) {
        int tmpR = srow;  srow = erow;  erow = tmpR;
        int tmpC = scol; scol = ecol;  ecol = tmpC;
    }

    /* Free old clipboard if it exists */
    free(clipboard);
    clipboard = NULL;
    clipboard_len = 0;

    /* Gather the text in selection */
    size_t buf_size = 1024;
    size_t buf_used = 0;
    char *buf = malloc(buf_size);
    if (!buf) return;

    for (int r = srow; r <= erow; r++) {
        char *line = E.row[r];
        int line_len = strlen(line);

        /* Calculate column boundaries for current line */
        int start_col = (r == srow) ? scol : 0;
        int end_col   = (r == erow) ? ecol : line_len;

        /* Validate column boundaries */
        if (start_col < 0) start_col = 0;
        if (end_col > line_len) end_col = line_len;
        if (start_col > end_col) start_col = end_col; 

        int segment_len = end_col - start_col;

        /* Grow buffer if needed */
        if (buf_used + segment_len + 2 >= buf_size) {
            buf_size = buf_size * 2 + segment_len + 2;
            buf = realloc(buf, buf_size);
            if (!buf) return;  // Out of memory
        }

        /* Copy text segment to buffer */
        if (segment_len > 0) {
            memcpy(buf + buf_used, line + start_col, segment_len);
            buf_used += segment_len;
        }

        /* Add newline if not the last line */
        if (r < erow) {
            buf[buf_used] = '\n';
            buf_used++;
        }
    }
    buf[buf_used] = '\0';

    /* Store in global clipboard */
    clipboard = buf;
    clipboard_len = buf_used;

    editorSetStatusMessage("Copied %zu bytes to clipboard", clipboard_len);
}

/*
 * Paste clipboard content at current cursor position
 * If selection is active, deletes it before pasting
 * Handles multi-line content with newline characters
 * Updates status message with result
 */
void editorPaste() {
    if (!clipboard || clipboard_len == 0) {
        editorSetStatusMessage("Clipboard is empty");
        return;
    }

    /* Delete active selection before pasting */
    if (E.selActive) {
        extern void editorDeleteSelection();
        editorDeleteSelection();
    }

    /* Insert clipboard content line by line */
    for (size_t i = 0; i < clipboard_len; i++) {
        if (clipboard[i] == '\n') {
            editorInsertNewline();
        } else {
            editorInsertChar(clipboard[i]);
        }
    }

    editorSetStatusMessage("Pasted %zu bytes from clipboard", clipboard_len);
}
