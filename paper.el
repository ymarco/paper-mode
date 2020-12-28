;;; paper.el --- TODO -*- lexical-binding: t; -*-
;;
;; Copyright (C) 2020 Yoav Marco
;;
;; Author: Yoav Marco <https://github/ymarco>
;; Maintainer: Yoav Marco <yoavm448@gmail.com>
;; Created: December 05, 2020
;; Modified: December 05, 2020
;; Version: 0.0.1
;; Keywords:
;; Homepage: https://github.com/ymarco/paper-mode
;; Package-Requires: ((emacs 28.0.50) (cl-lib "0.5"))
;;
;; This file is not part of GNU Emacs.
;;
;;; Commentary:
;;
;;  TODO
;;
;;; Code:


(unless module-file-suffix
  (error "Paper needs module support.  Please compile Emacs with the --with-modules option!"))

(require 'paper-module)
;; (module-load (concat default-directory "paper-module.so"))


(defvar-local paper--id nil
  "User-pointer of the PaperView Client for the current buffer.")

(defvar-local paper--process nil
  "The pipe processes talking to PaperView Client stored in `paper--id'.")

(defun paper--move-to-x-or-pgtk-frame (frame)
  (let* ((ws (window-system frame))
         (err-msg "Cannot move webkit view to frame with window-system %S")
         (win-id (string-to-number (frame-parameter frame 'window-id)))
         (win-id (cond ((eq ws 'pgtk) win-id)
                       ((eq ws 'x) (webkit--xid-to-pointer win-id))
                       (t (error err-msg ws)))))
    (paper--move-to-frame paper--id win-id)))


;;;###autoload
(define-derived-mode paper-mode special-mode "Paper"
  "Paper document viewing mode."
  (setq-local
   paper--process (make-pipe-process :name "paper"
                                     :buffer (generate-new-buffer
                                              (format "* %s: pipe-process"
                                                      buffer-file-name))
                                     :filter 'webkit--filter
                                     :noquery t)
   paper--id (paper--new paper--process nil buffer-file-name nil))
  (paper--move-to-x-or-pgtk-frame (selected-frame)))

(provide 'paper)
;;; paper.el ends here
