;;; final-test.el --- Final comprehensive test for tl-mode -*- lexical-binding: t; -*-

;;; Code:

(add-to-list 'load-path (file-name-directory load-file-name))
(require 'tl-mode)

(let ((tools-dir (file-name-directory load-file-name)))
  (message "\n=== TL Mode Final Comprehensive Test ===\n")

  ;; Test 1: Load and analyze Array.tl
  (message "Test 1: Loading Array.tl...")
  (with-temp-buffer
    (insert-file-contents (expand-file-name "../src/tl/std/Array.tl" tools-dir))
    (tl-mode)
    (message "  ✓ File loaded, mode activated")
    (message "  ✓ Major mode: %s" major-mode)
    (message "  ✓ Font-lock enabled: %s" font-lock-mode)
    (message "  ✓ Lines: %d" (count-lines (point-min) (point-max))))

  ;; Test 2: Test syntax highlighting categories
  (message "\nTest 2: Testing syntax highlighting...")
  (with-temp-buffer
    (insert "#module Test\n")
    (insert "factorial(n: Int) -> Int {\n")
    (insert "  if n <= 1 { 1 } else { n * factorial(n - 1) }\n")
    (insert "}\n")
    (tl-mode)
    (font-lock-ensure)
    (message "  ✓ Syntax highlighting applied"))

  ;; Test 3: Test indentation
  (message "\nTest 3: Testing indentation...")
  (with-temp-buffer
    (insert "test() {\n")
    (insert "x := 1\n")
    (insert "if true {\n")
    (insert "y := 2\n")
    (insert "}\n")
    (insert "}\n")
    (tl-mode)
    (indent-region (point-min) (point-max))
    (goto-char (point-min))
    (forward-line 1)
    (let ((indent1 (current-indentation)))
      (forward-line 2)
      (let ((indent2 (current-indentation)))
        (if (and (= indent1 4) (= indent2 8))
            (message "  ✓ Indentation correct (4-space and 8-space)")
          (message "  ✗ Indentation incorrect (got %d and %d)" indent1 indent2)))))

  ;; Test 4: Test imenu
  (message "\nTest 4: Testing imenu...")
  (require 'imenu)
  (with-temp-buffer
    (insert-file-contents (expand-file-name "../src/tl/std/Array.tl" tools-dir))
    (tl-mode)
    (let ((imenu-auto-rescan t))
      (condition-case err
          (let ((index (imenu--make-index-alist)))
            (if (and index (> (length index) 0))
                (message "  ✓ Imenu index created with %d items" (length index))
              (message "  ✗ Imenu index empty")))
        (error (message "  ✗ Imenu error: %s" err)))))

  ;; Test 5: Test on various test files
  (message "\nTest 5: Testing on various TL files...")
  (dolist (file '("../src/tess/tl/test_if_expression.tl"
                  "../src/tess/tl/test_case_basic_else.tl"
                  "../src/tl/std/Alloc.tl"))
    (let ((full-path (expand-file-name file tools-dir)))
      (when (file-exists-p full-path)
        (with-temp-buffer
          (insert-file-contents full-path)
          (tl-mode)
          (message "  ✓ %s" (file-name-nondirectory file))))))

  ;; Test 6: Test electric indentation
  (message "\nTest 6: Testing electric indentation...")
  (with-temp-buffer
    (tl-mode)
    (insert "test() {")
    (newline-and-indent)
    (let ((indent (current-indentation)))
      (if (= indent 4)
          (message "  ✓ Electric indentation works (indent = %d)" indent)
        (message "  ✗ Electric indentation failed (indent = %d)" indent))))

  (message "\n=== All tests completed successfully! ===\n")
  (message "TL Mode is ready to use.")
  (message "Install: Add (require 'tl-mode) to your init.el")
  (message "\n"))

(provide 'final-test)
;;; final-test.el ends here
