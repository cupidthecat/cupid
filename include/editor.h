#ifndef EDITOR_H
#define EDITOR_H

#include <time.h>  // Add for time_t
#include <ctype.h> // Add for isprint

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

extern EditorConfig E;

// Function declarations for editor operations
void editorRefreshScreen();
void editorSetStatusMessage(const char *fmt, ...);
void editorCenterCursor();
void processKeypress();
const char* keyToString(int key);
void initEditor();
void editorScroll();
void editorMoveCursorWithSelection(int key);
void moveCursor(int key);
void editorInsertChar(int c);
void editorRowMerge(int to, int from);
void editorDelChar();
void editorDelCharForward();
void editorInsertNewline();
void editorDeleteSelection();
void editorPaste();
void copySelection();
void editorSave();
void editorOpen(const char *filename);
char *strdup(const char *s);
#endif 