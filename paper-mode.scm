(define-module (ymarco packages paper-mode-test)
  #:use-module ((guix licenses) #:prefix license:)
  #:use-module (gnu packages emacs-xyz)
  #:use-module (gnu packages gtk)
  #:use-module (gnu packages pdf)
  #:use-module (gnu packages pkg-config)
  #:use-module (guix build-system emacs)
  #:use-module (guix build-system gnu)
  #:use-module (guix git-download)
  #:use-module (guix gexp)
  #:use-module (guix packages)
  #:use-module (guix utils))

(package
 (name "emacs-paper-mode-test")
 (version "0.0.1")
 (build-system emacs-build-system)
 (source (local-file "." "paper-mode"
                     #:select? (git-predicate
                                (dirname (assoc-ref
                                          (current-source-location)
                                          'filename)))
                     #:recursive? #t))
 (inputs
  `(("mupdf" ,mupdf)
    ("gtk+" ,gtk+)))
 (native-inputs
  `(("pkg-config" ,pkg-config)))
 (propagated-inputs
  `(("emacs-evil-collection" ,emacs-evil-collection)))
 (arguments
  `(#:modules ((guix build emacs-build-system)
               ((guix build gnu-build-system) #:prefix gbs:)
               (guix build emacs-utils)
               (guix build utils))
    #:imported-modules (,@%emacs-build-system-modules
                        (guix build gnu-build-system))
    #:phases
    (modify-phases %standard-phases
        (add-before 'add-source-to-load-path 'substitute-paper-module-path
                    (lambda* (#:key outputs #:allow-other-keys)
                      (make-file-writable "paper.el")
                      (emacs-substitute-sexps "paper.el"
                                              ("(require 'paper-module)"
                                               `(module-load
                                                 ,(string-append (assoc-ref outputs "out")
                                                                 "/lib/paper-module.so"))))
                      ;; for some reason the above doesn't actually remove the (require ...)
                      (substitute* "paper.el" (("\\(require 'paper-module\\)")
                                               ""))
                      #t))
        (add-before 'install 'make
                    ;; Run make.
                    (lambda* (#:key (make-flags '()) outputs #:allow-other-keys)
                      ;; Compile the shared object file.
                      (apply invoke "make" "CC=gcc" ; TODO (cc-for-target)
                             make-flags)
                      ;; Move the file into /lib.
                      (install-file
                       "paper-module.so"
                       (string-append (assoc-ref outputs "out") "/lib"))
                      #t)))
    #:tests? #f))
 (synopsis "TODO")
 (description "TODO")
 (home-page "TODO")
 (license license:gpl3+))
