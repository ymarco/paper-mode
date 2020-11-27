#ifndef __MUPDF_GTK_H_
#define __MUPDF_GTK_H_
#include <gtk/gtk.h>
#include <mupdf/fitz.h>
#include <mupdf/pdf.h> /* for pdf specifics and forms */

typedef struct DocInfo {
  fz_document *doc;
  fz_location location;
  fz_separations *seps;
  fz_outline *outline;
  fz_link *links;
  pdf_document *pdf;
  pdf_annot *selected_annot;
  int page_count;
  fz_page *page;
  fz_stext_page *page_text;
  fz_matrix draw_page_ctm, // zoom, rotation, no screen x,y
      view_page_ctm,       // with screen x,y
      view_page_inv_ctm;
  fz_rect page_bounds, draw_page_bounds, view_page_bounds;
  float zoom, rotate;
  char filename[PATH_MAX];
  char accel[PATH_MAX];
  int needs_rerender;
  fz_colorspace *colorspace;
  cairo_surface_t *image_surf;
} DocInfo;

typedef struct Client {
  GtkWidget *container;
  GtkWidget *view;
  DocInfo *doci;
  int fd;
} Client;

#endif // __MUPDF_GTK_H_
