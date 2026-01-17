;;; demo-imenu.el --- Demonstrate imenu flat list

(add-to-list 'load-path (file-name-directory load-file-name))
(require 'tl-mode)
(require 'imenu)

(let ((array-file (expand-file-name "../src/tl/std/Array.tl"
                                     (file-name-directory load-file-name))))
  (message "\n=== Imenu Demo: Flat List ===\n")
  (message "Opening Array.tl and building imenu index...\n")

  (with-temp-buffer
    (insert-file-contents array-file)
    (tl-mode)
    (let ((index (tl-imenu-create-index)))
      (message "Total items in flat list: %d\n" (length index))
      (message "First 20 items:")
      (dotimes (i (min 20 (length index)))
        (let ((item (nth i index)))
          (message "  %2d. %s" (1+ i) (car item))))
      (when (> (length index) 20)
        (message "  ... and %d more items" (- (length index) 20))))))

(message "\n=== Demo Complete ===\n")
(message "Note: All items appear in a single flat list,")
(message "      not grouped into Functions/Types/Modules categories.\n")
