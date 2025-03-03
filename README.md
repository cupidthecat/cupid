# cupid - A Minimalist Text Editor for Linux

**cupid** is a lightweight terminal-based text editor designed for GNU/Linux systems. Built with simplicity in mind, it provides essential editing capabilities while maintaining a small codebase written in pure C. Ideal for quick edits or terminal-based workflows.

## Features

- **Minimalist Design**: Clean interface with line numbers and status information
- **Text Selection**: Supports selecting text with Shift+Arrow keys
- **Clipboard Integration**: Copy selections using OSC 52 terminal protocol with Ctrl+C
- **File Operations**: Save with Ctrl+S, open files via command line
- **Visual Feedback**:
  - Line numbers
  - Selection highlighting
  - Status bar with file information
  - Message bar with contextual help

## Key Bindings

| Key Combination | Action |
|-----------------|--------|
| `Ctrl+Q` | Quit |
| `Ctrl+S` | Save file |
| `Ctrl+C` | Copy selection to clipboard |
| `Ctrl+L` | Center cursor in viewport |
| `Shift+Arrows` | Select text |
| `Backspace` | Delete previous character or selection |
| `Tab` | Insert 4 spaces |
| `Enter` | Insert new line |

## Technical Details

- Written in standard C with POSIX extensions
- Terminal handling with raw mode for direct input processing
- Scrolling with soft margins for better navigation
- Base64 encoding for clipboard operations via OSC 52
- Supports horizontal and vertical scrolling for large files

## Installation

### Requirements
- GNU/Linux environment
- C compiler (gcc or clang)
- POSIX-compliant terminal with OSC 52 support

### Build from Source
```bash
git clone https://github.com/yourusername/cupid.git
cd cupid
gcc -o cupid main.c
sudo cp cupid /usr/local/bin/  # Optional: install system-wide
```

## Usage

```bash
cupid [filename]  # Open existing file or create new
```

When no filename is provided, cupid starts with a blank buffer.

## Limitations

- No syntax highlighting
- No search/replace functionality
- No undo/redo capability
- Requires terminal with OSC 52 support for clipboard operations
- Optimized for small to medium-sized files

## Contributing

Contributions welcome! Please follow these steps:
1. Fork the repository
2. Create a feature branch
3. Make your changes
4. Submit a pull request

## License

Released under GNU GPLv3
