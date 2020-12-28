#ifndef __PAPER_MODULE_H_
#define __PAPER_MODULE_H_

#include "PaperView.h"
#include "emacs-module.h"
#include <gtk/gtk.h>


emacs_value Qnil;
emacs_value Qargs_out_of_range;
emacs_value Qfset;
emacs_value Qprovide;


typedef struct Client {
  GtkWidget *container;
  /* PaperView *view; */
  GtkWidget *view;
  int fd;
} Client;


#endif // __PAPER_MODULE_H_
