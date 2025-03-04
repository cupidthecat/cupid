#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <stdarg.h>
#include <errno.h>
#include <ctype.h>

#include "../include/keybindings.h"
#include "../include/editor.h"
#include "../include/terminal.h"
#include "../include/clipboard.h"
#include "../include/fileio.h"
#include "../include/globals.h"

// forward decs
void editorDrawRows(struct abuf *ab);
void editorDrawStatusBar(struct abuf *ab);
void editorDrawMessageBar(struct abuf *ab);
int readKey();

char *strdup(const char *s) {
    size_t len = strlen(s) + 1;
    void *new_s = malloc(len);
    if (new_s == NULL) {
        return NULL;
    }
    return (char *)memcpy(new_s, s, len);
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

void editorInsertChar(int c) {
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