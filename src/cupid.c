#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <termios.h>
#include <ctype.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <string.h>
#include <time.h>
#include <stdarg.h>
#include <stdint.h>
#include <ctype.h>

#define CTRL_KEY(k) ((k) & 0x1f)

/* New key definitions for Shift+Arrow keys */
#define SHIFT_ARROW_UP    2000
#define SHIFT_ARROW_DOWN  2001
#define SHIFT_ARROW_RIGHT 2002
#define SHIFT_ARROW_LEFT  2003

/* ============== Terminal Configuration ============== */

static struct termios orig_termios;

void die(const char *s) {
    perror(s);
    exit(1);
}

void disableRawMode() {
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios) == -1)
        die("tcsetattr");
}

void enableRawMode() {
    if (tcgetattr(STDIN_FILENO, &orig_termios) == -1) die("tcgetattr");
    atexit(disableRawMode);

    struct termios raw = orig_termios;
    /* Input flags: BRKINT, ICRNL, INPCK, ISTRIP, IXON */
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    /* Output flags: OPOST */
    raw.c_oflag &= ~(OPOST);
    /* Control flags: CS8 (8-bit chars) */
    raw.c_cflag |= (CS8);
    /* Local flags: ECHO, ICANON, ISIG, IEXTEN */
    raw.c_lflag &= ~(ECHO | ICANON | ISIG | IEXTEN);
    /* Min bytes & timeout for read() */
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1;

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1)
        die("tcsetattr");
}

/* ============== Window Size ============== */

int getWindowSize(int *rows, int *cols) {
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1) {
        return -1;
    } else {
        *cols = ws.ws_col;
        *rows = ws.ws_row;
        return 0;
    }
}

/* ============== Editor Data ============== */

typedef struct EditorConfig {
    int cx, cy;            // Cursor x and y
    int screenrows;
    int screencols;
    int numrows;           // Number of rows of text
    char **row;            // Array of strings (each line of text)
    char *filename;        // Current open filename (for saving)

    /* Status message and timing */
    char statusmsg[256];
    time_t statusmsg_time;

    /* Selection state */
    int selActive;         // 1 if a selection is active, 0 otherwise
    int selStartX, selStartY; // Where the selection was initiated (the anchor)
} EditorConfig;

static EditorConfig E;

/* ============== Utility: Set Status Message ============== */
void editorSetStatusMessage(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(E.statusmsg, sizeof(E.statusmsg), fmt, ap);
    va_end(ap);
    E.statusmsg_time = time(NULL);
}

/* ============== Append Buffer ============== */

struct abuf {
    char *b;
    int len;
};

#define ABUF_INIT {NULL, 0}

void abAppend(struct abuf *ab, const char *s, int len) {
    char *newb = realloc(ab->b, ab->len + len);
    if (!newb) return;
    memcpy(newb + ab->len, s, len);
    ab->b = newb;
    ab->len += len;
}

void abFree(struct abuf *ab) {
    free(ab->b);
}

/* ============== Reading Keypresses ============== */

enum editorKey {
    ARROW_LEFT = 1000,
    ARROW_RIGHT,
    ARROW_UP,
    ARROW_DOWN,
    DEL_KEY
};

int readKey() {
    int nread;
    char c;
    while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
        if (nread == -1 && errno != EAGAIN) die("read");
    }

    if (c == '\x1b') {
        char seq[6] = {0};
        if (read(STDIN_FILENO, &seq[0], 1) != 1) return '\x1b';
        if (read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1b';

        if (seq[0] == '[') {
            // Check for Shift+Arrow sequences: expect "1;2X"
            if (seq[1] >= '0' && seq[1] <= '9') {
                char seq2;
                if (read(STDIN_FILENO, &seq2, 1) != 1) return '\x1b';
                if (seq2 == ';') {
                    char seq3;
                    if (read(STDIN_FILENO, &seq3, 1) != 1) return '\x1b';
                    if (seq3 == '2') { // Modifier 2 usually means SHIFT
                        char seq4;
                        if (read(STDIN_FILENO, &seq4, 1) != 1) return '\x1b';
                        switch(seq4) {
                            case 'A': return SHIFT_ARROW_UP;
                            case 'B': return SHIFT_ARROW_DOWN;
                            case 'C': return SHIFT_ARROW_RIGHT;
                            case 'D': return SHIFT_ARROW_LEFT;
                            default: return '\x1b';
                        }
                    }
                }
                // Fall back: if not a semicolon sequence, ignore extra characters.
            } else {
                switch(seq[1]) {
                    case 'A': return ARROW_UP;
                    case 'B': return ARROW_DOWN;
                    case 'C': return ARROW_RIGHT;
                    case 'D': return ARROW_LEFT;
                }
            }
        }
        return '\x1b';
    } else {
        return c;
    }
}

/* ============== Cursor Movement ============== */

/* Plain arrow moves clear any selection */
void moveCursor(int key) {
    E.selActive = 0;  // Clear selection when moving without shift
    switch(key) {
        case ARROW_LEFT:
            if (E.cx > 0) {
                E.cx--;
            } else if (E.cy > 0) {
                E.cy--;
                E.cx = strlen(E.row[E.cy]);
            }
            break;
        case ARROW_RIGHT: {
            int rowLen = (E.cy < E.numrows) ? strlen(E.row[E.cy]) : 0;
            if (E.cx < rowLen) {
                E.cx++;
            } else if (E.cy < E.numrows - 1) {
                E.cy++;
                E.cx = 0;
            }
            break;
        }
        case ARROW_UP:
            if (E.cy > 0) {
                E.cy--;
                int rowLen = strlen(E.row[E.cy]);
                if (E.cx > rowLen) {
                    E.cx = rowLen;
                }
            }
            break;
        case ARROW_DOWN:
            if (E.cy < E.numrows - 1) {
                E.cy++;
                int rowLen = strlen(E.row[E.cy]);
                if (E.cx > rowLen) {
                    E.cx = rowLen;
                }
            }
            break;
    }
}

/* Shift–modified arrow keys use this to update selection while moving */
void editorMoveCursorWithSelection(int key) {
    /* If no selection is active, start one at the current cursor */
    if (!E.selActive) {
        E.selActive = 1;
        E.selStartX = E.cx;
        E.selStartY = E.cy;
    }
    /* Then move the cursor (without clearing selection) */
    switch(key) {
        case ARROW_LEFT:
            if (E.cx > 0) {
                E.cx--;
            } else if (E.cy > 0) {
                E.cy--;
                E.cx = strlen(E.row[E.cy]);
            }
            break;
        case ARROW_RIGHT: {
            int rowLen = (E.cy < E.numrows) ? strlen(E.row[E.cy]) : 0;
            if (E.cx < rowLen) {
                E.cx++;
            } else if (E.cy < E.numrows - 1) {
                E.cy++;
                E.cx = 0;
            }
            break;
        }
        case ARROW_UP:
            if (E.cy > 0) {
                E.cy--;
                int rowLen = strlen(E.row[E.cy]);
                if (E.cx > rowLen) {
                    E.cx = rowLen;
                }
            }
            break;
        case ARROW_DOWN:
            if (E.cy < E.numrows - 1) {
                E.cy++;
                int rowLen = strlen(E.row[E.cy]);
                if (E.cx > rowLen) {
                    E.cx = rowLen;
                }
            }
            break;
    }
}

/* ============== Insertion and Deletion ============== */

static void editorRowAppendString(int rowIndex, const char *s) {
    char *oldRow = E.row[rowIndex];
    int oldLen = strlen(oldRow);
    int sLen = strlen(s);

    oldRow = realloc(oldRow, oldLen + sLen + 1);
    memcpy(&oldRow[oldLen], s, sLen);
    oldRow[oldLen + sLen] = '\0';

    E.row[rowIndex] = oldRow;
}

/* Insert a character at the current cursor position */
void editorInsertChar(char c) {
    if (E.cy >= E.numrows) return; // Out of range

    char *row = E.row[E.cy];
    int rowLen = strlen(row);
    if (E.cx > rowLen) E.cx = rowLen;

    row = realloc(row, rowLen + 2); // +1 for new char, +1 for '\0'
    if (!row) return;

    memmove(&row[E.cx + 1], &row[E.cx], rowLen - E.cx + 1);
    row[E.cx] = c;
    E.row[E.cy] = row;
    E.cx++;

    editorSetStatusMessage("Inserted char '%c' at line %d", c, E.cy + 1);
}

/* Merge one row into the previous row */
void editorRowMerge(int to, int from) {
    int toLen = strlen(E.row[to]);
    int fromLen = strlen(E.row[from]);

    E.row[to] = realloc(E.row[to], toLen + fromLen + 1);
    memcpy(&E.row[to][toLen], E.row[from], fromLen);
    E.row[to][toLen + fromLen] = '\0';

    free(E.row[from]);
    for (int i = from; i < E.numrows - 1; i++) {
        E.row[i] = E.row[i + 1];
    }
    E.numrows--;
}

/* Delete the character to the left of the cursor */
void editorDelChar() {
    if (E.cy >= E.numrows) return;
    if (E.cx == 0 && E.cy == 0) {
        editorSetStatusMessage("Nothing to delete");
        return;
    }

    char *row = E.row[E.cy];
    int rowLen = strlen(row);
    if (E.cx == 0) {
        int prevLen = strlen(E.row[E.cy - 1]);
        E.cx = prevLen;
        editorRowMerge(E.cy - 1, E.cy);
        E.cy--;
        editorSetStatusMessage("Merged line %d into previous line", E.cy + 2);
    } else {
        memmove(&row[E.cx - 1], &row[E.cx], rowLen - (E.cx - 1));
        E.cx--;
        row = realloc(row, rowLen);
        if (row) {
            E.row[E.cy] = row;
        }
        editorSetStatusMessage("Deleted char at line %d", E.cy + 1);
    }
}

/* Delete the character under the cursor */
void editorDelCharForward() {
    if (E.cy >= E.numrows) return;

    char *row = E.row[E.cy];
    int rowLen = strlen(row);
    if (E.cx == rowLen) {
        if (E.cy < E.numrows - 1) {
            editorRowMerge(E.cy, E.cy + 1);
            editorSetStatusMessage("Merged line %d into line %d", E.cy + 2, E.cy + 1);
        } else {
            editorSetStatusMessage("Nothing to delete");
        }
    } else {
        memmove(&row[E.cx], &row[E.cx + 1], rowLen - E.cx);
        row = realloc(row, rowLen);
        if (row) {
            E.row[E.cy] = row;
        }
        editorSetStatusMessage("Deleted char at line %d", E.cy + 1);
    }
}

/* Insert a new line at the cursor position */
void editorInsertNewline() {
    if (E.cy > E.numrows) return;

    char *row = E.row[E.cy];
    int rowLen = strlen(row);
    int oldNumRows = E.numrows;

    if (E.cx == 0) {
        E.numrows++;
        E.row = realloc(E.row, sizeof(char*) * E.numrows);
        memmove(&E.row[E.cy + 1], &E.row[E.cy], sizeof(char*) * (oldNumRows - E.cy));
        E.row[E.cy] = strdup("");
        E.cy++;
        E.cx = 0;
        editorSetStatusMessage("Inserted blank line above (line %d)", E.cy + 1);
    } else if (E.cx == rowLen) {
        E.numrows++;
        E.row = realloc(E.row, sizeof(char*) * E.numrows);
        memmove(&E.row[E.cy + 2], &E.row[E.cy + 1], sizeof(char*) * (oldNumRows - E.cy - 1));
        E.row[E.cy + 1] = strdup("");
        E.cy++;
        E.cx = 0;
        editorSetStatusMessage("Inserted blank line after (line %d)", E.cy);
    } else {
        char *newLine = strdup(&row[E.cx]);
        row[E.cx] = '\0';
        E.row[E.cy] = realloc(row, E.cx + 1);
        E.numrows++;
        E.row = realloc(E.row, sizeof(char*) * E.numrows);
        memmove(&E.row[E.cy + 2], &E.row[E.cy + 1], sizeof(char*) * (oldNumRows - E.cy - 1));
        E.row[E.cy + 1] = newLine;
        E.cy++;
        E.cx = 0;
        editorSetStatusMessage("Split line %d at position %d", E.cy, 0);
    }
}

/* ============== File I/O (Save & Open) ============== */

void editorSave() {
    if (!E.filename) {
        editorSetStatusMessage("No filename to save to!");
        return;
    }

    FILE *fp = fopen(E.filename, "w");
    if (!fp) {
        editorSetStatusMessage("Could not open %s for writing!", E.filename);
        return;
    }
    for (int i = 0; i < E.numrows; i++) {
        fprintf(fp, "%s\n", E.row[i]);
    }
    fclose(fp);
    editorSetStatusMessage("Saved %d lines to '%s'", E.numrows, E.filename);
}

void editorOpen(const char *filename) {
    E.filename = strdup(filename);
    FILE *fp = fopen(filename, "r");
    if (!fp) {
        FILE *empty = fopen(filename, "w");
        if (!empty) die("fopen");
        fclose(empty);
        fp = fopen(filename, "r");
        if (!fp) die("fopen");
    }
    char *line = NULL;
    size_t linecap = 0;
    ssize_t linelen;
    E.numrows = 0;
    E.row = NULL;
    while ((linelen = getline(&line, &linecap, fp)) != -1) {
        while (linelen > 0 && (line[linelen - 1] == '\n' ||
                               line[linelen - 1] == '\r'))
            linelen--;
        line[linelen] = '\0';
        E.numrows++;
        E.row = realloc(E.row, sizeof(char*) * E.numrows);
        E.row[E.numrows - 1] = strdup(line);
    }
    free(line);
    fclose(fp);
    if (E.numrows == 0) {
        E.numrows = 1;
        E.row = malloc(sizeof(char*));
        E.row[0] = strdup("");
    }
    editorSetStatusMessage("Opened file: %s (%d lines)", filename, E.numrows);
}

/* ============== Base64 Encoding (for OSC 52) ============== */

char *base64_encode(const unsigned char *data, size_t input_length, size_t *output_length) {
    const char base64_chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    *output_length = 4 * ((input_length + 2) / 3);
    char *encoded_data = malloc(*output_length + 1);
    if (!encoded_data) return NULL;
    size_t i, j;
    for (i = 0, j = 0; i < input_length;) {
        uint32_t octet_a = i < input_length ? data[i++] : 0;
        uint32_t octet_b = i < input_length ? data[i++] : 0;
        uint32_t octet_c = i < input_length ? data[i++] : 0;

        uint32_t triple = (octet_a << 16) | (octet_b << 8) | octet_c;

        encoded_data[j++] = base64_chars[(triple >> 18) & 0x3F];
        encoded_data[j++] = base64_chars[(triple >> 12) & 0x3F];
        encoded_data[j++] = (i - 2 < input_length) ? base64_chars[(triple >> 6) & 0x3F] : '=';
        encoded_data[j++] = (i - 1 < input_length) ? base64_chars[triple & 0x3F] : '=';
    }
    encoded_data[j] = '\0';
    return encoded_data;
}

/* ============== Copy Selection to Clipboard ============== */

/*
 * This function extracts the text within the current selection (if any),
 * encodes it in base64, and sends it via an OSC 52 escape sequence.
 */
void copySelection() {
    if (!E.selActive) {
        editorSetStatusMessage("No selection to copy");
        return;
    }
    int srow = E.selStartY, scol = E.selStartX;
    int erow = E.cy, ecol = E.cx;
    /* For same-line selection, ensure the lower column is first */
    if (srow == erow && scol > ecol) {
        int temp = scol;
        scol = ecol;
        ecol = temp;
    } else if (srow > erow) { /* If selection was made upward, swap rows */
        int temp = srow;
        srow = erow;
        erow = temp;
    }
    size_t copy_buffer_size = 1024;
    size_t copy_length = 0;
    char *copy_buffer = malloc(copy_buffer_size);
    if (!copy_buffer) return;
    for (int i = srow; i <= erow; i++) {
        char *line = E.row[i];
        int line_len = strlen(line);
        int start = (i == srow) ? scol : 0;
        int end = (i == erow) ? ecol : line_len;
        if (start > line_len) start = line_len;
        if (end > line_len) end = line_len;
        int segment_length = end - start;
        if (segment_length < 0) segment_length = 0;
        if (copy_length + segment_length + 2 > copy_buffer_size) {
            copy_buffer_size *= 2;
            copy_buffer = realloc(copy_buffer, copy_buffer_size);
        }
        memcpy(copy_buffer + copy_length, line + start, segment_length);
        copy_length += segment_length;
        if (i < erow) {
            copy_buffer[copy_length] = '\n';
            copy_length++;
        }
    }
    if (copy_length + 1 > copy_buffer_size)
        copy_buffer = realloc(copy_buffer, copy_length + 1);
    copy_buffer[copy_length] = '\0';

    size_t b64_length = 0;
    char *b64_data = base64_encode((const unsigned char *)copy_buffer, copy_length, &b64_length);
    free(copy_buffer);
    if (!b64_data) {
        editorSetStatusMessage("Failed to encode selection");
        return;
    }
    char osc_sequence[512];
    int osc_len = snprintf(osc_sequence, sizeof(osc_sequence), "\x1b]52;c;%s\x07", b64_data);
    free(b64_data);
    write(STDOUT_FILENO, osc_sequence, osc_len);
    editorSetStatusMessage("Copied selection (%d bytes)", copy_length);
}

/* ============== Improved Selection & Highlighting ============== */

/*
 * Helper: Calculate the selection bounds.
 * For a single-line selection, sCol is the minimum of the anchor and current x,
 * and eCol is the maximum.
 * For multi-line selection, the first line is selected from the anchor column,
 * the last line up to the current column, and any rows in between are fully selected.
 */
void getSelectionBounds(int *sRow, int *eRow, int *sCol, int *eCol) {
    if (!E.selActive) {
        *sRow = *eRow = *sCol = *eCol = -1;
        return;
    }
    if (E.selStartY < E.cy || (E.selStartY == E.cy && E.selStartX <= E.cx)) {
        *sRow = E.selStartY;
        *eRow = E.cy;
        if (E.selStartY == E.cy) {
            *sCol = (E.selStartX < E.cx) ? E.selStartX : E.cx;
            *eCol = (E.selStartX > E.cx) ? E.selStartX : E.cx;
        } else {
            *sCol = E.selStartX;
            *eCol = E.cx;
        }
    } else {
        *sRow = E.cy;
        *eRow = E.selStartY;
        if (E.cy == E.selStartY) {
            *sCol = (E.cx < E.selStartX) ? E.cx : E.selStartX;
            *eCol = (E.cx > E.selStartX) ? E.cx : E.selStartX;
        } else {
            *sCol = E.cx;
            *eCol = E.selStartX;
        }
    }
}

void editorDeleteSelection() {
    if (!E.selActive)
        return;

    int sRow, eRow, sCol, eCol;
    getSelectionBounds(&sRow, &eRow, &sCol, &eCol);

    // Safety check
    if (sRow < 0 || eRow < 0)
        return;

    if (sRow == eRow) {
        // Single-line selection: remove characters from sCol to eCol.
        char *line = E.row[sRow];
        int linelen = strlen(line);
        if (sCol < linelen) {
            memmove(&line[sCol], &line[eCol], linelen - eCol + 1);
        }
        E.cx = sCol;
        E.cy = sRow;
    } else {
        // Multi-line selection:
        // 1. Truncate the first line at sCol.
        char *firstLine = E.row[sRow];
        firstLine[sCol] = '\0';

        // 2. Get the remainder of the last line starting at eCol.
        char *lastLine = E.row[eRow];
        char *lastPart = strdup(lastLine + eCol);

        // 3. Merge first line and lastPart.
        int newLen = sCol + strlen(lastPart);
        firstLine = realloc(firstLine, newLen + 1);
        if (!firstLine) return;  // handle allocation error if needed
        strcat(firstLine, lastPart);
        free(lastPart);
        E.row[sRow] = firstLine;

        // 4. Free all lines between sRow+1 and eRow (inclusive).
        for (int i = sRow + 1; i <= eRow; i++) {
            free(E.row[i]);
        }
        // 5. Shift remaining rows upward.
        int numDeleted = eRow - sRow;
        for (int i = sRow + 1; i < E.numrows - numDeleted; i++) {
            E.row[i] = E.row[i + numDeleted];
        }
        E.numrows -= numDeleted;
        E.cx = sCol;
        E.cy = sRow;
    }
    E.selActive = 0;
    editorSetStatusMessage("Deleted selection");
}

/* ============== Output & Refresh ============== */

/*
 * Draw text rows.
 * If a selection is active, only the region between the calculated bounds is highlighted.
 */
void editorDrawRows(struct abuf *ab) {
    int textRows = E.screenrows - 2;
    int sRow = 0, eRow = 0, sCol = 0, eCol = 0;

    if (E.selActive) {
        getSelectionBounds(&sRow, &eRow, &sCol, &eCol);
    }

    for (int y = 0; y < textRows; y++) {
        if (y < E.numrows) {
            // Adjust the line number width properly
            char lineNumber[16];
            snprintf(lineNumber, sizeof(lineNumber), "%4d | ", y + 1);
            abAppend(ab, lineNumber, strlen(lineNumber));

            char *line = E.row[y];
            int linelen = strlen(line);
            if (linelen > E.screencols - 6)
                linelen = E.screencols - 6;

            int inSelection = 0;
            for (int x = 0; x < linelen; x++) {
                int selected = 0;

                // Correct selection highlighting logic
                if (E.selActive && y >= sRow && y <= eRow) {
                    if (sRow == eRow) {  // Single-line selection
                        if (x >= sCol && x < eCol)
                            selected = 1;
                    } else if (y == sRow) {  // First line of selection
                        if (x >= sCol)
                            selected = 1;
                    } else if (y == eRow) {  // Last line of selection
                        if (x < eCol)
                            selected = 1;
                    } else {  // Fully selected rows in between
                        selected = 1;
                    }
                }

                // Apply proper highlighting
                if (selected && !inSelection) {
                    abAppend(ab, "\x1b[7m", 4);  // Inverse colors (highlight)
                    inSelection = 1;
                } else if (!selected && inSelection) {
                    abAppend(ab, "\x1b[0m", 4);  // Reset to normal colors
                    inSelection = 0;
                }
                abAppend(ab, &line[x], 1);
            }

            // Reset selection at the end of the line
            if (inSelection)
                abAppend(ab, "\x1b[0m", 4);
        }

        abAppend(ab, "\x1b[K", 3);
        abAppend(ab, "\r\n", 2);
    }
}
/* Draw the status bar */
void editorDrawStatusBar(struct abuf *ab) {
    abAppend(ab, "\x1b[7m", 4);
    char statusLeft[256], statusRight[256];
    int lenLeft = snprintf(statusLeft, sizeof(statusLeft), "%.20s - %d lines",
                             (E.filename ? E.filename : "[No Name]"), E.numrows);
    int lenRight = snprintf(statusRight, sizeof(statusRight), "%d/%d", E.cy + 1, E.numrows);
    if (lenLeft > E.screencols) lenLeft = E.screencols;
    abAppend(ab, statusLeft, lenLeft);
    while (lenLeft < E.screencols) {
        if (E.screencols - lenLeft == lenRight) {
            abAppend(ab, statusRight, lenRight);
            break;
        } else {
            abAppend(ab, " ", 1);
            lenLeft++;
        }
    }
    abAppend(ab, "\x1b[m", 3);
    abAppend(ab, "\r\n", 2);
}

/* Draw the message bar */
void editorDrawMessageBar(struct abuf *ab) {
    abAppend(ab, "\x1b[K", 3);
    int msglen = strlen(E.statusmsg);
    if (msglen > E.screencols)
        msglen = E.screencols;
    abAppend(ab, E.statusmsg, msglen);
    abAppend(ab, "\r\n", 2);
}

/* Refresh the screen */
void editorRefreshScreen() {
    struct abuf ab = ABUF_INIT;
    abAppend(&ab, "\x1b[?25l", 6);  // Hide cursor
    abAppend(&ab, "\x1b[H", 3);  // Move cursor to top-left

    editorDrawRows(&ab);
    editorDrawStatusBar(&ab);
    editorDrawMessageBar(&ab);

    int cursorOffset = 6;  // Ensuring alignment after line numbers
    char buf[32];
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", E.cy + 1, E.cx + 1 + cursorOffset);
    abAppend(&ab, buf, strlen(buf));

    abAppend(&ab, "\x1b[?25h", 6);  // Show cursor
    write(STDOUT_FILENO, ab.b, ab.len);
    abFree(&ab);
}


/* ============== Process Keypresses ============== */

void processKeypress() {
    int c = readKey();
    switch (c) {
        case CTRL_KEY('q'):
            write(STDOUT_FILENO, "\x1b[2J\x1b[H", 7);
            exit(0);
            break;
        case CTRL_KEY('s'):
            editorSave();
            break;
        case CTRL_KEY('c'):
            copySelection();
            break;
        case '\r':
            E.selActive = 0; // Cancel selection on newline
            editorInsertNewline();
            break;
        // Backspace / Ctrl+H handling:
        case 127:
        case CTRL_KEY('h'):
            if (E.selActive) {
                editorDeleteSelection();
            } else {
                editorDelChar();
            }
            break;
        // Forward delete key:
        case DEL_KEY:
            if (E.selActive) {
                editorDeleteSelection();
            } else {
                editorDelCharForward();
            }
            break;
        case '\t':
            E.selActive = 0;
            for (int i = 0; i < 4; i++) {
                editorInsertChar(' ');
            }
            break;
        // Movement keys cancel selection:
        case ARROW_LEFT:
        case ARROW_RIGHT:
        case ARROW_UP:
        case ARROW_DOWN:
            E.selActive = 0;
            moveCursor(c);
            editorSetStatusMessage("Moved cursor to line %d", E.cy + 1);
            break;
        case SHIFT_ARROW_LEFT:
            editorMoveCursorWithSelection(ARROW_LEFT);
            editorSetStatusMessage("Selected from (%d,%d) to (%d,%d)",
                                    E.selStartY + 1, E.selStartX + 1, E.cy + 1, E.cx + 1);
            break;
        case SHIFT_ARROW_RIGHT:
            editorMoveCursorWithSelection(ARROW_RIGHT);
            editorSetStatusMessage("Selected from (%d,%d) to (%d,%d)",
                                    E.selStartY + 1, E.selStartX + 1, E.cy + 1, E.cx + 1);
            break;
        case SHIFT_ARROW_DOWN:
            editorMoveCursorWithSelection(ARROW_DOWN);
            E.cx = strlen(E.row[E.cy]); // Move to end of line
            editorSetStatusMessage("Selected from (%d,%d) to (%d,%d)",
                                     E.selStartY + 1, E.selStartX + 1, E.cy + 1, E.cx + 1);
            break;
        case SHIFT_ARROW_UP:
            editorMoveCursorWithSelection(ARROW_UP);
            E.cx = 0; // Move to beginning of line
            editorSetStatusMessage("Selected from (%d,%d) to (%d,%d)",
                                     E.selStartY + 1, E.selStartX + 1, E.cy + 1, E.cx + 1);
            break;
        default:
            if (isprint(c)) {
                E.selActive = 0;  // Cancel selection on text input
                editorInsertChar(c);
            }
            break;
    }
}

/* ============== Init & Main ============== */

/* Reserve two lines for the status and message bars */
void initEditor() {
    E.cx = 0;
    E.cy = 0;
    E.numrows = 0;
    E.row = NULL;
    E.filename = NULL;
    E.statusmsg[0] = '\0';
    E.statusmsg_time = 0;
    E.selActive = 0;
    E.selStartX = 0;
    E.selStartY = 0;

    if (getWindowSize(&E.screenrows, &E.screencols) == -1)
        die("getWindowSize"); // hehe die
    E.screenrows -= 2;
}

int main(int argc, char *argv[]) {
    enableRawMode();
    initEditor();

    if (argc >= 2) {
        editorOpen(argv[1]);
    } else {
        E.numrows = 1;
        E.row = malloc(sizeof(char*));
        E.row[0] = strdup("Type here... Use Ctrl+S to save, Ctrl+Q to quit, Ctrl+C to copy selection.");
        editorSetStatusMessage("New buffer (no filename)");
    }

    editorSetStatusMessage("HELP: Ctrl-S = save | Ctrl-Q = quit | Arrow keys = move | Shift+Arrow = select | Ctrl+C = copy selection");

    while (1) {
        editorRefreshScreen();
        processKeypress();
    }

    return 0;
}
