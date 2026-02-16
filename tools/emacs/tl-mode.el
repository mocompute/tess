;;; tl-mode.el --- Major mode for editing TL programming language files -*- lexical-binding: t; -*-

;; Copyright (C) 2026

;; Author: Claude Code
;; Version: 1.0.0
;; Package-Requires: ((emacs "24.4"))
;; Keywords: languages tl
;; URL: https://github.com/tess-lang/tess

;;; Commentary:

;; This package provides a major mode for editing TL programming language files.
;; TL is a statically-typed, compiled language that transpiles to C, featuring
;; type inference (Hindley-Milner style), generic types and functions, lambdas,
;; closures, and C interoperability.
;;
;; Features:
;; - Syntax highlighting for keywords, types, operators, and literals
;; - Intelligent indentation using custom brace-based indentation
;; - Comment support (C++-style //)
;; - Automatic file association for .tl files
;; - Imenu support for navigation
;;
;; Installation:
;;
;; Add to your Emacs configuration:
;;
;;   (add-to-list 'load-path "/path/to/tl-mode")
;;   (require 'tl-mode)
;;
;; Or with use-package:
;;
;;   (use-package tl-mode
;;     :load-path "/path/to/tl-mode"
;;     :mode "\\.tl\\'")
;;
;; Customization:
;;
;; Set the indentation offset (default: 4):
;;   (setq tl-indent-offset 4)

;;; Code:

;;; Customization

(defgroup tl nil
  "Major mode for editing TL programming language files."
  :group 'languages
  :prefix "tl-")

(defcustom tl-indent-offset 4
  "Number of spaces for each indentation level."
  :type 'integer
  :safe 'integerp
  :group 'tl)

(defcustom tl-tess-executable "tess"
  "Path to the tess executable used for formatting."
  :type 'string
  :safe 'stringp
  :group 'tl)

(defcustom tl-format-on-save nil
  "When non-nil, automatically format the buffer before saving."
  :type 'boolean
  :safe 'booleanp
  :group 'tl)

;;; Syntax Table

(defvar tl-mode-syntax-table
  (let ((table (make-syntax-table)))
    ;; C++-style comments
    (modify-syntax-entry ?/ ". 12" table)
    (modify-syntax-entry ?\n ">" table)

    ;; Strings
    (modify-syntax-entry ?\" "\"" table)
    (modify-syntax-entry ?\\ "\\" table)

    ;; Characters
    (modify-syntax-entry ?' "\"" table)

    ;; Operators
    (modify-syntax-entry ?+ "." table)
    (modify-syntax-entry ?- "." table)
    (modify-syntax-entry ?* "." table)
    (modify-syntax-entry ?% "." table)
    (modify-syntax-entry ?< "." table)
    (modify-syntax-entry ?> "." table)
    (modify-syntax-entry ?& "." table)
    (modify-syntax-entry ?| "." table)
    (modify-syntax-entry ?= "." table)
    (modify-syntax-entry ?! "." table)
    (modify-syntax-entry ?: "." table)

    ;; Parentheses and brackets
    (modify-syntax-entry ?\( "()" table)
    (modify-syntax-entry ?\) ")(" table)
    (modify-syntax-entry ?\{ "(}" table)
    (modify-syntax-entry ?\} "){" table)
    (modify-syntax-entry ?\[ "(]" table)
    (modify-syntax-entry ?\] ")[" table)

    table)
  "Syntax table for `tl-mode'.")

;;; Font Lock (Syntax Highlighting)

(defconst tl-font-lock-keywords
  (let* (
         ;; Keywords
         (keywords '("if" "else" "case" "when" "defer" "while" "for" "return"
                     "void" "null" "true" "false" "Void" "any"))

         ;; Built-in functions
         (builtins '("sizeof" "alignof"))

         ;; Types (comprehensive list)
         (types '("Int" "Float" "Bool" "Byte"
                  ;; C types
                  "CChar" "CShort" "CInt" "CLong" "CLongLong"
                  "CSize" "CPtrDiff"
                  ;; C unsigned types
                  "CUnsignedChar" "CUnsignedShort" "CUnsignedInt"
                  "CUnsignedLong" "CUnsignedLongLong"
                  ;; C fixed-width types
                  "CInt8" "CInt16" "CInt32" "CInt64"
                  "CUInt8" "CUInt16" "CUInt32" "CUInt64"
                  ;; C floating point
                  "CFloat" "CDouble" "CLongDouble"
                  ;; Common generic types
                  "Ptr" "CArray" "Array" "Iter"))

         ;; Create regexp patterns
         (keywords-regexp (regexp-opt keywords 'symbols))
         (builtins-regexp (regexp-opt builtins 'symbols))
         (types-regexp (regexp-opt types 'symbols)))

    `(
      ;; Directives (must be at line start)
      ("^[ \t]*\\(#\\(?:module\\|import\\|include\\|ifc\\|endc\\)\\)\\>"
       (1 font-lock-preprocessor-face))

      ("^[ \t]*\\(#\\(?:define\\|undef\\|ifdef\\|ifndef\\|endif\\)\\)\\>"
       (1 font-lock-preprocessor-face))

      ;; Attributes [[...]]
      ("\\[\\[\\([^]]*\\)\\]\\]" . font-lock-preprocessor-face)

      ;; Keywords
      (,keywords-regexp . font-lock-keyword-face)

      ;; Built-in functions
      (,builtins-regexp . font-lock-builtin-face)

      ;; Types
      (,types-regexp . font-lock-type-face)

      ;; Function definitions (function_name followed by optional type args and paren at line start)
      ("^[ \t]*\\([a-zA-Z_][a-zA-Z0-9_]*\\)\\(?:\\[.*?\\]\\)?[ \t]*("
       (1 font-lock-function-name-face))

      ;; Type constructors (capitalized identifier followed by optional type args and paren)
      ("\\<\\([A-Z][a-zA-Z0-9_]*\\)\\(?:\\[.*?\\]\\)?[ \t]*("
       (1 font-lock-type-face))

      ;; Generic types with type arguments without parens (e.g., Array[Int])
      ("\\<\\([A-Z][a-zA-Z0-9_]*\\)\\[" (1 font-lock-type-face))

      ;; Type parameters inside square brackets (e.g., [T], [a], [Int, Bool])
      ;; First type arg after opening bracket
      ("\\[\\([a-zA-Z][a-zA-Z0-9_]*\\)" (1 font-lock-type-face))
      ;; Subsequent type args: ", x]" or ", x," pattern (not ", x)")
      (",[ \t]*\\([a-zA-Z][a-zA-Z0-9_]*\\)[ \t]*[],]" (1 font-lock-type-face))

      ;; Multi-character operators (use default punctuation face, no special highlighting)
      ;; Operators like ->, :=, ::, .&, .*, ==, !=, <=, >=, &&, || are left unhighlighted

      ;; Numeric literals
      ;; Hexadecimal
      ("\\<0[xX][0-9a-fA-F_]+\\>" . font-lock-constant-face)
      ;; Binary
      ("\\<0[bB][01_]+\\>" . font-lock-constant-face)
      ;; Float with exponent
      ("\\<[0-9][0-9_]*\\.[0-9_]*\\(?:[eE][+-]?[0-9_]+\\)?\\>" . font-lock-constant-face)
      ("\\<[0-9][0-9_]*[eE][+-]?[0-9_]+\\>" . font-lock-constant-face)
      ;; Float without exponent
      ("\\<[0-9][0-9_]*\\.[0-9_]+\\>" . font-lock-constant-face)
      ;; Integer (including octal and with underscores)
      ("\\<[0-9][0-9_]*\\>" . font-lock-constant-face)

      ;; Character literals (including \0 null, \n, \r, \t, \', \", \\)
      ("'\\(?:\\\\[0nrt'\"\\\\]\\|[^']\\)'" . font-lock-string-face)
      ))
  "Font lock keywords for TL mode.")

;;; Syntax Propertize

(defun tl-syntax-propertize (start end)
  "Apply syntax properties to C string prefixes (c\"...\") from START to END."
  (goto-char start)
  (funcall
   (syntax-propertize-rules
    ("\\(c\\)\\(\"\\)\\(?:[^\"\\\\]\\|\\\\.\\)*\\(\"\\)"
     (1 "|") (2 ".") (3 "|")))
   start end))

;;; Indentation

(defun tl-indent-line ()
  "Indent current line as TL code."
  (interactive)
  (let ((indent-col 0)
        (pos (- (point-max) (point))))
    (save-excursion
      (beginning-of-line)
      (setq indent-col (tl-calculate-indentation)))
    (if (< (current-column) (current-indentation))
        (indent-line-to indent-col)
      (save-excursion (indent-line-to indent-col)))
    ;; If point was in the indentation, move to the start of content
    (when (> (- (point-max) pos) (point))
      (goto-char (- (point-max) pos)))))

(defun tl-previous-code-line-info ()
  "Get indentation info from the previous non-blank, non-comment line.
Returns a cons cell (INDENT . ENDS-WITH-OPEN-BRACE)."
  (save-excursion
    (forward-line -1)
    (while (and (not (bobp))
                (looking-at "^[ \t]*\\(?:$\\|//\\|#\\)"))
      (forward-line -1))
    (let ((indent (current-indentation))
          (opens-block nil))
      ;; Check if line ends with opening brace (ignoring trailing comment)
      (end-of-line)
      (skip-chars-backward " \t")
      (when (and (> (point) (line-beginning-position))
                 (eq (char-before) ?\{))
        (setq opens-block t))
      (cons indent opens-block))))

(defun tl-tagged-union-continuation-indent ()
  "Calculate indentation for a tagged union continuation line (starts with |).
Aligns with the | on the first line of the tagged union definition."
  (save-excursion
    (forward-line -1)
    ;; Skip blank lines and comments
    (while (and (not (bobp))
                (looking-at "^[ \t]*\\(?:$\\|//\\)"))
      (forward-line -1))
    ;; Look for | at any position on this line
    (if (looking-at "^\\(.*?\\)|")
        (length (match-string 1))
      ;; Fallback: look for "= |" pattern (start of tagged union definition)
      (if (re-search-backward "^[a-zA-Z_][a-zA-Z0-9_]*\\(?:(.*)\\)?[ \t]*=[ \t]*|" nil t)
          (progn
            (goto-char (match-end 0))
            (backward-char 1)
            (current-column))
        0))))

(defun tl-after-tagged-union-p ()
  "Return t if the previous non-blank code was part of a tagged union.
Used to detect when we should reset to column 0."
  (save-excursion
    (forward-line -1)
    ;; Skip blank lines
    (while (and (not (bobp))
                (looking-at "^[ \t]*$"))
      (forward-line -1))
    ;; Check if previous non-blank line starts with |
    (looking-at "^[ \t]*|")))

(defun tl-calculate-indentation ()
  "Calculate the indentation for the current line."
  (save-excursion
    (beginning-of-line)
    (skip-chars-forward " \t")
    (cond
     ;; Start of buffer
     ((bobp) 0)

     ;; Closing brace: align with the opening brace's line
     ((looking-at "}")
      (save-excursion
        (condition-case nil
            (progn
              (forward-char 1)
              (backward-sexp)
              (current-indentation))
          (error 0))))

     ;; Else: align with the matching if
     ((looking-at "\\<else\\>")
      (save-excursion
        (condition-case nil
            (progn
              (forward-word 1)
              (backward-sexp)
              (current-indentation))
          (error 0))))

     ;; Tagged union continuation: line starts with |
     ((looking-at "|")
      (tl-tagged-union-continuation-indent))

     ;; After tagged union: top-level definition resets to column 0
     ((and (tl-after-tagged-union-p)
           (looking-at "[a-zA-Z_]"))
      0)

     ;; Default: base on previous line
     (t
      (let* ((info (tl-previous-code-line-info))
             (prev-indent (car info))
             (opens-block (cdr info)))
        (if opens-block
            (+ prev-indent tl-indent-offset)
          prev-indent))))))

(defun tl-indent-region (start end)
  "Indent region from START to END."
  (save-excursion
    (goto-char start)
    (while (< (point) end)
      (unless (looking-at "^[ \t]*$")
        (tl-indent-line))
      (forward-line 1))))

;;; Formatting

(defun tl-format-buffer ()
  "Format the current buffer using `tess fmt'."
  (interactive)
  (let ((output-buf (generate-new-buffer " *tl-fmt-output*"))
        (stderr-file (make-temp-file "tl-fmt-stderr"))
        (original-point (point)))
    (unwind-protect
        (let ((exit-code (call-process-region (point-min) (point-max)
                                              tl-tess-executable
                                              nil (list output-buf stderr-file)
                                              nil "fmt")))
          (if (zerop exit-code)
              (let ((formatted (with-current-buffer output-buf (buffer-string))))
                (erase-buffer)
                (insert formatted)
                (goto-char (min original-point (point-max))))
            (message "tess fmt failed: %s"
                     (string-trim
                      (with-temp-buffer
                        (insert-file-contents stderr-file)
                        (buffer-string))))))
      (kill-buffer output-buf)
      (delete-file stderr-file))))

(defun tl-format-on-save-hook ()
  "Format the buffer before saving if `tl-format-on-save' is non-nil."
  (when tl-format-on-save
    (tl-format-buffer)))

(defun tl-format-region (start end)
  "Format the region from START to END using `tess fmt'."
  (interactive "r")
  (let ((output-buf (generate-new-buffer " *tl-fmt-output*"))
        (stderr-file (make-temp-file "tl-fmt-stderr")))
    (unwind-protect
        (let ((exit-code (call-process-region start end
                                              tl-tess-executable
                                              nil (list output-buf stderr-file)
                                              nil "fmt")))
          (if (zerop exit-code)
              (let ((formatted (with-current-buffer output-buf (buffer-string))))
                (delete-region start end)
                (goto-char start)
                (insert formatted))
            (message "tess fmt failed: %s"
                     (string-trim
                      (with-temp-buffer
                        (insert-file-contents stderr-file)
                        (buffer-string))))))
      (kill-buffer output-buf)
      (delete-file stderr-file))))

;;; Imenu Support

(defun tl-imenu-create-index ()
  "Create a flat imenu index for TL mode.
Returns a list of (name . position) pairs for all functions, types, and modules."
  (let ((index '()))
    (save-excursion
      (goto-char (point-min))
      ;; Find all modules
      (while (re-search-forward "^[ \t]*#module[ \t]+\\([a-zA-Z_][a-zA-Z0-9_]*\\)" nil t)
        (push (cons (concat "#module " (match-string-no-properties 1))
                    (match-beginning 1))
              index))

      ;; Find all function definitions and type constructors
      (goto-char (point-min))
      (while (re-search-forward "^[ \t]*\\([a-zA-Z_][a-zA-Z0-9_]*\\)[ \t]*\\(?:\\[\\|(\\)" nil t)
        (let ((name (match-string-no-properties 1)))
          (push (cons name (match-beginning 1))
                index))))

    ;; Return in forward order (reverse because we pushed)
    (nreverse index)))

;;; Keymap

(defvar tl-mode-map
  (let ((map (make-sparse-keymap)))
    (define-key map (kbd "C-c C-f") #'tl-format-buffer)
    map)
  "Keymap for `tl-mode'.")

;;; Mode Definition

;;;###autoload
(define-derived-mode tl-mode prog-mode "TL"
  "Major mode for editing TL programming language files.

TL is a statically-typed, compiled language that transpiles to C,
featuring type inference (Hindley-Milner style), generic types and
functions, lambdas, closures, and C interoperability.

\\{tl-mode-map}"
  :syntax-table tl-mode-syntax-table
  :group 'tl

  ;; Comments
  (setq-local comment-start "// ")
  (setq-local comment-end "")
  (setq-local comment-start-skip "//+[ \t]*")

  ;; Font lock
  (setq-local font-lock-defaults '(tl-font-lock-keywords))

  ;; Syntax propertize (for c"..." string prefix)
  (setq-local syntax-propertize-function #'tl-syntax-propertize)

  ;; Indentation
  (setq-local indent-line-function #'tl-indent-line)
  (setq-local indent-region-function #'tl-indent-region)

  ;; Imenu
  (setq-local imenu-create-index-function #'tl-imenu-create-index)

  ;; Electric indentation
  (setq-local electric-indent-chars
              (append '(?\n ?\{ ?\} ?\;) electric-indent-chars))

  ;; Beginning/end of defun
  (setq-local beginning-of-defun-function #'tl-beginning-of-defun)
  (setq-local end-of-defun-function #'tl-end-of-defun)

  ;; Format on save
  (add-hook 'before-save-hook #'tl-format-on-save-hook nil t))

(defun tl-beginning-of-defun (&optional arg)
  "Move backward to the beginning of a function definition.
With ARG, do it that many times."
  (interactive "p")
  (let ((arg (or arg 1)))
    (if (< arg 0)
        (tl-end-of-defun (- arg))
      (dotimes (_ arg)
        (re-search-backward "^[ \t]*[a-zA-Z_][a-zA-Z0-9_]*[ \t]*\\(?:\\[\\|(\\)" nil 'move)))))

(defun tl-end-of-defun (&optional arg)
  "Move forward to the end of a function definition.
With ARG, do it that many times.
Handles both regular functions and type constructors like `Point[a] : { ... }`."
  (interactive "p")
  (let ((arg (or arg 1)))
    (if (< arg 0)
        (tl-beginning-of-defun (- arg))
      (dotimes (_ arg)
        (when (re-search-forward "^[ \t]*[a-zA-Z_][a-zA-Z0-9_]*[ \t]*\\(?:\\[\\|(\\)" nil 'move)
          (forward-char -1)
          ;; Skip type args [...] if present
          (when (eq (char-after) ?\[)
            (forward-sexp)
            (skip-chars-forward " \t"))
          ;; Skip parameter list (...) if present
          (when (eq (char-after) ?\()
            (forward-sexp))
          (skip-chars-forward " \t\n")
          ;; Handle return type annotation (-> Type)
          (when (looking-at "->")
            (forward-char 2)
            (skip-chars-forward " \t")
            ;; Skip return type (may include parens/brackets for generics)
            (if (looking-at "(")
                (forward-sexp)
              (progn
                (skip-chars-forward "a-zA-Z0-9_.")
                (when (eq (char-after) ?\[)
                  (forward-sexp)))))
          (skip-chars-forward " \t\n")
          ;; Handle type constructor colon
          (when (looking-at ":")
            (forward-char 1))
          (skip-chars-forward " \t\n")
          ;; Now we should be at the opening brace
          (when (looking-at "{")
            (forward-sexp)))))))

;;; File Association

;;;###autoload
(add-to-list 'auto-mode-alist '("\\.tl\\'" . tl-mode))

(provide 'tl-mode)

;;; tl-mode.el ends here
