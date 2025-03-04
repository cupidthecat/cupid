# cupid - A Minimalist Text Editor for Linux

**cupid** is a lightweight terminal-based text editor designed for GNU/Linux systems. Built with simplicity in mind, it provides essential editing capabilities while maintaining a small codebase written in pure C. Ideal for quick edits or terminal-based workflows.

## Features

- **Minimalist Design**: Clean interface with line numbers and status information.
- **Configurable Keybindings with `cupidconf`**: Dynamically load user-defined keybindings from a config file (default is created if not found).
- **Text Selection**: Supports selecting text with Shift+Arrow keys.
- **Clipboard Integration**: Copy selections or paste from a local clipboard buffer (support for OSC 52 can be added based on your terminal).
- **File Operations**: Save with Ctrl+S, open files via command line.
- **Visual Feedback**:
  - Line numbers
  - Selection highlighting
  - Status bar with file information
  - Message bar with contextual help

## Key Bindings

By default, the editor ships with these bindings, but you can override them in your `cupidconf` config:

| Key Combination | Action                           |
|-----------------|-----------------------------------|
| `Ctrl+Q`        | Quit                              |
| `Ctrl+S`        | Save file                         |
| `Ctrl+C`        | Copy selection to local clipboard |
| `Ctrl+V`        | Paste from local clipboard        |
| `Ctrl+L`        | Center cursor in viewport         |
| `Shift+Arrows`  | Select text                       |
| `Backspace`     | Delete previous character/selection |
| `Tab`           | Insert 4 spaces                   |
| `Enter`         | Insert new line                   |

## `cupidconf` Integration for Keybindings

`cupidconf` is used to **load and parse** a user-specific configuration file located at:
```
~/.config/cupid/keybinds.conf
```
- If this file doesn’t exist, cupid **automatically creates** it with sensible default settings for **quit**, **save**, **copy**, **paste**, and **center** commands.
- Each line in the config is in the form `key = value`.  
- You can customize these defaults by editing `~/.config/cupid/keybinds.conf`.

**Sample `keybinds.conf`:**
```ini
# Default keybinds for Cupid Editor
quit = ^q
save = ^s
copy = ^c
paste = ^v
center = ^l
```
- `^q` means Ctrl+Q, `^s` means Ctrl+S, etc.
- You can also specify arrow keys (e.g., `ARROW_UP`, `ARROW_DOWN`), shifted variants (`SHIFT_ARROW_UP`), or literal characters.

When you run cupid, it will read from this config file and adjust its internal keymap accordingly, allowing you to change the bindings to fit your workflow.

## Technical Details

- Written in standard C with POSIX extensions
- Terminal handling with raw mode for direct input processing
- Scrolling with soft margins for better navigation
- **Keybindings dynamically loaded via `cupidconf`** from `~/.config/cupid/keybinds.conf`
- Optimized for small to medium-sized files

## Installation

### Requirements
- GNU/Linux environment
- C compiler (gcc or clang)
- POSIX-compliant terminal

### Build from Source

```bash
git clone https://github.com/yourusername/cupid.git
cd cupid
make  # Build with provided Makefile
sudo cp cupid /usr/local/bin/  # (Optional) Install system-wide
```

> **Note**: By default, this editor compiles with the `cupidconf` library (`cupidconf.c` and `cupidconf.h` in `lib/`). Make sure they are included in your build commands or Makefile.

## Usage

```bash
cupid [filename]  # Open existing file or create a new one
```

When no filename is provided, cupid starts with a blank buffer.

## Limitations

- No syntax highlighting
- No search/replace functionality
- No undo/redo capability
- Terminal’s raw mode is used, which can vary in behavior across environments

## Contributing

Contributions are welcome! Please follow these steps:
1. Fork the repository
2. Create a feature branch
3. Make your changes
4. Submit a pull request

## License

Released under the **GNU GPLv3**.