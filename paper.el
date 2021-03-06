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
         (err-msg "Cannot move paper view to frame with window-system %S")
         (win-id (string-to-number (frame-parameter frame 'window-id)))
         (win-id (cond ((eq ws 'pgtk) win-id)
                       ((eq ws 'x) (paper--xid-to-pointer win-id))
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

(defun paper--kill-buffer ()
  (let* ((process paper--process)
         (buffer (process-buffer process)))
    (paper--hide paper--id)
    (paper--destroy paper--id)
    (delete-process process)
    (kill-buffer buffer)))

(defun paper--delete-frame (frame)
  (let ((new-frame (cl-some
                    (lambda (elt)
                      (not (or (eq elt frame)
                               (frame-parameter elt 'parent-frame)
                               (not (display-graphic-p elt)))))
                    (frame-list))))
    (dolist (buffer (buffer-list))
      (when (eq major-mode 'paper-mode)
        (with-current-buffer buffer
          (paper--move-to-x-or-pgtk-frame new-frame))))))

(defmacro paper--bind-id (new-name mod-func-name &rest args)
  `(defun ,new-name ()
     (interactive)
     (,mod-func-name paper--id ,@args)))

(paper--bind-id paper-scroll-up
                paper--scroll 0.0 -0.1)
(paper--bind-id paper-scroll-down
                paper--scroll 0.0 0.1)
(paper--bind-id paper-scroll-left
                paper--scroll -0.1 0.0)
(paper--bind-id paper-scroll-right
                paper--scroll 0.1 0.0)
(paper--bind-id paper-scroll-window-up
                paper--scroll 0.0 -0.5)
(paper--bind-id paper-scroll-window-down
                paper--scroll 0.0 0.5)
(paper--bind-id paper-scroll-prev-page
                paper--scroll-pagewise -1)
(paper--bind-id paper-scroll-next-page
                paper--scroll-pagewise 1)
(paper--bind-id paper-zoom-in
                paper--zoom 1.1)
(paper--bind-id paper-zoom-out
                paper--zoom (/ 1 1.1))
(defmacro paper--bind-same (prefix)
  `(paper--bind-id ,(intern (concat "paper-" (symbol-name prefix)))
                   ,(intern (concat "paper--" (symbol-name prefix)))))
(paper--bind-same center)
(paper--bind-same goto-first-page)
(paper--bind-same goto-last-page)
(paper--bind-same scroll-to-page-start)
(paper--bind-same scroll-to-page-end)
(paper--bind-same fit-height)
(paper--bind-same fit-width)

(defun paper-copy-selection ()
  (interactive)
  (kill-new (paper--get-selection paper--id))
  (let ((message-log-max nil))
    (message "Copied!")))

(defun paper-search (needle)
  (interactive "Msearch: ")
  (paper--set-search paper--id needle))

(defun paper-deselect ()
  (interactive)
  (paper--unset-selection paper--id)
  (paper--unset-search paper--id))

(defun paper-mwheel-scroll (button scroll-window)
  "Scroll up or down in SCROLL-WINDOW according to the BUTTON.

Similar to `mwheel-scroll' but for paper-mode."
  (interactive (list (mwheel-event-button last-input-event)
                     (mouse-wheel--get-scroll-window last-input-event)))
  (let ((original-window (selected-window)))
    (select-window scroll-window)
    (unwind-protect
        (paper--scroll paper--id
                       0.0      ; x
                       ;; emacs seems to miss some of the scroll events
                       ;; so zoom by a little more than the default 0.1
                       (cond
                        ((eq button mouse-wheel-down-event) -0.2)
                        ((eq button mouse-wheel-up-event) 0.2)
                        (t 0)))
      (select-window original-window))))

(defun paper-mouse-wheel-text-scale (e)
  "Increase or decrease zoom level in SCROLL-WINDOW according to the BUTTON.

Similar to `mouse-wheel-text-scale' but for paper-mode."
  (interactive "e")
  (let* ((original-window (selected-window))
         (scroll-posn (event-start e))
         (scroll-window (posn-window scroll-posn))
         (scroll-x-y (posn-x-y scroll-posn))
         (scroll-x (float (car scroll-x-y)))
         (scroll-y (float (cdr scroll-x-y)))
         (button (mwheel-event-button last-input-event)))
    (select-window scroll-window)
    (unwind-protect
        ;; emacs seems to miss some of the scroll events
        ;; so zoom by a little more than the default 1.1
        (paper--zoom-around-point paper--id
                                  (cond
                                   ((eq button mouse-wheel-down-event) 1.2)
                                   ((eq button mouse-wheel-up-event) (/ 1 1.2))
                                   (t 1.0))
                                  scroll-x scroll-y)
      (select-window original-window))))

(defvar paper-mode-map
  (let ((map (make-sparse-keymap)))
    ;; TODO have someone who uses vanilla-style bindings do it
    (define-key map "-" #'paper-zoom-out)
    (define-key map [remap text-scale-decrease] #'paper-zoom-out)
    (define-key map "=" #'paper-zoom-in)
    (define-key map [remap text-scale-increase] #'paper-zoom-in)
    (define-key map [remap next-line] #'paper-scroll-down)
    (define-key map [remap previous-line] #'paper-scroll-up)
    (define-key map [remap right-char] #'paper-scroll-right)
    (define-key map [remap left-char] #'paper-scroll-left)
    (define-key map [remap scroll-up-command] #'paper-scroll-window-up)
    (define-key map [remap scroll-down-command] #'paper-scroll-window-down)
    (define-key map [remap mwheel-scroll] #'paper-mwheel-scroll)
    (define-key map [remap mouse-wheel-text-scale] #'paper-mouse-wheel-text-scale)
    map)
  "Keymap for `paper-mode'.")

;;;###autoload
(define-derived-mode paper-mode fundamental-mode "Paper"
  "Paper document viewing mode."
  (setq-local
   buffer-read-only t
   cursor-type nil
   left-margin-width nil
   right-margin-width nil
   left-fringe-width 0
   right-fringe-width 0
   vertical-scroll-bar nil
   paper--process (make-pipe-process :name "paper"
                                     :buffer (generate-new-buffer
                                              (format "* %s: pipe-process"
                                                      buffer-file-name))
                                     ;; :filter #'paper--filter
                                     :noquery t)
   paper--id (paper--new paper--process nil buffer-file-name nil))
  ;; don't waste rendering time below our frame with the raw PDF text
  (add-hook 'kill-buffer-hook #'paper--kill-buffer nil t)
  (narrow-to-region (point-min) (point-min))
  (paper--adjust-size (selected-frame)))

(add-hook 'window-size-change-functions #'paper--adjust-size)
(add-hook 'delete-frame-functions #'paper--delete-frame)

(provide 'paper)
;;; paper.el ends here
