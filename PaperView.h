#ifndef __MUPDF_GTK_H_
#define __MUPDF_GTK_H_
#include <gtk/gtk.h>
#include <mupdf/fitz.h>
#include <mupdf/pdf.h> /* for pdf specifics and forms */

typedef struct PageRenderCache {
  fz_quad *selection_quads; // allocated on demand by complete_selection()
  int selection_quads_count;
  fz_link *highlighted_link;
} PageRenderCache;

typedef struct Page {
  fz_page *page;
  fz_stext_page *page_text;
  fz_rect page_bounds;
  fz_separations *seps;
  fz_link *links;
  fz_display_list *display_list;
  fz_point selection_start;
  fz_point selection_end;
  PageRenderCache cache;
} Page;

extern const int PAGE_SEPARATOR_HEIGHT;

typedef struct DocInfo {
  fz_document *doc;
  fz_location location;
  fz_outline *outline;
  pdf_document *pdf;
  pdf_annot *selected_annot;
  float zoom;   // in precentage
  float rotate; // in degrees
  /* pages[location.chapter][location.page] = page */
  Page **pages;
  /* 0 <= scroll.y <= get_page(doci, doci.location).page_bounds.y1 +
   * PAGE_SEPARATOR_HEIGHT*/
  /* scroll is always relative to current page bounds */
  fz_point scroll;
  int chapter_count;
  int *page_count_for_chapter;
  gboolean selecting;
  gboolean selection_active;
  fz_location selection_loc_start;
  fz_location selection_loc_end;
  int selection_mode; // FZ_SELECT_(CHARS|WORDS|LINES)
  char filename[PATH_MAX];
  char accel[PATH_MAX];
  fz_colorspace *colorspace;
  fz_context *ctx;
} DocInfo;

typedef struct _PaperViewPrivate {
  DocInfo doci;
  cairo_surface_t *image_surf;
  GdkEventButton mouse_event;
  gboolean has_mouse_event;
  GdkCursor *default_cursor;
  GdkCursor *click_cursor;
} PaperViewPrivate;

typedef struct _PaperView {
  GtkDrawingArea parent_instance;
} PaperView;

typedef struct _PaperViewClass {
  GtkDrawingAreaClass parent_class;
} PaperViewClass;

GType paper_view_get_type(void) G_GNUC_CONST;

#define TYPE_PAPER_VIEW (paper_view_get_type())
#define PAPER_VIEW(obj)                                                        \
  (G_TYPE_CHECK_INSTANCE_CAST((obj), TYPE_PAPER_VIEW, PaperView))
#define PAPER_VIEW_CLASS(klass)                                                \
  (G_TYPE_CHECK_CLASS_CAST((klass), TYPE_PAPER_VIEW, PaperViewClass))
#define IS_PAPER_VIEW(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj), TYPE_PAPER_VIEW))
#define IS_PAPER_VIEW_CLASS(klass)                                             \
  (G_TYPE_CHECK_CLASS_TYPE((klass), TYPE_PAPER_VIEW))
#define PAPER_VIEW_GET_CLASS(obj)                                              \
  (G_TYPE_INSTANCE_GET_CLASS((obj), TYPE_PAPER_VIEW, PaperViewClass))

void scroll_relatively(GtkWidget *widget, fz_point mult);
void scroll_whole_pages(GtkWidget *widget, int i);
void zoom_to_window_center(GtkWidget *widget, float multiplier);
void center(GtkWidget *widget);
void goto_first_page(GtkWidget *widget);
void goto_last_page(GtkWidget *widget);

PaperView *paper_view_new(char *filename, char *accel_filename);

#endif // __MUPDF_GTK_H_
