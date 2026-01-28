;;; test-indentation.el --- Test indentation for tl-mode -*- lexical-binding: t; -*-

;;; Commentary:
;; This script tests indentation by re-indenting test-syntax.tl

;;; Code:

(add-to-list 'load-path (file-name-directory load-file-name))
(require 'tl-mode)

(let* ((dir (file-name-directory load-file-name))
       (test-file (expand-file-name "test-syntax.tl" dir))
       (output-file (expand-file-name "test-syntax-indented.tl" dir)))
  (message "Testing indentation on %s..." test-file)

  (with-temp-buffer
    (insert-file-contents test-file)
    (tl-mode)

    ;; Re-indent the entire buffer
    (indent-region (point-min) (point-max))

    ;; Write the indented version
    (write-region (point-min) (point-max) output-file)
    (message "✓ Indented file written to %s" output-file))

  ;; Compare the files
  (let ((original (with-temp-buffer
                    (insert-file-contents test-file)
                    (buffer-string)))
        (indented (with-temp-buffer
                    (insert-file-contents output-file)
                    (buffer-string))))
    (if (string= original indented)
        (message "✓ Indentation matches original (file was already correctly indented)")
      (message "! Indentation differs from original (see test-syntax-indented.tl for result)")))

  (message "\nIndentation test complete!"))

(provide 'test-indentation)
;;; test-indentation.el ends here
