#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <termios.h>
#include <ctype.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <string.h>
#include <time.h>
#include <stdarg.h>
#include <stdint.h>
#include <ctype.h>

#include "../lib/cupidconf.h"

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

void editorRefreshScreen();

typedef struct EditorConfig {
    int cx, cy;            // Cursor x and y
    int rx;                // Render x position (accounting for tabs)
    int screenrows;
    int screencols;
    int numrows;           // Number of rows of text
    int rowoff;            // Row offset for scrolling
    int coloff;            // Column offset for scrolling
    int scroll_margin;     // Scrolling margin (soft border)
    char **row;            // Array of strings (each line of text)
    char *filename;        // Current open filename (for saving)

    /* Status message and timing */
    char statusmsg[256];
    time_t statusmsg_time;

    /* Selection state */
    int selActive;         // 1 if a selection is active, 0 otherwise
    int selStartX, selStartY; // Where the selection was initiated
} EditorConfig;

static EditorConfig E;

/* A simple global clipboard buffer for local copy/paste. */
static char *clipboard = NULL;      
static size_t clipboard_len = 0;    

/* ============== Key Binding Support ============== */
/*
 * We define a simple structure for configurable keybindings.
 * The default bindings are for:
 *   quit   : CTRL+Q
 *   save   : CTRL+S
 *   copy   : CTRL+C
 *   paste  : CTRL+V
 *   center : CTRL+L
 */
typedef struct KeyBindings {
    int quit;
    int save;
    int copy;
    int paste;
    int center;
} KeyBindings;

KeyBindings kb;  // Global keybindings

/* parse_key() converts a string (from the config) into an int key code.
 * It understands:
 *   - A leading '^' (e.g. "^q" becomes CTRL_KEY('q'))
 *   - Literal names for arrow keys (e.g. "ARROW_UP")
 *   - Single character strings.
 */
int parse_key(const char *str) {
    if (!str || !*str)
        return 0;
    if (str[0] == '^' && str[1] != '\0') {
        return CTRL_KEY(str[1]);
    } else if (strcmp(str, "ARROW_LEFT") == 0) {
        return 1000;  // ARROW_LEFT defined later in processKeypress()'s switch
    } else if (strcmp(str, "ARROW_RIGHT") == 0) {
        return 1001;  // ARROW_RIGHT
    } else if (strcmp(str, "ARROW_UP") == 0) {
        return 1002;  // ARROW_UP
    } else if (strcmp(str, "ARROW_DOWN") == 0) {
        return 1003;  // ARROW_DOWN
    } else if (strcmp(str, "SHIFT_ARROW_LEFT") == 0) {
        return SHIFT_ARROW_LEFT;
    } else if (strcmp(str, "SHIFT_ARROW_RIGHT") == 0) {
        return SHIFT_ARROW_RIGHT;
    } else if (strcmp(str, "SHIFT_ARROW_UP") == 0) {
        return SHIFT_ARROW_UP;
    } else if (strcmp(str, "SHIFT_ARROW_DOWN") == 0) {
        return SHIFT_ARROW_DOWN;
    } else if (strlen(str) == 1) {
        return str[0];
    } else {
        return atoi(str);
    }
}

/* loadKeyBindings() looks for a config file at ~/.config/cupid/keybinds.conf.
 * If not found, it creates a default config file with the standard keybindings.
 * Then it loads the configuration and updates the global keybindings.
 */
void loadKeyBindings(void) {
    char *home = getenv("HOME");
    if (!home) {
        home = ".";
    }
    char configDir[1024];  // Increased buffer size
    char configPath[1024]; // Increased buffer size
    
    // Check if paths would fit in buffers
    if (snprintf(configDir, sizeof(configDir), "%s/.config/cupid", home) >= (int)sizeof(configDir) ||
        snprintf(configPath, sizeof(configPath), "%s/keybinds.conf", configDir) >= (int)sizeof(configPath)) {
        fprintf(stderr, "Path too long for config directory\n");
        return;
    }

    // Create the directory if it does not exist.
    struct stat st = {0};
    if (stat(configDir, &st) == -1) {
        if (mkdir(configDir, 0755) == -1) {
            perror("mkdir");
            return;
        }
    }

    // Check if the config file exists.
    if (access(configPath, F_OK) == -1) {
        // Create the default config file.
        FILE *fp = fopen(configPath, "w");
        if (fp) {
            fprintf(fp, "# Default keybinds for Cupid Editor\n");
            fprintf(fp, "quit = ^q\n");
            fprintf(fp, "save = ^s\n");
            fprintf(fp, "copy = ^c\n");
            fprintf(fp, "paste = ^v\n");
            fprintf(fp, "center = ^l\n");
            fclose(fp);
            fprintf(stderr, "Default config created at %s\n", configPath);
        } else {
            perror("fopen for default config");
        }
    }

    cupidconf_t *conf = cupidconf_load(configPath);
    if (conf) {
        const char *bind;
        bind = cupidconf_get(conf, "quit");
        if (bind) kb.quit = parse_key(bind);
        bind = cupidconf_get(conf, "save");
        if (bind) kb.save = parse_key(bind);
        bind = cupidconf_get(conf, "copy");
        if (bind) kb.copy = parse_key(bind);
        bind = cupidconf_get(conf, "paste");
        if (bind) kb.paste = parse_key(bind);
        bind = cupidconf_get(conf, "center");
        if (bind) kb.center = parse_key(bind);
        cupidconf_free(conf);
    }
}

void editorScroll() {
    E.rx = E.cx;  // In a simple editor without tabs, rx equals cx
    
    // Vertical scrolling with soft margins
    if (E.cy < E.rowoff + E.scroll_margin) {
        E.rowoff = E.cy - E.scroll_margin;
        if (E.rowoff < 0) E.rowoff = 0;
    }
    if (E.cy >= E.rowoff + E.screenrows - E.scroll_margin) {
        E.rowoff = E.cy - E.screenrows + E.scroll_margin + 1;
        if (E.rowoff < 0) E.rowoff = 0;
    }
    
    // Horizontal scrolling with soft margins
    int screen_width = E.screencols - 6;  // Accounting for line number margin
    
    if (E.rx < E.coloff + E.scroll_margin) {
        E.coloff = E.rx - E.scroll_margin;
        if (E.coloff < 0) E.coloff = 0;
    }
    if (E.rx >= E.coloff + screen_width - E.scroll_margin) {
        E.coloff = E.rx - screen_width + E.scroll_margin + 1;
        if (E.coloff < 0) E.coloff = 0;
    }
}

/* ============== Utility: Set Status Message ============== */
void editorSetStatusMessage(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(E.statusmsg, sizeof(E.statusmsg), fmt, ap);
    va_end(ap);
    E.statusmsg_time = time(NULL);
}

void editorCenterCursor() {
    if (E.cy < E.numrows) {
        E.rowoff = E.cy - (E.screenrows / 2);
        if (E.rowoff < 0) E.rowoff = 0;
        else if (E.rowoff > E.numrows - E.screenrows)
            E.rowoff = E.numrows - E.screenrows;

        int line_length = strlen(E.row[E.cy]);
        int screen_width = E.screencols - 6;  
        if (line_length > screen_width) {
            E.coloff = E.cx - (screen_width / 2);
            if (E.coloff < 0) E.coloff = 0;
        } else {
            E.coloff = 0;
        }
    }
    editorSetStatusMessage("Centered cursor in viewport");
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
                    if (seq3 == '2') { // SHIFT
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
    if (!E.selActive) {
        E.selActive = 1;
        E.selStartX = E.cx;
        E.selStartY = E.cy;
    }
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

void editorInsertChar(char c) {
    if (E.cy >= E.numrows) return;

    char *row = E.row[E.cy];
    int rowLen = strlen(row);
    if (E.cx > rowLen) E.cx = rowLen;

    row = realloc(row, rowLen + 2);
    if (!row) return;

    memmove(&row[E.cx + 1], &row[E.cx], rowLen - E.cx + 1);
    row[E.cx] = c;
    E.row[E.cy] = row;
    E.cx++;

    editorSetStatusMessage("Inserted char '%c' at line %d", c, E.cy + 1);
}

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

    if (E.cy >= E.numrows) {
        E.cy = E.numrows - 1;
    }
    if (E.cy < E.rowoff) {
        E.rowoff = E.cy;
    }
    if (E.cy >= E.rowoff + E.screenrows) {
        E.rowoff = E.cy - E.screenrows + 1;
    }
}

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
        editorSetStatusMessage("Inserted blank line above (line %d)", E.cy);
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

    /* Insert an empty line at the beginning of the file, if desired: */
    E.numrows++;
    E.row = realloc(E.row, sizeof(char*) * E.numrows);
    memmove(&E.row[1], &E.row[0], sizeof(char*) * (E.numrows - 1));
    E.row[0] = strdup("");

    E.rowoff = 0;
    E.coloff = 0;
    E.cy = 0;
    E.cx = 0;

    editorSetStatusMessage("Opened file: %s (%d lines including empty start line)", filename, E.numrows);
    editorRefreshScreen();
}

/* ============== Local Copy/Paste Implementation ============== */

/*
 * Extract the currently selected text, store it into our local `clipboard`.
 * (We remove the OSC 52 code to keep it purely local.)
 */
void copySelection() {
    if (!E.selActive) {
        editorSetStatusMessage("No selection to copy");
        return;
    }
    /* Calculate start/end row/col of selection. */
    int srow = E.selStartY, scol = E.selStartX;
    int erow = E.cy, ecol = E.cx;

    // Normalize if user selected "backwards":
    if (srow > erow || (srow == erow && scol > ecol)) {
        int tmpR = srow;  srow = erow;  erow = tmpR;
        int tmpC = scol; scol = ecol;  ecol = tmpC;
    }

    /* Free old clipboard, if any. */
    free(clipboard);
    clipboard = NULL;
    clipboard_len = 0;

    /* Gather the text in selection. */
    size_t buf_size = 1024;
    size_t buf_used = 0;
    char *buf = malloc(buf_size);
    if (!buf) return;

    for (int r = srow; r <= erow; r++) {
        char *line = E.row[r];
        int line_len = strlen(line);

        int start_col = (r == srow) ? scol : 0;
        int end_col   = (r == erow) ? ecol : line_len;

        if (start_col < 0) start_col = 0;
        if (end_col > line_len) end_col = line_len;
        if (start_col > end_col) start_col = end_col; 

        int segment_len = end_col - start_col;

        /* Grow buffer if needed. */
        if (buf_used + segment_len + 2 >= buf_size) {
            buf_size = buf_size * 2 + segment_len + 2;
            buf = realloc(buf, buf_size);
            if (!buf) return;  // out of memory
        }

        if (segment_len > 0) {
            memcpy(buf + buf_used, line + start_col, segment_len);
            buf_used += segment_len;
        }

        /* Add a newline if this isn't the last line. */
        if (r < erow) {
            buf[buf_used] = '\n';
            buf_used++;
        }
    }
    buf[buf_used] = '\0';

    /* Store in global clipboard. */
    clipboard = buf;
    clipboard_len = buf_used;

    editorSetStatusMessage("Copied %zu bytes to clipboard", clipboard_len);
}

/*
 * Paste whatever is in `clipboard` at the current cursor.
 * If there's a selection active, delete it first.
 */
void editorPaste() {
    if (!clipboard || clipboard_len == 0) {
        editorSetStatusMessage("Clipboard is empty");
        return;
    }

    /* If we have a selection active, remove it before pasting. */
    if (E.selActive) {
        // Reuse the existing selection-deletion code:
        extern void editorDeleteSelection();
        editorDeleteSelection();
    }

    /* We can insert the clipboard line by line. */
    for (size_t i = 0; i < clipboard_len; i++) {
        if (clipboard[i] == '\n') {
            editorInsertNewline();
        } else {
            editorInsertChar(clipboard[i]);
        }
    }

    editorSetStatusMessage("Pasted %zu bytes from clipboard", clipboard_len);
}

/* ============== Improved Selection & Highlighting ============== */

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
    if (!E.selActive) return;

    int sRow, eRow, sCol, eCol;
    getSelectionBounds(&sRow, &eRow, &sCol, &eCol);
    if (sRow < 0 || eRow < 0) return;

    if (sRow == eRow) {
        char *line = E.row[sRow];
        int linelen = strlen(line);
        if (sCol < linelen) {
            memmove(&line[sCol], &line[eCol], linelen - eCol + 1);
        }
        E.cx = sCol;
        E.cy = sRow;
    } else {
        char *firstLine = E.row[sRow];
        firstLine[sCol] = '\0';

        char *lastLine = E.row[eRow];
        char *lastPart = strdup(lastLine + eCol);

        firstLine = realloc(firstLine, sCol + strlen(lastPart) + 1);
        strcat(firstLine, lastPart);
        free(lastPart);
        E.row[sRow] = firstLine;

        for (int i = sRow + 1; i <= eRow; i++) {
            free(E.row[i]);
        }
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

void editorDrawRows(struct abuf *ab) {
    int textRows = E.screenrows;
    int sRow = 0, eRow = 0, sCol = 0, eCol = 0;

    if (E.selActive) {
        getSelectionBounds(&sRow, &eRow, &sCol, &eCol);
    }

    for (int y = 0; y < textRows; y++) {
        int filerow = y + E.rowoff;
        
        if (filerow < E.numrows) {
            char *line = E.row[filerow];
            int linelen = strlen(line);

            char lineNumber[16];
            snprintf(lineNumber, sizeof(lineNumber), "%4d | ", filerow);
            abAppend(ab, lineNumber, strlen(lineNumber));

            int start = E.coloff;
            if (start > linelen) start = linelen;
            int len = linelen - start;
            if (len > E.screencols - 6)
                len = E.screencols - 6;

            int inSelection = 0;
            for (int x = 0; x < len; x++) {
                int fileCol = x + start;
                int selected = 0;

                if (E.selActive && filerow >= sRow && filerow <= eRow) {
                    if (sRow == eRow) {
                        if (fileCol >= sCol && fileCol < eCol)
                            selected = 1;
                    } else if (filerow == sRow) {
                        if (fileCol >= sCol)
                            selected = 1;
                    } else if (filerow == eRow) {
                        if (fileCol < eCol)
                            selected = 1;
                    } else {
                        selected = 1;
                    }
                }

                if (selected && !inSelection) {
                    abAppend(ab, "\x1b[7m", 4); 
                    inSelection = 1;
                } else if (!selected && inSelection) {
                    abAppend(ab, "\x1b[0m", 4); 
                    inSelection = 0;
                }
                abAppend(ab, &line[fileCol], 1);
            }
            if (inSelection)
                abAppend(ab, "\x1b[0m", 4);
        } else {
            abAppend(ab, "     | ", 7);
        }

        abAppend(ab, "\x1b[K", 3);
        abAppend(ab, "\r\n", 2);
    }
}

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

void editorDrawMessageBar(struct abuf *ab) {
    abAppend(ab, "\x1b[K", 3);
    int msglen = strlen(E.statusmsg);
    if (msglen > E.screencols)
        msglen = E.screencols;
    abAppend(ab, E.statusmsg, msglen);
    abAppend(ab, "\r\n", 2);
}

void editorRefreshScreen() {
    editorScroll();  
    struct abuf ab = ABUF_INIT;
    abAppend(&ab, "\x1b[?25l", 6);  
    abAppend(&ab, "\x1b[H", 3);     

    editorDrawRows(&ab);
    editorDrawStatusBar(&ab);
    editorDrawMessageBar(&ab);

    int cursorY = (E.cy - E.rowoff);
    int cursorX = (E.rx - E.coloff) + 8;  
    char buf[32];
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", cursorY, cursorX);
    abAppend(&ab, buf, strlen(buf));

    abAppend(&ab, "\x1b[?25h", 6);  
    write(STDOUT_FILENO, ab.b, ab.len);
    abFree(&ab);
}

/* ============== Process Keypresses ============== */

void processKeypress() {
    int c = readKey();

    // Check for configurable commands first.
    if (c == kb.quit) {
        write(STDOUT_FILENO, "\x1b[2J\x1b[H", 7);
        exit(0);
    } else if (c == kb.save) {
        editorSave();
    } else if (c == kb.copy) {
        copySelection();
    } else if (c == kb.paste) {
        editorPaste();
    } else if (c == kb.center) {
        editorCenterCursor();
    }
    // Handle other keys (newline, backspace, arrow keys, etc.)
    else if (c == '\r') {
        E.selActive = 0;
        editorInsertNewline();
    } else if (c == 127 || c == CTRL_KEY('h')) {
        if (E.selActive) {
            editorDeleteSelection();
        } else {
            editorDelChar();
        }
    } else if (c == /* DEL_KEY */ 100 /* use your DEL_KEY constant */) {
        if (E.selActive) {
            editorDeleteSelection();
        } else {
            editorDelCharForward();
        }
    } else if (c == '\t') {
        E.selActive = 0;
        for (int i = 0; i < 4; i++) {
            editorInsertChar(' ');
        }
    } else {
        switch (c) {
            case 1000:  // ARROW_LEFT (example constant; see readKey() below)
            case 1001:  // ARROW_RIGHT
            case 1002:  // ARROW_UP
            case 1003:  // ARROW_DOWN:
                E.selActive = 0;
                moveCursor(c);
                editorSetStatusMessage("Moved cursor to line %d", E.cy + 1);
                break;
            case SHIFT_ARROW_LEFT:
                editorMoveCursorWithSelection(1000); // ARROW_LEFT
                editorSetStatusMessage("Selected from (%d,%d) to (%d,%d)",
                    E.selStartY + 1, E.selStartX + 1, E.cy + 1, E.cx + 1);
                break;
            case SHIFT_ARROW_RIGHT:
                editorMoveCursorWithSelection(1001); // ARROW_RIGHT
                editorSetStatusMessage("Selected from (%d,%d) to (%d,%d)",
                    E.selStartY + 1, E.selStartX + 1, E.cy + 1, E.cx + 1);
                break;
            case SHIFT_ARROW_DOWN:
                editorMoveCursorWithSelection(1003); // ARROW_DOWN
                E.cx = strlen(E.row[E.cy]);
                editorSetStatusMessage("Selected from (%d,%d) to (%d,%d)",
                    E.selStartY + 1, E.selStartX + 1, E.cy + 1, E.cx + 1);
                break;
            case SHIFT_ARROW_UP:
                editorMoveCursorWithSelection(1002); // ARROW_UP
                E.cx = 0;
                editorSetStatusMessage("Selected from (%d,%d) to (%d,%d)",
                    E.selStartY + 1, E.selStartX + 1, E.cy + 1, E.cx + 1);
                break;
            default:
                if (isprint(c)) {
                    E.selActive = 0;
                    editorInsertChar(c);
                }
                break;
        }
    }
}

const char* keyToString(int key) {
    static char buf[32];
    if (key >= 1000 && key <= 1003) {
        const char *arrows[] = {"←", "→", "↑", "↓"};
        return arrows[key - 1000];
    } else if (key >= 2000 && key <= 2003) {
        const char *shiftArrows[] = {"Shift+←", "Shift+→", "Shift+↑", "Shift+↓"};
        return shiftArrows[key - 2000];
    } else if (key == CTRL_KEY(' ')) {
        return "Ctrl+Space";
    } else if (key < 32) {
        snprintf(buf, sizeof(buf), "Ctrl+%c", key + 64);
        return buf;
    } else if (key < 256 && isprint(key)) {
        snprintf(buf, sizeof(buf), "%c", key);
        return buf;
    } else {
        return "Custom";
    }
}

/* ============== Init & Main ============== */

void initEditor() {
    E.cx = 0;
    E.cy = 0;
    E.rx = 0;
    E.rowoff = 0;
    E.coloff = 0;
    E.scroll_margin = 5;
    E.numrows = 0;
    E.row = NULL;
    E.filename = NULL;
    E.statusmsg[0] = '\0';
    E.statusmsg_time = 0;
    E.selActive = 0;
    E.selStartX = 0;
    E.selStartY = 0;
}

int main(int argc, char *argv[]) {
    enableRawMode();
    initEditor();

    // Set default keybindings.
    kb.quit = CTRL_KEY('q');
    kb.save = CTRL_KEY('s');
    kb.copy = CTRL_KEY('c');
    kb.paste = CTRL_KEY('v');
    kb.center = CTRL_KEY('l');

    // Load (or create) the keybinds config.
    loadKeyBindings();

    if (getWindowSize(&E.screenrows, &E.screencols) == -1)
        die("getWindowSize");
    E.screenrows -= 2;

    if (argc >= 2) {
        editorOpen(argv[1]);
    } else {
        E.numrows = 2;
        E.row = malloc(sizeof(char*) * 2);
        E.row[0] = strdup("");
        E.row[1] = strdup("Type here... Use your configured keybinds (e.g. save, quit, etc.)");
        editorSetStatusMessage("New buffer (no filename)");
    }

    editorSetStatusMessage(
        "HELP: Use your configured keybinds | %s = save | %s = quit | %s = copy | %s = paste",
        (kb.save == CTRL_KEY('s') ? "Ctrl+S" : keyToString(kb.save)),
        (kb.quit == CTRL_KEY('q') ? "Ctrl+Q" : keyToString(kb.quit)),
        (kb.copy == CTRL_KEY('c') ? "Ctrl+C" : keyToString(kb.copy)),
        (kb.paste == CTRL_KEY('v') ? "Ctrl+V" : keyToString(kb.paste))
    );

    while (1) {
        editorRefreshScreen();
        processKeypress();
    }

    return 0;
}