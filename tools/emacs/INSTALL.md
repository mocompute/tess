# Installing TL Mode for Emacs

## Quick Start

### Option 1: Load Directly from Tools Directory

Add to your `~/.emacs` or `~/.emacs.d/init.el`:

```elisp
;; Add the tools directory to load-path
(add-to-list 'load-path "/path/to/tess/tools/emacs")

;; Load tl-mode
(require 'tl-mode)
```

Replace `/path/to/tess/tools/emacs` with the actual path to this directory.

### Option 2: Copy to Emacs Directory

1. Copy `tl-mode.el` to your Emacs site-lisp directory:

```bash
cp tl-mode.el ~/.emacs.d/lisp/
```

2. Add to your init file:

```elisp
(add-to-list 'load-path "~/.emacs.d/lisp")
(require 'tl-mode)
```

### Option 3: Use with use-package

If you use `use-package`:

```elisp
(use-package tl-mode
  :load-path "/path/to/tess/tools/emacs"
  :mode "\\.tl\\'"
  :config
  ;; Optional: customize indentation
  (setq tl-indent-offset 4))
```

## Verification

After installation, open any `.tl` file in Emacs. You should see:

1. **Mode line** shows "TL" (indicating tl-mode is active)
2. **Syntax highlighting** is applied to keywords, types, and operators
3. **TAB key** properly indents code

To verify manually:

```elisp
M-x tl-mode
```

Check the mode with:

```elisp
M-: major-mode RET
```

Should return: `tl-mode`

## Testing

Run the test suite to verify installation:

```bash
cd /path/to/tess/tools/emacs
emacs --batch -l test-tl-mode.el
```

Expected output: All tests should pass with ✓ marks.

## Customization

### Change Indentation Level

```elisp
;; Use 2-space indentation instead of 4
(setq tl-indent-offset 2)
```

Or use the customization interface:

```
M-x customize-group RET tl RET
```

### Key Bindings

Add custom key bindings in your init file:

```elisp
(with-eval-after-load 'tl-mode
  (define-key tl-mode-map (kbd "C-c C-c") 'compile)
  (define-key tl-mode-map (kbd "C-c C-t") 'tess-run-tests))
```

### Integration with Compilation

Set up compilation command for TL files:

```elisp
(with-eval-after-load 'tl-mode
  (add-hook 'tl-mode-hook
            (lambda ()
              (set (make-local-variable 'compile-command)
                   (format "./tess exe %s -o /tmp/a.out"
                          (buffer-file-name))))))
```

Then use `M-x compile` or `C-c C-c` to compile the current file.

## Troubleshooting

### Mode doesn't activate automatically

Check that the file association is set:

```elisp
M-: (assoc "\\.tl\\'" auto-mode-alist) RET
```

Should return: `("\\.tl\\'" . tl-mode)`

### Syntax highlighting not working

Ensure font-lock-mode is enabled:

```elisp
M-x font-lock-mode
```

Or add to init file:

```elisp
(global-font-lock-mode 1)
```

### Indentation issues

Force re-indentation of entire file:

```
C-x h         (select all)
C-M-\         (indent region)
```

Or:

```
M-x mark-whole-buffer
M-x indent-region
```

## Uninstallation

Remove the following from your init file:

```elisp
(require 'tl-mode)
;; or
(use-package tl-mode ...)
```

Then restart Emacs or evaluate:

```elisp
M-x unload-feature RET tl-mode RET
```

## Getting Help

- Check the README: `tools/emacs/README.md`
- View inline documentation: `M-x describe-mode` (in a TL buffer)
- Check function documentation: `C-h f tl-mode RET`
- Report issues: [Tess GitHub repository](https://github.com/tess-lang/tess/issues)

## Requirements

- A recent version of Emacs
- No external dependencies

## Next Steps

After installation:

1. Open any `.tl` file from the Tess repository
2. Try the indentation: `TAB` or `C-M-\`
3. Navigate with Imenu: `M-x imenu`
4. Explore the code: `C-M-a` (beginning of function), `C-M-e` (end of function)

Happy coding in TL!
