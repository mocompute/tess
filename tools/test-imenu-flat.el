;;; test-imenu-flat.el --- Test that imenu creates a flat list -*- lexical-binding: t; -*-

;;; Commentary:
;; This script verifies that the imenu index is a flat list rather than grouped.

;;; Code:

(add-to-list 'load-path (file-name-directory load-file-name))
(require 'tl-mode)
(require 'imenu)

(message "Testing imenu flat list structure...")

(with-temp-buffer
  (insert "#module TestModule\n")
  (insert "\n")
  (insert "// Function\n")
  (insert "foo() { 0 }\n")
  (insert "\n")
  (insert "// Type constructor\n")
  (insert "Point(a) : { x: a, y: a }\n")
  (insert "\n")
  (insert "// Another function\n")
  (insert "bar(x: Int) -> Int { x + 1 }\n")

  (tl-mode)
  (let ((index (tl-imenu-create-index)))
    (message "Index has %d items:" (length index))
    (dolist (item index)
      (message "  - %s (at position %d)" (car item) (cdr item)))

    ;; Verify it's a flat list (no nested lists)
    (let ((is-flat t))
      (dolist (item index)
        (when (listp (cdr item))
          ;; Check if cdr is a list of more items (nested structure)
          (when (and (consp (cdr item))
                     (consp (cadr item)))
            (setq is-flat nil))))

      (if is-flat
          (message "\n✓ Imenu structure is flat (not grouped)")
        (message "\n✗ Imenu structure is nested/grouped")))

    ;; Verify expected items are present
    (let ((names (mapcar #'car index)))
      (if (and (member "#module TestModule" names)
               (member "foo" names)
               (member "Point" names)
               (member "bar" names))
          (message "✓ All expected items found in flat list")
        (message "✗ Some items missing. Found: %s" names)))))

(message "\nImenu flat list test complete!")

(provide 'test-imenu-flat)
;;; test-imenu-flat.el ends here
