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

(require 'cl-lib)

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

(defun paper--adjust-size (frame)
  (ignore frame)
  (dolist (buffer (buffer-list))
    (with-current-buffer buffer
      (when (and (eq major-mode 'paper-mode) (buffer-live-p buffer))
        (let* ((windows (get-buffer-window-list buffer 'nomini t)))
          (if (not windows)
              (paper--hide paper--id)
            (let* ((show-window (if (memq (selected-window) windows)
                                    (selected-window)
                                  (car windows)))
                   (hide-windows (remq show-window windows))
                   (show-frame (window-frame show-window)))
              (paper--move-to-x-or-pgtk-frame show-frame)
              (cl-destructuring-bind (left top right bottom)
                  (window-inside-pixel-edges show-window)
                (paper--show paper--id)
                (paper--resize paper--id left top
                               (- right left) (- bottom top)))
              (dolist (window hide-windows)
                (switch-to-prev-buffer window)))))))))

(defun paper-scroll-up ()
  (interactive)
  (paper--scroll paper--id 0.0 -0.1))
(defun paper-scroll-down ()
  (interactive)
  (paper--scroll paper--id 0.0 0.1))
(defun paper-scroll-left ()
  (interactive)
  (paper--scroll paper--id -0.1 0.0))
(defun paper-scroll-right ()
  (interactive)
  (paper--scroll paper--id 0.1 0.0))
(defun paper-scroll-window-up ()
  (interactive)
  (paper--scroll paper--id 0.0 -0.5))
(defun paper-scroll-window-down ()
  (interactive)
  (paper--scroll paper--id 0.0 0.5))
(defun paper-scroll-prev-page ()
  (interactive)
  (paper--scroll-pagewise paper--id -1))
(defun paper-scroll-next-page ()
  (interactive)
  (paper--scroll-pagewise paper--id 1))
(defun paper-zoom-in ()
  (interactive)
  (paper--zoom paper--id 1.1))
(defun paper-zoom-out ()
  (interactive)
  (paper--zoom paper--id (/ 1 1.1)))

(defvar paper-mode-map (let ((map (make-sparse-keymap)))
                         ;; TODO vanilla style bindings
                         (define-key map "-" #'paper-zoom-out)
                         (define-key map "=" #'paper-zoom-in)
                         map)
  "Keymap for `paper-mode'.")

;;;###autoload
(define-derived-mode paper-mode fundamental-mode "Paper"
  "Paper document viewing mode."
  (setq-local
   buffer-read-only t
   cursor-type nil
   paper--process (make-pipe-process :name "paper"
                                     :buffer (generate-new-buffer
                                              (format "* %s: pipe-process"
                                                      buffer-file-name))
                                     :filter 'webkit--filter
                                     :noquery t)
   paper--id (paper--new paper--process nil buffer-file-name nil))
  ;; don't waste rendering time below our frame with the raw PDF text
  (narrow-to-region (point-min) (point-min))
  (paper--adjust-size (selected-frame)))

(add-hook 'window-size-change-functions #'paper--adjust-size)
;; (add-hook 'delete-frame-functions #'webkit--delete-frame)

(provide 'paper)
;;; paper.el ends here
