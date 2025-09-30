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
#include "../include/keybindings.h"
#include "../include/editor.h"
#include "../include/terminal.h"
#include "../include/clipboard.h"
#include "../include/fileio.h"
#include "../include/globals.h"

EditorConfig E;
KeyBindings kb;

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