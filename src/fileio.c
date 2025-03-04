#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include "../include/terminal.h"
#include "../include/editor.h"
#include "../include/fileio.h"
#include "../include/globals.h"

/* ============== File I/O (Save & Open) ============== */

/*
 * Save current editor content to file.
 * Uses E.filename as destination path.
 * Shows status message with result.
 */
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

/*
 * Open a file and load its contents into the editor.
 * @param filename: Path to file to open
 * Creates file if it doesn't exist.
 * Always inserts empty first line.
 * Updates editor state and shows status message.
 */
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
    
    char buffer[1024];
    while (fgets(buffer, sizeof(buffer), fp)) {
        linelen = strlen(buffer);
        while (linelen > 0 && (buffer[linelen - 1] == '\n' ||
                               buffer[linelen - 1] == '\r'))
            linelen--;
        buffer[linelen] = '\0';
        E.numrows++;
        E.row = realloc(E.row, sizeof(char*) * E.numrows);
        E.row[E.numrows - 1] = strdup(buffer);
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