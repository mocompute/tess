;;; test-tl-mode.el --- Test script for tl-mode -*- lexical-binding: t; -*-

;;; Commentary:
;; This script tests the tl-mode by loading it and checking basic functionality.

;;; Code:

(add-to-list 'load-path (file-name-directory load-file-name))
(require 'tl-mode)

;; Test 1: Mode loads successfully
(message "Test 1: Checking if tl-mode is defined...")
(if (fboundp 'tl-mode)
    (message "✓ tl-mode is defined")
  (error "✗ tl-mode is not defined"))

;; Test 2: File association is set up
(message "Test 2: Checking .tl file association...")
(if (assoc "\\.tl\\'" auto-mode-alist)
    (message "✓ .tl files are associated with tl-mode")
  (error "✗ .tl file association not found"))

;; Test 3: Syntax table is defined
(message "Test 3: Checking syntax table...")
(if (boundp 'tl-mode-syntax-table)
    (message "✓ Syntax table is defined")
  (error "✗ Syntax table is not defined"))

;; Test 4: Font-lock keywords are defined
(message "Test 4: Checking font-lock keywords...")
(if (boundp 'tl-font-lock-keywords)
    (message "✓ Font-lock keywords are defined")
  (error "✗ Font-lock keywords are not defined"))

;; Test 5: Indentation functions are defined
(message "Test 5: Checking indentation functions...")
(if (fboundp 'tl-indent-line)
    (message "✓ Indentation functions are defined")
  (error "✗ Indentation functions are not defined"))

;; Test 6: Load a TL file and enable the mode
(message "Test 6: Testing mode activation on a TL file...")
(let ((test-file "../../src/tl/std/Array.tl"))
  (when (file-exists-p test-file)
    (with-temp-buffer
      (insert-file-contents test-file)
      (tl-mode)
      (if (eq major-mode 'tl-mode)
          (message "✓ tl-mode activates successfully")
        (error "✗ tl-mode failed to activate")))
    (message "✓ Mode works with Array.tl")))

;; Test 7: Test indentation calculation function exists
(message "Test 7: Checking indentation calculation...")
(if (fboundp 'tl-calculate-indentation)
    (message "✓ Indentation calculation function is defined")
  (error "✗ Indentation calculation function is missing"))

;; Test 8: Test imenu
(message "Test 8: Checking imenu support...")
(if (fboundp 'tl-imenu-create-index)
    (message "✓ Imenu support is defined")
  (error "✗ Imenu support is missing"))

;; Test 9: Font-lock for type arguments in brackets
(message "Test 9: Checking font-lock for type arguments...")

(defun tl-test-get-faces (text)
  "Return alist of (substring . face) for TEXT after applying tl-mode font-lock."
  (with-temp-buffer
    (insert text)
    (tl-mode)
    (font-lock-ensure)
    (let ((result nil)
          (pos 1))
      (while (<= pos (point-max))
        (let ((face (get-text-property pos 'face)))
          (push (cons (buffer-substring-no-properties pos (1+ pos)) face) result))
        (setq pos (1+ pos)))
      (nreverse result))))

(defun tl-test-check-type-face (text type-name)
  "Check that TYPE-NAME has font-lock-type-face in TEXT."
  (with-temp-buffer
    (insert text)
    (tl-mode)
    (font-lock-ensure)
    (goto-char (point-min))
    (if (search-forward type-name nil t)
        (let ((face (get-text-property (match-beginning 0) 'face)))
          (if (eq face 'font-lock-type-face)
              (message "  ✓ %s is highlighted as type" type-name)
            (error "  ✗ %s not highlighted as type (got %s)" type-name face)))
      (error "  ✗ %s not found in text" type-name))))

;; Test simple case with lowercase type params: [xx, yy]
(message "  Testing func[xx, yy](v: xx)...")
(let ((code "func[xx, yy](v: xx) {"))
  (tl-test-check-type-face code "xx")
  (tl-test-check-type-face code "yy"))

;; Test nested type arguments - mixed case, all unique names
(message "  Testing nested [aa, Option[bb], Result[cc, dd]]...")
(let ((code "func2[aa, Option[bb], Result[cc, dd]]()"))
  (tl-test-check-type-face code "aa")
  (tl-test-check-type-face code "Option")
  (tl-test-check-type-face code "bb")
  (tl-test-check-type-face code "Result")
  (tl-test-check-type-face code "cc")
  (tl-test-check-type-face code "dd"))

(message "\n========================================")
(message "All tests passed! tl-mode is working correctly.")
(message "========================================\n")

(provide 'test-tl-mode)
;;; test-tl-mode.el ends here
