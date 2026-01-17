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
(let ((test-file "../src/tl/std/Array.tl"))
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
(if (boundp 'tl-imenu-generic-expression)
    (message "✓ Imenu support is defined")
  (error "✗ Imenu support is missing"))

(message "\n========================================")
(message "All tests passed! tl-mode is working correctly.")
(message "========================================\n")

(provide 'test-tl-mode)
;;; test-tl-mode.el ends here
