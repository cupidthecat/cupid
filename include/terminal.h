#ifndef _TERMINAL_H
#define _TERMINAL_H

struct abuf {
    char *b;
    int len;
};

#define ABUF_INIT {NULL, 0}

void abAppend(struct abuf *ab, const char *s, int len);
void abFree(struct abuf *ab);

void die(const char *s);
void disableRawMode();
void enableRawMode();
int getWindowSize(int *rows, int *cols);

#endif 