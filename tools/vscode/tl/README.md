# TL Language Support for VS Code

Syntax highlighting and formatting support for the [TL programming language](https://github.com/tess-lang/tess) (Tess).

## Features

- Syntax highlighting for all TL constructs
  - Keywords, built-in types, and constants
  - Directives (`#module`, `#import`, `#include`, `#ifc`/`#endc`)
  - Function definitions and type constructors
  - String literals, C string literals (`c"..."`), and character literals
  - Numeric literals (integers, floats, hex, binary, with `_` separators)
  - Attributes (`[[...]]`)
  - Line comments (`//`)
- Code formatting via `tess fmt`
- Bracket matching and auto-closing pairs
- Comment toggling

## Installation

Copy the `tools/vscode/tl` directory into `~/.vscode/extensions/` and reload VS Code.

## Configuration

| Setting              | Default  | Description                                  |
|----------------------|----------|----------------------------------------------|
| `tl.tessExecutable`  | `"tess"` | Path to the `tess` executable for formatting |
| `tl.formatOnSave`    | `false`  | Automatically format TL files on save        |

### Example settings.json

```json
{
  "tl.tessExecutable": "/usr/local/bin/tess",
  "tl.formatOnSave": true
}
```

## Formatting

The extension provides document formatting using `tess fmt`. You can format via:

- **Command palette**: "Format Document" (`Shift+Alt+F`)
- **On save**: Enable `tl.formatOnSave` in settings
- **Selection**: Select a region and use "Format Selection"

The `tess` executable must be on your `PATH` or configured via `tl.tessExecutable`.
