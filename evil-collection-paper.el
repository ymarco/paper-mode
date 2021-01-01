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

    "k" #'paper-scroll-up
    "j" #'paper-scroll-down
    "h" #'paper-scroll-left
    "l" #'paper-scroll-right

    "K" #'paper-scroll-prev-page
    "J" #'paper-scroll-next-page

    "-" #'paper-zoom-out
    "=" #'paper-zoom-in

    "c" #'paper-center

    "gg" #'paper-goto-first-page
    "G" #'paper-goto-last-page

    "zt" #'paper-scroll-to-page-start
    "zb" #'paper-scroll-to-page-end

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
