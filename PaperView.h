#ifndef __MUPDF_GTK_H_
#define __MUPDF_GTK_H_
#include <gtk/gtk.h>
#include <mupdf/fitz.h>
#include <mupdf/pdf.h> /* for pdf specifics and forms */
#include <time.h>

typedef struct Quads {
  fz_quad *quads; // allocated on demand by complete_selection()
  int count;
} Quads;

typedef struct CachedQuads {
  Quads quads;
  // global DocInfo IDs and local ones are compared to check if the cache is
  // valid; DocInfo IDs change each time to invalidate all cached results.
  unsigned int id;
} CachedQuads;

typedef struct PageRenderCache {
  CachedQuads selection;
  CachedQuads search;
  fz_link *highlighted_link;
  struct CachedSurface {
    cairo_surface_t *surface;
    unsigned int id;
    char is_in_progress;
  } rendered;
} PageRenderCache;

typedef struct Page {
  fz_page *page;
  fz_stext_page *page_text;
  fz_rect page_bounds;
  fz_separations *seps;
  fz_link *links;
  fz_display_list *display_list;
  PageRenderCache cache;
} Page;

extern const int PAGE_SEPARATOR_HEIGHT;

#define PAGE_CACHE_LEN 32

typedef struct DocInfo {
  fz_document *doc;
  fz_location location;
  fz_outline *outline;
  pdf_document *pdf;
  pdf_annot *selected_annot;
  float zoom;   // 1.0 means no scaling
  float rotate; // in degrees
  unsigned int rendered_id;
  /* 0 <= scroll.y <= get_page(doci, doci.location).page_bounds.y1 +
   * PAGE_SEPARATOR_HEIGHT*/
  /* scroll is always relative to current page bounds */
  fz_point scroll;
  int chapter_count;
  // cache of pages implemented as a circular array with newly fetched pages at
  // the front
  struct PageCache {
    Page pages[PAGE_CACHE_LEN];
    fz_location locs[PAGE_CACHE_LEN];
    GThreadPool *render_pool;
    int first;
  } page_cache;
  struct Selection {
    gboolean is_in_progress;
    gboolean is_active;
    fz_point start;
    fz_point end;
    fz_location loc_start;
    fz_location loc_end;
    int mode; // FZ_SELECT_(CHARS|WORDS|LINES)
    unsigned int id;
  } selection;
  char filename[PATH_MAX];
  char accel[PATH_MAX];
  char search[PATH_MAX];
  unsigned int search_id;
  fz_colorspace *colorspace;
  fz_context *ctx;
  GMutex ctx_mutexes[FZ_LOCK_MAX];
  fz_locks_context locks_context;
} DocInfo;

typedef struct _PaperViewPrivate {
  DocInfo doci;
  GdkEventButton mouse_event;
  gboolean has_mouse_event;
  GdkCursor *default_cursor;
  GdkCursor *click_cursor;
} PaperViewPrivate;

typedef struct PaperView {
  GtkDrawingArea parent_instance;
} PaperView;

typedef struct PaperViewClass {
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
void scroll_to_page_start(GtkWidget *widget);
void scroll_to_page_end(GtkWidget *widget);
void fit_width(GtkWidget *widget);
void fit_height(GtkWidget *widget);
char *get_selection(GtkWidget *widget, size_t *res_len);
void unset_selection(GtkWidget *widget);
void set_search(GtkWidget *widget, char *needle);
void unset_search(GtkWidget *widget);
void zoom_relatively_around_point(GtkWidget *widget, float mult,
                                  fz_point point);

PaperView *paper_view_new(char *filename, char *accel_filename);

#endif // __MUPDF_GTK_H_
