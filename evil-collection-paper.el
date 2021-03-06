;;; evil-collection-paper.el --- Evil bindings for Paper Mode -*- lexical-binding: t -*-

;; Copyright (C) 2020 Akira Kyle

;; Author: Yoav Marco <https://github/ymarco>
;; Maintainer: Yoav Marco <yoavm448@gmail.com>
;; Created: December 05, 2020
;; Modified: December 05, 2020
;; Version: 0.0.1
;; Keywords:
;; Homepage: https://github.com/ymarco/paper-mode
;; Package-Requires: ((emacs 28.0.50) (cl-lib "0.5"))

;; This file is free software; you can redistribute it and/or modify
;; it under the terms of the GNU General Public License as published
;; by the Free Software Foundation; either version 3, or (at your
;; option) any later version.
;;
;; This file is distributed in the hope that it will be useful,
;; but WITHOUT ANY WARRANTY; without even the implied warranty of
;; MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
;; GNU General Public License for more details.
;;
;; For a full copy of the GNU General Public License
;; see <http://www.gnu.org/licenses/>.

;;; Commentary:
;; Evil bindings for Paper Mode.

;;; Code:
(require 'paper)
(require 'evil-collection)

(defvar evil-collection-paper-maps '(paper-mode-map))


;;;###autoload
(defun evil-collection-paper-setup ()
  "Set up `evil' bindings for `paper'."
  (evil-collection-define-key 'normal 'paper-mode-map
    "q" 'quit-window

    [remap evil-previous-line] #'paper-scroll-up
    [remap evil-next-line] #'paper-scroll-down
    [remap evil-forward-char] #'paper-scroll-right
    [remap evil-backward-char] #'paper-scroll-left

    "K" #'paper-scroll-prev-page
    (kbd "C-k") #'paper-scroll-prev-page
    "J" #'paper-scroll-next-page
    (kbd "C-j") #'paper-scroll-next-page

    "-" #'paper-zoom-out
    [remap text-scale-decrease] #'paper-zoom-out
    "=" #'paper-zoom-in
    [remap text-scale-increase] #'paper-zoom-in

    "c" #'paper-center

    [remap evil-goto-first-line] #'paper-goto-first-page
    [remap evil-goto-line] #'paper-goto-last-page

    [remap evil-scroll-line-to-top] #'paper-scroll-to-page-start
    [remap evil-scroll-line-to-bottom] #'paper-scroll-to-page-end

    "W" #'paper-fit-width
    "H" #'paper-fit-height

    "y" #'paper-copy-selection

    "/" #'paper-search
    ;; TODO binding to ESC doesn't work
    ;; (kbd "ESC") #'paper-deselect

    "i" #'ignore
    "I" #'ignore
    "a" #'ignore
    "A" #'ignore)

  (when evil-want-C-d-scroll
    (evil-collection-define-key 'normal 'paper-mode-map
      (kbd "C-d") 'paper-scroll-window-down))
  (when evil-want-C-u-scroll
    (evil-collection-define-key 'normal 'paper-mode-map
      (kbd "C-u") 'paper-scroll-window-up)))

(provide 'evil-collection-paper)
