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
;; - Intelligent indentation using SMIE (Simple Minded Indentation Engine)
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

(require 'smie)

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

    ;; Underscores are part of identifiers
    (modify-syntax-entry ?_ "w" table)

    table)
  "Syntax table for `tl-mode'.")

;;; Font Lock (Syntax Highlighting)

(defconst tl-font-lock-keywords
  (let* (
         ;; Keywords
         (keywords '("if" "else" "case" "while" "for" "return"
                     "void" "null" "true" "false" "Type" "Void" "any"))

         ;; Built-in functions
         (builtins '("sizeof" "alignof"))

         ;; Types (comprehensive list)
         (types '("Int" "Float" "Bool" "String" "Byte"
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
         (keywords-regexp (regexp-opt keywords 'words))
         (builtins-regexp (regexp-opt builtins 'words))
         (types-regexp (regexp-opt types 'words)))

    `(
      ;; Directives (must be at line start)
      ("^[ \t]*\\(#\\(?:module\\|import\\|include\\|ifc\\|endc\\)\\)\\>"
       (1 font-lock-preprocessor-face))

      ;; Keywords
      (,keywords-regexp . font-lock-keyword-face)

      ;; Built-in functions
      (,builtins-regexp . font-lock-builtin-face)

      ;; Types
      (,types-regexp . font-lock-type-face)

      ;; Function definitions (function_name followed by paren at line start)
      ("^[ \t]*\\([a-zA-Z_][a-zA-Z0-9_]*\\)[ \t]*("
       (1 font-lock-function-name-face))

      ;; Type constructors (capitalized identifier followed by paren)
      ("\\<\\([A-Z][a-zA-Z0-9_]*\\)[ \t]*("
       (1 font-lock-type-face))

      ;; Multi-character operators
      ("\\(->\\|:=\\|::\\|\\.&\\|\\.\\*\\|==\\|!=\\|<=\\|>=\\|&&\\|||\\)"
       (1 font-lock-builtin-face))

      ;; Numeric literals
      ;; Hexadecimal
      ("\\<0[xX][0-9a-fA-F_]+\\>" . font-lock-constant-face)
      ;; Float with exponent
      ("\\<[0-9][0-9_]*\\.[0-9_]*\\(?:[eE][+-]?[0-9_]+\\)?\\>" . font-lock-constant-face)
      ("\\<[0-9][0-9_]*[eE][+-]?[0-9_]+\\>" . font-lock-constant-face)
      ;; Float without exponent
      ("\\<[0-9][0-9_]*\\.[0-9_]+\\>" . font-lock-constant-face)
      ;; Integer (including octal and with underscores)
      ("\\<[0-9][0-9_]*\\>" . font-lock-constant-face)

      ;; Character literals
      ("'\\(?:\\\\[nrt'\"\\\\]\\|[^']\\)'" . font-lock-string-face)
      ))
  "Font lock keywords for TL mode.")

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

     ;; Default: check the previous non-empty line
     (t
      (let ((cur-indent 0)
            (base-indent 0))
        (save-excursion
          ;; Find previous non-empty, non-comment line
          (forward-line -1)
          (while (and (not (bobp))
                      (or (looking-at "^[ \t]*$")
                          (looking-at "^[ \t]*//")
                          (looking-at "^[ \t]*#")))
            (forward-line -1))

          (setq base-indent (current-indentation))

          ;; Look for opening brace on previous line
          (end-of-line)
          (when (re-search-backward "[{}]" (line-beginning-position) t)
            (if (looking-at "{")
                ;; Previous line opens a block: increase indent
                (setq cur-indent (+ base-indent tl-indent-offset))
              ;; Previous line closes a block: use base indent
              (setq cur-indent base-indent)))

          ;; If no brace found, continue at same level
          (when (= cur-indent 0)
            (setq cur-indent base-indent)))
        cur-indent)))))

(defun tl-indent-region (start end)
  "Indent region from START to END."
  (save-excursion
    (goto-char start)
    (while (< (point) end)
      (unless (looking-at "^[ \t]*$")
        (tl-indent-line))
      (forward-line 1))))

;;; Imenu Support

(defvar tl-imenu-generic-expression
  '(("Functions" "^[ \t]*\\([a-zA-Z_][a-zA-Z0-9_]*\\)[ \t]*(" 1)
    ("Types" "^[ \t]*\\([A-Z][a-zA-Z0-9_]*\\)[ \t]*(" 1)
    ("Modules" "^[ \t]*#module[ \t]+\\([a-zA-Z_][a-zA-Z0-9_]*\\)" 1))
  "Imenu generic expression for TL mode.")

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

  ;; Indentation
  (setq-local indent-line-function #'tl-indent-line)
  (setq-local indent-region-function #'tl-indent-region)

  ;; Imenu
  (setq-local imenu-generic-expression tl-imenu-generic-expression)

  ;; Electric indentation
  (setq-local electric-indent-chars
              (append '(?\n ?\} ?\;) electric-indent-chars))

  ;; Beginning/end of defun
  (setq-local beginning-of-defun-function #'tl-beginning-of-defun)
  (setq-local end-of-defun-function #'tl-end-of-defun))

(defun tl-beginning-of-defun (&optional arg)
  "Move backward to the beginning of a function definition.
With ARG, do it that many times."
  (interactive "p")
  (let ((arg (or arg 1)))
    (if (< arg 0)
        (tl-end-of-defun (- arg))
      (dotimes (_ arg)
        (re-search-backward "^[ \t]*[a-zA-Z_][a-zA-Z0-9_]*[ \t]*(" nil 'move)))))

(defun tl-end-of-defun (&optional arg)
  "Move forward to the end of a function definition.
With ARG, do it that many times."
  (interactive "p")
  (let ((arg (or arg 1)))
    (if (< arg 0)
        (tl-beginning-of-defun (- arg))
      (dotimes (_ arg)
        (when (re-search-forward "^[ \t]*[a-zA-Z_][a-zA-Z0-9_]*[ \t]*(" nil 'move)
          (forward-char -1)
          (forward-sexp)
          (forward-sexp))))))

;;; File Association

;;;###autoload
(add-to-list 'auto-mode-alist '("\\.tl\\'" . tl-mode))

(provide 'tl-mode)

;;; tl-mode.el ends here
