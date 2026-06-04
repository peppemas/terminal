# Terminal

A modern, lightweight shell for Windows implemented in C++20. It provides a Unix-like command-line experience with native Windows integration, featuring built-in utilities, piping, and a rich interactive line editor.

## Key Features

- **Unix-like Built-ins**: Familiar commands like `ls`, `grep`, `cat`, and `tail` implemented natively in C++.
- **Command Piping**: Support for chaining commands using the pipe (`|`) operator (e.g., `cat file.txt | grep pattern`).
- **Advanced Tokenization**: Handles single/double quotes and backslash escaping for complex arguments.
- **Interactive Line Editing**:
    - **Tab Completion**: Smart autocompletion for registered commands and file system paths.
    - **Command History**: Persistent-session history accessible via arrow keys.
    - **Word-based Navigation**: Move quickly through the input buffer using Ctrl+Arrow keys.
    - **UTF-8 & Emoji Support**: Full support for multibyte character input and display.
- **VT100/ANSI Support**: Uses Virtual Terminal sequences for colored output and advanced cursor management.
- **External Commands**: Capability to launch and manage external Windows executables.

## Supported Commands

| Command | Description |
| :--- | :--- |
| `ls` | List directory contents. Supports `-l` (long format), `-a` (all files), `-R` (recursive), `-h` (human readable sizes), `-r` (reverse order), `-t` (sort by time), and `-S` (sort by size). |
| `cd` | Change the current working directory. |
| `pwd` | Print the current working directory. |
| `cat` | Concatenate and display file contents. Supports piping. |
| `grep` | Search text using regular expressions. Supports piping. |
| `tail` | Output the last part of files. Supports `-n` (lines), `-c` (bytes), `-q` (quiet), and `-v` (verbose). Supports piping. |
| `echo` | Display a line of text. Supports piping. |
| `cp` | Copy files to a destination. |
| `mv` | Move or rename files and directories. |
| `rm` | Remove files or directories. Use `-r` for recursive removal. |
| `clear` / `cls` | Clear the terminal screen and scrollback buffer. |
| `open` | Open the specified directory in Windows File Explorer. |

## Keyboard Shortcuts

| Shortcut | Action |
| :--- | :--- |
| **Enter** | Execute the entered command. |
| **Tab** | Autocomplete the current command or path. |
| **Up / Down** | Cycle through command history. |
| **Left / Right** | Move the cursor one character at a time. |
| **Ctrl + Left / Right** | Move the cursor one word at a time. |
| **Backspace** | Delete the character before the cursor. |
| **Ctrl + C** | Clear the current input line or interrupt a foreground process. |

## Getting Started

### Prerequisites

- A Windows environment.
- A C++20 compatible compiler (MSVC recommended).
- [CMake](https://cmake.org/) 3.20 or higher.

### Building

1. Clone the repository.
2. Create a build directory:
   ```powershell
   mkdir build
   cd build
   ```
3. Generate project files and build:
   ```powershell
   cmake ..
   cmake --build . --config Release
   ```
4. Run the terminal:
   ```powershell
   .\Release\terminal.exe
   ```

## Project Structure

- `include/`: Header files for all terminal components.
- `src/`: Implementation of the terminal logic and commands.
- `main.cpp`: Entry point of the application.
- `transform_commands.py`: A utility script used to refactor command signatures for pipeline support.

## Technical Details

The terminal uses the Windows Console API in raw mode to handle input character-by-character, allowing for a highly responsive UI with custom completion and history logic. Command parsing is handled by a dedicated `CommandParser` that supports quoted arguments and pipeline stages. External processes are managed via `CreateProcess` with appropriate handle inheritance for I/O redirection.
