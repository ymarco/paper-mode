#ifndef __MUPDF_GTK_H_
#define __MUPDF_GTK_H_
#include <gtk/gtk.h>
#include <mupdf/fitz.h>
#include <mupdf/pdf.h> /* for pdf specifics and forms */

typedef struct Page {
  fz_page *page;
  fz_stext_page *page_text;
  fz_matrix draw_page_ctm, // zoom, rotation, no screen x,y
      view_page_ctm,       // with screen x,y
      view_page_inv_ctm;
  fz_rect page_bounds;
  fz_location location;
  fz_separations *seps;
  fz_link *links;
  fz_display_list *display_list;
} Page;

typedef struct DocInfo {
  fz_document *doc;
  fz_location location;
  fz_outline *outline;
  pdf_document *pdf;
  pdf_annot *selected_annot;
  float zoom; // in precentage
  float rotate; // in degrees
  /* pages[location.chapter][location.page] = page */
  Page **pages;
  /* 0 <= scroll <= pages[location.chapter][location.page].page_bounds.y1 */
  float scroll_x;
  float scroll_y;
  int chapter_count;
  int *page_count_for_chapter;
  char filename[PATH_MAX];
  char accel[PATH_MAX];
  fz_colorspace *colorspace;
} DocInfo;

typedef struct Client {
  GtkWidget *container;
  DocInfo *doci;
  int fd;
  cairo_surface_t *image_surf;
  double mouse_event_x;
  double mouse_event_y;
  guint mouse_event_button;
  gboolean has_mouse_event;
} Client;

#endif // __MUPDF_GTK_H_
