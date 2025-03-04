#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include "../include/keybindings.h"
#include "../lib/cupidconf.h"
#include "../include/globals.h"

/* ============== Key Binding Support ============== */

/*
 * Convert a string from config into an integer key code.
 * Handles:
 *   - Ctrl keys (e.g. "^q" becomes CTRL_KEY('q'))
 *   - Arrow keys (e.g. "ARROW_UP")
 *   - Shift+Arrow combinations
 *   - Single character strings
 *   - Numeric values
 */
int parse_key(const char *str) {
    if (!str || !*str) return 0;
    
    // Handle Ctrl key combinations
    if (str[0] == '^' && str[1] != '\0') {
        return CTRL_KEY(str[1]);
    }
    // Handle arrow keys
    else if (strcmp(str, "ARROW_LEFT") == 0) {
        return 1000;  // ARROW_LEFT
    } else if (strcmp(str, "ARROW_RIGHT") == 0) {
        return 1001;  // ARROW_RIGHT
    } else if (strcmp(str, "ARROW_UP") == 0) {
        return 1002;  // ARROW_UP
    } else if (strcmp(str, "ARROW_DOWN") == 0) {
        return 1003;  // ARROW_DOWN
    }
    // Handle shift+arrow combinations
    else if (strcmp(str, "SHIFT_ARROW_LEFT") == 0) {
        return SHIFT_ARROW_LEFT;
    } else if (strcmp(str, "SHIFT_ARROW_RIGHT") == 0) {
        return SHIFT_ARROW_RIGHT;
    } else if (strcmp(str, "SHIFT_ARROW_UP") == 0) {
        return SHIFT_ARROW_UP;
    } else if (strcmp(str, "SHIFT_ARROW_DOWN") == 0) {
        return SHIFT_ARROW_DOWN;
    }
    // Handle single character or numeric values
    else if (strlen(str) == 1) {
        return str[0];
    } else {
        return atoi(str);
    }
}

/*
 * Load keybindings from config file.
 * Creates default config if none exists at:
 *   ~/.config/cupid/keybinds.conf
 * Updates global keybindings structure with loaded values.
 */
void loadKeyBindings(void) {
    char *home = getenv("HOME");
    if (!home) home = ".";
    
    // Build config paths with buffer safety
    char configDir[1024];
    char configPath[1024];
    
    if (snprintf(configDir, sizeof(configDir), "%s/.config/cupid", home) >= (int)sizeof(configDir) ||
        snprintf(configPath, sizeof(configPath), "%s/keybinds.conf", configDir) >= (int)sizeof(configPath)) {
        fprintf(stderr, "Path too long for config directory\n");
        return;
    }

    // Create config directory if missing
    struct stat st = {0};
    if (stat(configDir, &st) == -1) {
        if (mkdir(configDir, 0755) == -1) {
            perror("mkdir");
            return;
        }
    }

    // Create default config if missing
    if (access(configPath, F_OK) == -1) {
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

    // Load and parse config
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