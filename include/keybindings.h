#ifndef KEYBINDINGS_H
#define KEYBINDINGS_H

#define SHIFT_ARROW_UP    2000
#define SHIFT_ARROW_DOWN  2001
#define SHIFT_ARROW_RIGHT 2002
#define SHIFT_ARROW_LEFT  2003

#define CTRL_KEY(k) ((k) & 0x1f)

typedef struct {
    int quit;
    int save;
    int copy;
    int paste;
    int center;
} KeyBindings;

enum editorKey {
    ARROW_LEFT = 1000,
    ARROW_RIGHT,
    ARROW_UP,
    ARROW_DOWN,
    DEL_KEY
};

extern KeyBindings kb;

int parse_key(const char *str);
void loadKeyBindings(void);

#endif 