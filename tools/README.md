# TL Mode for Emacs

A major mode for editing TL (Tess Language) files in Emacs.

## Features

- **Syntax Highlighting**: Full syntax highlighting for TL language constructs
  - Keywords (`if`, `else`, `case`, `while`, `for`, `return`, etc.)
  - Built-in functions (`sizeof`, `alignof`)
  - Types (primitives, C-compatible types, generic types)
  - Function definitions and type constructors
  - Operators, literals (numbers, strings, characters)
  - Preprocessor directives (`#module`, `#import`, `#include`, `#ifc`, `#endc`)

- **Intelligent Indentation**: Automatic indentation based on brace structure
  - 2-space indentation by default (customizable)
  - Proper handling of nested blocks
  - Closing braces align with their opening context
  - `else` statements align with their matching `if`

- **Code Navigation**: Imenu support for quick navigation
  - Displays a flat list of all functions, types, and modules
  - Jump to any definition instantly

- **Comment Support**: C++-style `//` comments

- **Electric Indentation**: Auto-reindent on `}`, `;`, and newline

## Installation

### Manual Installation

1. Copy `tl-mode.el` to your Emacs load path (e.g., `~/.emacs.d/lisp/`)

2. Add to your `~/.emacs` or `~/.emacs.d/init.el`:

```elisp
(add-to-list 'load-path "~/.emacs.d/lisp/")
(require 'tl-mode)
```

### With use-package

```elisp
(use-package tl-mode
  :load-path "path/to/tess/tools"
  :mode "\\.tl\\'")
```

## Customization

### Indentation Offset

Change the indentation level (default: 4 spaces):

```elisp
(setq tl-indent-offset 4)
```

Or customize via `M-x customize-group RET tl RET`

## Usage

The mode automatically activates for files with the `.tl` extension.

### Key Bindings

Standard Emacs editing commands work as expected:

- `TAB` - Indent current line
- `C-M-\` - Indent region
- `C-M-a` - Beginning of function
- `C-M-e` - End of function
- `M-;` - Comment line/region
- `C-c C-j` - Jump to definition (via Imenu: `M-x imenu`)

### Imenu Navigation

Press `M-x imenu` (or `C-c C-j` if bound) to see a flat list of all definitions in the current file:
- All functions
- All type definitions
- All module declarations

All items appear in a single list in the order they appear in the file. Module names are prefixed with `#module`. Select any item to jump to its definition.

## Testing

Run the test suite:

```bash
cd tools
emacs --batch -l test-tl-mode.el
```

Test indentation:

```bash
cd tools
emacs --batch -l test-indentation.el
```

## Language Support

TL Mode supports all TL language features:

- Type inference and generic types
- Lambdas and closures
- Pattern matching (case expressions)
- C interoperability
- Module system
- Pointers and arrays
- Inline C blocks (`#ifc`...`#endc`)

## Example

```tl
#module Example
#import <Array.tl>

// Generic function with type inference
map(arr: Array.T(a), f: (a -> b)) -> Array.T(b) {
  result := Array.empty(b)
  for x <- arr.& {
    Array.push(result.&, f(x))
  }
  result
}

// Function with explicit types
factorial(n: Int) -> Int {
  if n <= 1 {
    1
  } else {
    n * factorial(n - 1)
  }
}
```

## Requirements

- Emacs 24.4 or later

## License

Same as the Tess project.

## Contributing

Contributions are welcome! Please test your changes with the included test suite.

## See Also

- [Tess Language Repository](https://github.com/tess-lang/tess)
- [TL Syntax Documentation](../docs/TL_SYNTAX.md)
