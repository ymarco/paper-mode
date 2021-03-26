#include <math.h>
#include <mupdf/fitz.h>
#include <mupdf/pdf.h> /* for pdf specifics and forms */
#include <mupdf/ucdn.h>

#include "PaperView.h"
#include <gtk/gtk.h>

const int PAGE_SEPARATOR_HEIGHT = 18;

G_DEFINE_TYPE_WITH_PRIVATE(PaperView, paper_view, GTK_TYPE_DRAWING_AREA);

int locationcmp(fz_location a, fz_location b) {
  int chapcmp = a.chapter - b.chapter;
  return chapcmp != 0 ? chapcmp : a.page - b.page;
}

void drop_page(fz_context *ctx, Page *page) {
  if (!page)
    return;
  fz_drop_page(ctx, page->page);
  fz_drop_stext_page(ctx, page->page_text);
  fz_drop_separations(ctx, page->seps);
  fz_drop_link(ctx, page->links);
  fz_drop_display_list(ctx, page->display_list);
  free(page->cache.selection.quads.quads);
  free(page->cache.search.quads.quads);
}

void load_page(DocInfo *doci, fz_location location, Page *page) {
  fz_context *ctx = doci->ctx;
  memset(page, 0, sizeof(*page));
  fz_try(ctx) {
    page->page =
        fz_load_chapter_page(ctx, doci->doc, location.chapter, location.page);
    page->seps = NULL; // TODO seps
    page->links = fz_load_links(ctx, page->page);
    page->page_bounds = fz_bound_page(ctx, page->page);
    page->display_list = fz_new_display_list(ctx, page->page_bounds);
    // populate display_list
    fz_device *device = fz_new_list_device(ctx, page->display_list);
    fz_try(ctx) { fz_run_page(ctx, page->page, device, fz_identity, NULL); }
    fz_always(ctx) {
      fz_close_device(ctx, device);
      fz_drop_device(ctx, device);
    }
    fz_catch(ctx) { fz_rethrow(ctx); }
    page->page_text =
        fz_new_stext_page_from_display_list(ctx, page->display_list, NULL);
  }
  fz_catch(ctx) {
    fprintf(stderr, "error loading page %d,%d: %s\n", location.chapter,
            location.page, fz_caught_message(ctx));
  }
  PageRenderCache *cache = &page->cache;
  cache->selection.id = 0;
  cache->search.id = 0;
}

int prev_ind_in_page_cache(int i) {
  return (i + PAGE_CACHE_LEN - 1) % PAGE_CACHE_LEN;
}

/*
 * Return a pointer to a Page object at location LOC. The pointer might be
 * invalidated as soon a new page is fetched via get_page (like strtok).
 */
Page *get_page(DocInfo *doci, fz_location loc) {
  for (int i = 0; i < PAGE_CACHE_LEN; i++) {
    if (locationcmp(loc, doci->page_cache.locs[i]) == 0) {
      return &doci->page_cache.pages[i];
    }
  }
  int new_first = prev_ind_in_page_cache(doci->page_cache.first);
  Page *res = &doci->page_cache.pages[new_first];
  drop_page(doci->ctx, res);
  load_page(doci, loc, res);
  doci->page_cache.locs[new_first] = loc;
  doci->page_cache.first = new_first;
  return res;
}

Page *get_cur_page(DocInfo *doci) { return get_page(doci, doci->location); }

fz_matrix get_scale_ctm(DocInfo *doci, Page *page) {
  return fz_transform_page(page->page_bounds, 72.0f * doci->zoom, doci->rotate);
}

/*
 * Get the position of POINT whithin the boundries of the current or next pages.
 */
static void trace_point_to_page(GtkWidget *widget, DocInfo *doci,
                                fz_point point, fz_point *res,
                                fz_location *loc) {
  *loc = doci->location;
  fz_context *ctx = doci->ctx;
  fz_point stopped = fz_make_point(-doci->scroll.x, -doci->scroll.y);
  Page *page = get_page(doci, *loc);
  while (1) {
    fz_matrix scale_ctm = get_scale_ctm(doci, page);
    fz_matrix draw_page_ctm =
        fz_concat(fz_translate(stopped.x, stopped.y), scale_ctm);
    fz_matrix draw_page_inv = fz_invert_matrix(draw_page_ctm);
    *res = fz_transform_point(point, draw_page_inv);
    stopped.y += page->page_bounds.y1 + PAGE_SEPARATOR_HEIGHT;
    page = get_page(doci, *loc);
    if (res->y < page->page_bounds.y1 + PAGE_SEPARATOR_HEIGHT) {
      break;
    }
    *loc = fz_next_page(ctx, doci->doc, *loc);
  };
}

/*
 * Set scroll.x so the current page is centered.
 */
static void center_page(int surface_width, DocInfo *doci) {
  Page *page = get_cur_page(doci);
  fz_matrix scale_ctm = get_scale_ctm(doci, page);
  fz_rect scaled_bounds = fz_transform_rect(page->page_bounds, scale_ctm);
  fz_matrix scale_ctm_inv = fz_invert_matrix(scale_ctm);
  fz_point centered_page_start = fz_transform_point(
      fz_make_point(((float)scaled_bounds.x1 - surface_width) / 2, 0),
      scale_ctm_inv);

  doci->scroll.x = centered_page_start.x;
}

static void highlight_quads(fz_context *ctx, Quads *quads, fz_pixmap *pixmap,
                            fz_matrix ctm) {
  for (int i = 0; i < quads->count; i++) {
    fz_quad box = fz_transform_quad(quads->quads[i], ctm);
    fz_clear_pixmap_rect_with_value(
        ctx, pixmap, 0xE8,
        fz_round_rect(fz_make_rect(box.ul.x, box.ul.y, box.lr.x, box.lr.y)));
    // TODO actually fill in the quad instead of assuming its a rectangle
  }
}

void get_selection_bounds_for_page(fz_context *ctx, DocInfo *doci,
                                   fz_location loc, fz_point *res_start,
                                   fz_point *res_end) {
  Page *page = get_page(doci, loc);
  if (locationcmp(doci->selection.loc_start, loc) > 0 ||
      locationcmp(doci->selection.loc_end, loc) < 0) { // out of bounds
    page->cache.selection.quads.count = 0;
    return;
  }
  if (locationcmp(doci->selection.loc_start, loc) < 0) {
    res_start->x = page->page_bounds.x0;
    res_start->y = page->page_bounds.y0;
  } else { // loc == loc_start
    *res_start = doci->selection.start;
  }
  if (locationcmp(loc, doci->selection.loc_end) != 0) {
    res_end->x = page->page_bounds.x1;
    res_end->y = page->page_bounds.y1;
  } else { // loc == loc_start
    *res_end = doci->selection.end;
  }
  fz_snap_selection(ctx, page->page_text, res_start, res_end,
                    doci->selection.mode);
}

/*
 * Return a string of the text selection spanning on all the pages with
 * selection. It's your responsibility to free the returned pointer. *res_len is
 * set to the strlen of the return value.
 * Return NULL if no selection is available.
 */
char *get_selection(GtkWidget *widget, size_t *res_len) {
  PaperViewPrivate *c = paper_view_get_instance_private(PAPER_VIEW(widget));
  if (!c->doci.selection.is_active)
    return NULL;
  char *res = NULL;
  // oh, what would I do to use open_memstream. Not standardized though.
  size_t len = 0;
  size_t size = 0;
  for (fz_location loc = c->doci.selection.loc_start;
       locationcmp(loc, c->doci.selection.loc_end) <= 0;
       loc = fz_next_page(c->doci.ctx, c->doci.doc, loc)) {
    Page *page = get_page(&c->doci, loc);
    fz_point sel_start, sel_end;
    get_selection_bounds_for_page(c->doci.ctx, &c->doci, loc, &sel_start,
                                  &sel_end);
    char *page_sel = fz_copy_selection(c->doci.ctx, page->page_text, sel_start,
                                       sel_start, 0);
    size_t n = strlen(page_sel);
    size_t new_len = n + len;
    if (new_len > size) {
      size = fz_maxi(new_len + 1, size * 2);
      res = realloc(res, size);
      if (!res)
        return NULL;
    }
    memcpy(&res[len], page_sel, n);
    len = new_len;
    fz_free(c->doci.ctx, page_sel);
  }
  if (res)
    res[len] = '\0';
  *res_len = len;
  return res;
}

void ensure_selection_cache_is_updated(fz_context *ctx, DocInfo *doci,
                                       fz_location loc) {
  Page *page = get_page(doci, loc);
  if (page->cache.selection.id == doci->selection.id)
    return;
  fz_point sel_start, sel_end;
  get_selection_bounds_for_page(ctx, doci, loc, &sel_start, &sel_end);
  page->cache.selection.id = doci->selection.id;
  int max_count = 256;
  int count;
  do {
    page->cache.selection.quads.quads =
        realloc(page->cache.selection.quads.quads, max_count * sizeof(fz_quad));
    count =
        fz_highlight_selection(ctx, page->page_text, sel_start, sel_end,
                               page->cache.selection.quads.quads, max_count);
    max_count *= 2;
  } while (count == max_count);
  page->cache.selection.quads.count = count;
}

void ensure_search_cache_is_updated(fz_context *ctx, DocInfo *doci, Page *page,
                                    char *search) {
  if (page->cache.search.id == doci->search_id)
    return;
  page->cache.search.id = doci->search_id;
  int max_count = 256;
  int count;
  do {
    page->cache.search.quads.quads =
        realloc(page->cache.search.quads.quads, max_count * sizeof(fz_quad));
    count = fz_search_stext_page(ctx, page->page_text, search,
                                 page->cache.search.quads.quads, max_count);
    max_count *= 2;
  } while (count == max_count);
  page->cache.search.quads.count = count;
}

static void allocate_pixmap(GtkWidget *widget, GdkRectangle *allocation) {
  PaperViewPrivate *c = paper_view_get_instance_private(PAPER_VIEW(widget));
  // allocate only if dimensions changed
  if ((!c->image_surf) ||
      cairo_image_surface_get_width(c->image_surf) != allocation->width ||
      cairo_image_surface_get_height(c->image_surf) != allocation->height) {
    cairo_surface_destroy(c->image_surf);
    c->image_surf = cairo_image_surface_create(
        CAIRO_FORMAT_RGB24, allocation->width, allocation->height);
  }
}

gboolean draw_callback(GtkWidget *widget, cairo_t *cr) {
  PaperViewPrivate *c = paper_view_get_instance_private(PAPER_VIEW(widget));
  fz_context *ctx = c->doci.ctx;
  GdkRectangle rec = {.width = gtk_widget_get_allocated_width(widget),
                      .height = gtk_widget_get_allocated_height(widget)};
  allocate_pixmap(widget, &rec);

  cairo_surface_t *surface = c->image_surf;

  unsigned int width = cairo_image_surface_get_width(surface);
  unsigned int height = cairo_image_surface_get_height(surface);

  unsigned char *image = cairo_image_surface_get_data(surface);

  fz_irect whole_rect = {.x1 = width, .y1 = height};
  fz_rect float_rect = {.x1 = width, .y1 = height};

  fz_pixmap *pixmap = NULL;
  fz_device *draw_device = NULL;
  fz_try(ctx) {
    pixmap = fz_new_pixmap_with_bbox_and_data(ctx, c->doci.colorspace,
                                              whole_rect, NULL, 1, image);
    fz_clear_pixmap_with_value(ctx, pixmap, 0xF0);
    draw_device = fz_new_draw_device(ctx, fz_identity, pixmap);
  }
  fz_catch(ctx) {
    fprintf(stderr, "Failed allocations: %s\n", fz_caught_message(ctx));
    return FALSE;
  }
  // background

  fz_location loc = c->doci.location;
  Page *page = get_page(&c->doci, loc);
  fz_matrix scale_ctm = get_scale_ctm(&c->doci, page);
  fz_matrix draw_page_ctm;
  fz_point stopped = fz_make_point(-c->doci.scroll.x, -c->doci.scroll.y);

  while (fz_transform_point(stopped, scale_ctm).y < height) {
    scale_ctm = get_scale_ctm(&c->doci, page);
    draw_page_ctm = fz_concat(fz_translate(stopped.x, stopped.y), scale_ctm);
    // foreground around page boundry
    fz_rect transformed_bounds =
        fz_transform_rect(page->page_bounds, draw_page_ctm);
    fz_clear_pixmap_rect_with_value(ctx, pixmap, 0xFF,
                                    fz_round_rect(transformed_bounds));
    // highlight text selection
    if ((c->doci.selection.is_active || c->doci.selection.is_in_progress) &&
        locationcmp(loc, c->doci.selection.loc_end) <= 0) {
      ensure_selection_cache_is_updated(ctx, &c->doci, loc);
      highlight_quads(ctx, &page->cache.selection.quads, pixmap, draw_page_ctm);
    }
    // highlight search results
    if (c->doci.search[0]) {
      ensure_search_cache_is_updated(ctx, &c->doci, page, c->doci.search);
      highlight_quads(ctx, &page->cache.search.quads, pixmap, draw_page_ctm);
    }
    // highlight selected link
    if (page->cache.highlighted_link) {
      fz_clear_pixmap_rect_with_value(
          ctx, pixmap, 0xD0,
          fz_round_rect(fz_transform_rect(page->cache.highlighted_link->rect,
                                          draw_page_ctm)));
    }
    fz_try(ctx) {
      fz_run_display_list(ctx, page->display_list, draw_device, draw_page_ctm,
                          float_rect, NULL);
    }
    fz_catch(ctx) {
      fprintf(stderr, "couldn't render page %d,%d: %s\n", loc.chapter, loc.page,
              fz_caught_message(ctx));
    }
    /* fprintf(stderr, "\rscroll: %3.0f %3.0f, stopped.y: %3.0f", */
    /*         c->doci.scroll.x, c->doci->scroll.y, stopped.y); */
    stopped.y += page->page_bounds.y1 + PAGE_SEPARATOR_HEIGHT;
    fz_location next = fz_next_page(ctx, c->doci.doc, loc);
    if (next.chapter == loc.chapter && next.page == loc.page) {
      // end of document
      break;
    }
    loc = next;
    page = get_page(&c->doci, loc);
  }

  fz_close_device(ctx, draw_device);
  fz_drop_device(ctx, draw_device);
  fz_drop_pixmap(ctx, pixmap);
  cairo_set_source_surface(cr, surface, 0, 0);
  cairo_paint(cr);
  return FALSE;
}

static gboolean button_press_event(GtkWidget *widget, GdkEventButton *event) {
  PaperViewPrivate *c = paper_view_get_instance_private(PAPER_VIEW(widget));
  switch (event->button) {
  case GDK_BUTTON_PRIMARY:
    c->doci.selection.is_in_progress = TRUE;
    fz_point orig_point;
    trace_point_to_page(widget, &c->doci, fz_make_point(event->x, event->y),
                        &orig_point, &c->doci.selection.loc_start);
    c->doci.selection.start = orig_point;
    switch (event->type) {
    case GDK_BUTTON_PRESS:
      c->doci.selection.mode = FZ_SELECT_CHARS;
      break;
    case GDK_2BUTTON_PRESS:
      c->doci.selection.mode = FZ_SELECT_WORDS;
      break;
    case GDK_3BUTTON_PRESS:
      c->doci.selection.mode = FZ_SELECT_LINES;
      break;
    default:
      fprintf(stderr, "Unhandled button press type\n");
    }
    break;
  case GDK_BUTTON_MIDDLE:;
    int width = gtk_widget_get_allocated_width(widget);
    center_page(width, &c->doci);
    gtk_widget_queue_draw(widget);
    // TODO smooth scrolling like evince has
    break;
  }
  return FALSE;
}

// TODO actually run this on realize
static gboolean configure_event(GtkWidget *widget, GdkEventConfigure *event) {
  PaperViewPrivate *c = paper_view_get_instance_private(PAPER_VIEW(widget));
  GdkDisplay *display = gtk_widget_get_display(widget);
  c->default_cursor = gdk_cursor_new_from_name(display, "default");
  c->click_cursor = gdk_cursor_new_from_name(display, "pointer");
  return FALSE;
}

/*
 * Update cache.highlighted_link on the page below mouse_point.
 * Return TRUE if the link was updated.
 * Also set the gdk mouse cursor in the correct shape.
 */
static gboolean update_highlighted_link(GtkWidget *widget,
                                        fz_point mouse_point) {
  PaperViewPrivate *c = paper_view_get_instance_private(PAPER_VIEW(widget));
  GdkCursor *cursor = c->default_cursor;
  fz_point mouse_page_point;
  fz_location mouse_page_loc;
  trace_point_to_page(widget, &c->doci, mouse_point, &mouse_page_point,
                      &mouse_page_loc);
  Page *page = get_page(&c->doci, mouse_page_loc);
  // skip altogether if point stayed in the same link area as before
  if (page->cache.highlighted_link &&
      fz_is_point_inside_rect(mouse_page_point,
                              page->cache.highlighted_link->rect))
    return FALSE;
  fz_link *found = NULL;
  for (fz_link *link = page->links; link != NULL; link = link->next) {
    if (fz_is_point_inside_rect(mouse_page_point, link->rect)) {
      found = link;
      cursor = c->click_cursor;
      break;
    }
  }
  if (found != page->cache.highlighted_link) {
    page->cache.highlighted_link = found;
    gdk_window_set_cursor(gtk_widget_get_window(widget), cursor);
    return TRUE;
  }
  return FALSE;
}

static gboolean query_tooltip(GtkWidget *widget, int x, int y,
                              gboolean keyboard_mode, GtkTooltip *tooltip) {
  if (keyboard_mode)
    return FALSE;
  PaperViewPrivate *c = paper_view_get_instance_private(PAPER_VIEW(widget));
  fz_context *ctx = c->doci.ctx;
  fz_point mouse_point = {x, y};
  fz_point mouse_page_point;
  fz_location mouse_page_loc;
  trace_point_to_page(widget, &c->doci, mouse_point, &mouse_page_point,
                      &mouse_page_loc);
  fz_link *link = get_page(&c->doci, mouse_page_loc)->cache.highlighted_link;
  if (!link)
    return FALSE;

  gchar text[PATH_MAX + 4]; // 4 for unicode link symbol
  /* fprintf(stderr, "%s\n", link->uri); */
  if (fz_is_external_link(ctx, link->uri)) {
    snprintf(text, sizeof(text), "↪%s", link->uri);
  } else {
    float _x, _y;
    fz_location loc = fz_resolve_link(ctx, c->doci.doc, link->uri, &_x, &_y);
    // start pages and chapters from 1
    loc.chapter += 1;
    loc.page += 1;
    // TODO display the label of the page instead the absolute number. I'm not
    // sure its possible in mupdf though.
    if (c->doci.chapter_count > 1)
      if (c->doci.location.chapter == loc.chapter)
        snprintf(text, sizeof(text), "↪Page %d in current chapter", loc.page);
      else
        snprintf(text, sizeof(text), "↪Chapter %d, page %d", loc.chapter,
                 loc.page);
    else
      snprintf(text, sizeof(text), "↪Page %d", loc.page);
  }
  gtk_tooltip_set_text(tooltip, text);
  return TRUE;
}

static void follow_link(GtkWidget *widget, fz_link *link) {
  // TODO follow non-internal links
  PaperViewPrivate *c = paper_view_get_instance_private(PAPER_VIEW(widget));
  DocInfo *doci = &c->doci;
  fz_context *ctx = doci->ctx;
  fz_point dst_scroll;
  fz_location dst =
      fz_resolve_link(ctx, doci->doc, link->uri, &dst_scroll.x, &dst_scroll.y);
  if (dst.chapter == -1 || dst.page == -1) // invalid link
    // TODO emit some signal
    return;
  doci->location = dst;
  doci->scroll = dst_scroll;
  int width = gtk_widget_get_allocated_width(widget);
  Page *page = get_cur_page(doci);
  fz_matrix scale_ctm = get_scale_ctm(doci, page);
  if (width > fz_transform_rect(page->page_bounds, scale_ctm).x1)
    center_page(width, &c->doci);
  // set back cursor. update_highlighted_link won't reset it since from
  // its perspective the selected link did not change on the
  // newly-followed page
  gdk_window_set_cursor(gtk_widget_get_window(widget), c->default_cursor);
}

static gboolean button_release_event(GtkWidget *widget, GdkEventButton *event) {
  PaperViewPrivate *c = paper_view_get_instance_private(PAPER_VIEW(widget));
  DocInfo *doci = &c->doci;
  switch (event->button) {
  case GDK_BUTTON_PRIMARY:
    if (doci->selection.is_in_progress) {
      doci->selection.is_in_progress = FALSE;
    }
    if (memcmp(&doci->selection.start, &doci->selection.end,
               sizeof(fz_point)) != 0) {
      doci->selection.is_active = TRUE;
    } else {
      if (doci->selection.is_active) {
        doci->selection.is_active = FALSE;
        gtk_widget_queue_draw(widget);
      }

      fz_point mouse_point = {event->x, event->y};
      fz_point mouse_page_point;
      fz_location mouse_page_loc;
      trace_point_to_page(widget, &c->doci, mouse_point, &mouse_page_point,
                          &mouse_page_loc);
      Page *page = get_page(&c->doci, mouse_page_loc);
      fz_link *link = page->cache.highlighted_link;
      if (link != NULL) {
        follow_link(widget, link);
      }
    }
    break;
  }
  return FALSE;
}

static gboolean motion_notify_event(GtkWidget *widget, GdkEventMotion *event) {
  if (event->state & GDK_BUTTON1_MASK) { // user is selecting
    PaperViewPrivate *c = paper_view_get_instance_private(PAPER_VIEW(widget));
    fz_point point = {event->x, event->y};
    fz_point end_point;
    trace_point_to_page(widget, &c->doci, point, &end_point,
                        &c->doci.selection.loc_end);
    c->doci.selection.end = end_point;
    c->doci.selection.id++;
    gtk_widget_queue_draw(widget);
  } else {
    if (update_highlighted_link(widget, fz_make_point(event->x, event->y))) {
      gtk_widget_queue_draw(widget);
    }
  }
  return FALSE;
}

/*
 * Move to next/previous pages if scroll.y is past the page bound
 */
static void scroll_pages(DocInfo *doci) {
  // move to next pages if scroll.y is past page border
  fz_context *ctx = doci->ctx;
  while (doci->scroll.y >=
         get_cur_page(doci)->page_bounds.y1 + PAGE_SEPARATOR_HEIGHT) {
    fz_location next = fz_next_page(ctx, doci->doc, doci->location);
    Page *page = get_cur_page(doci);
    if (locationcmp(next, doci->location) == 0) {
      // end of document
      doci->scroll.y = page->page_bounds.y1;
      break;
    }
    doci->scroll.y -= page->page_bounds.y1 + PAGE_SEPARATOR_HEIGHT;
    doci->location = next;
  }
  // move to previous pages if scroll.y is negative
  while (doci->scroll.y < 0) {
    fz_location next = fz_previous_page(ctx, doci->doc, doci->location);
    if (locationcmp(next, doci->location) == 0) {
      // Beginning of document
      doci->scroll.y = 0;
      break;
    }
    doci->location = next;
    doci->scroll.y +=
        get_cur_page(doci)->page_bounds.y1 + PAGE_SEPARATOR_HEIGHT;
  }
}

static void scroll(DocInfo *doci, fz_point delta) {
  doci->scroll.x += delta.x;
  doci->scroll.y += delta.y;
  scroll_pages(doci);
}

/*
 * Increase zoom by ZOOM_MULTIPLIER and set scroll.x, scroll.y so that POINT (a
 * point in the bounds of WIDGET) stays on the same pixel as it did before
 * adjusting the zoom.
 */
static void zoom_around_point(GtkWidget *widget, DocInfo *doci, float new_zoom,
                              fz_point point) {
  fz_context *ctx = doci->ctx;
  fz_point original_point_in_page;
  fz_location original_loc;
  trace_point_to_page(widget, doci, point, &original_point_in_page,
                      &original_loc);
  for (fz_location loc = original_loc; locationcmp(loc, doci->location) > 0;
       loc = fz_previous_page(ctx, doci->doc, loc)) {
    original_point_in_page.y +=
        get_page(doci, loc)->page_bounds.y1 + PAGE_SEPARATOR_HEIGHT;
  }
  doci->zoom = new_zoom;
  fz_matrix new_scale_ctm = get_scale_ctm(doci, get_page(doci, original_loc));
  fz_matrix new_scale_ctm_inv = fz_invert_matrix(new_scale_ctm);
  fz_point new_point =
      fz_transform_point(original_point_in_page, new_scale_ctm);
  fz_point scaled_diff =
      fz_make_point(new_point.x - point.x, new_point.y - point.y);
  fz_point unscaled_diff = fz_transform_point(scaled_diff, new_scale_ctm_inv);
  doci->scroll = unscaled_diff;
  scroll_pages(doci);
}

/*
 * Scroll relative to widget width/height.
 * Having mult.y = 0.1 means scroll down 10% of the widget height.
 */
void scroll_relatively(GtkWidget *widget, fz_point mult) {
  PaperViewPrivate *c = paper_view_get_instance_private(PAPER_VIEW(widget));
  int w = gtk_widget_get_allocated_width(widget);
  int h = gtk_widget_get_allocated_height(widget);
  Page *page = get_cur_page(&c->doci);
  // don't include rotation
  float rotate = c->doci.rotate;
  c->doci.rotate = 0;
  fz_matrix scale_ctm = get_scale_ctm(&c->doci, page);
  c->doci.rotate = rotate;
  fz_matrix scale_ctm_inv = fz_invert_matrix(scale_ctm);
  fz_point scrolled =
      fz_transform_point(fz_make_point(mult.x * w, mult.y * h), scale_ctm_inv);
  scroll(&c->doci, scrolled);
}

static gboolean scroll_event(GtkWidget *widget, GdkEventScroll *event) {
  PaperViewPrivate *c = paper_view_get_instance_private(PAPER_VIEW(widget));
  if (event->type != GDK_SCROLL) {
    return TRUE;
  }
  if (event->state & GDK_CONTROL_MASK) { // zoom
    float multiplier = 1;
    switch (event->direction) {
    case GDK_SCROLL_UP:
      multiplier = 1.1;
      break;
    case GDK_SCROLL_DOWN:
      multiplier = 1 / 1.1;
      break;
    default:
      fprintf(stderr, "unhandled zoom scroll case\n");
    }
    zoom_around_point(widget, &c->doci, c->doci.zoom * multiplier,
                      fz_make_point(event->x, event->y));
  } else { // scroll
    // scroll 10% of window dimentions
    float multiplier = 0.10;
    fz_point d = {0, 0};
    switch (event->direction) {
    case GDK_SCROLL_UP:
      d.y = -multiplier;
      break;
    case GDK_SCROLL_DOWN:
      d.y = multiplier;
      break;
    case GDK_SCROLL_LEFT:
      d.x = -multiplier;
      break;
    case GDK_SCROLL_RIGHT:
      d.x = multiplier;
      break;
    case GDK_SCROLL_SMOOTH:
      d.x = event->delta_x;
      d.y = event->delta_y;
      fprintf(stderr, "Smooth scroll\n");
      break;
    }
    scroll_relatively(widget, d);
  }
  update_highlighted_link(widget, fz_make_point(event->x, event->y));
  gtk_widget_queue_draw(widget);
  return FALSE;
}

void scroll_to_page_start(GtkWidget *widget) {
  PaperViewPrivate *c = paper_view_get_instance_private(PAPER_VIEW(widget));
  c->doci.scroll.y = 0;
}

void scroll_to_page_end(GtkWidget *widget) {
  PaperViewPrivate *c = paper_view_get_instance_private(PAPER_VIEW(widget));
  int h = gtk_widget_get_allocated_height(widget);
  Page *page = get_cur_page(&c->doci);
  fz_matrix scale_ctm = get_scale_ctm(&c->doci, page);
  fz_rect scaled_bounds = fz_transform_rect(page->page_bounds, scale_ctm);
  float scroll_scaled = scaled_bounds.y1 - h;
  c->doci.scroll.y = fz_transform_point(fz_make_point(0, scroll_scaled),
                                        fz_invert_matrix(scale_ctm))
                         .y;
  scroll_pages(&c->doci);
}

void scroll_whole_pages(GtkWidget *widget, int i) {
  PaperViewPrivate *c = paper_view_get_instance_private(PAPER_VIEW(widget));
  fz_location future;
  if (i > 0) {
    for (; i > 0; i--) {
      future = fz_next_page(c->doci.ctx, c->doci.doc, c->doci.location);
      if (locationcmp(future, c->doci.location) == 0) {
        scroll_to_page_end(widget);
        return;
      }
      c->doci.location = future;
    }
  } else {
    for (; i < 0; i++) {
      future = fz_previous_page(c->doci.ctx, c->doci.doc, c->doci.location);
      if (locationcmp(future, c->doci.location) == 0) {
        // beginning of document, scroll to page start
        c->doci.scroll.y = 0;
        return;
      }
      c->doci.location = future;
    }
  }
}

void goto_first_page(GtkWidget *widget) {
  PaperViewPrivate *c = paper_view_get_instance_private(PAPER_VIEW(widget));
  c->doci.location = fz_make_location(0, 0);
  c->doci.scroll.y = 0;
}

void goto_last_page(GtkWidget *widget) {
  PaperViewPrivate *c = paper_view_get_instance_private(PAPER_VIEW(widget));
  c->doci.location = fz_last_page(c->doci.ctx, c->doci.doc);
  scroll_to_page_end(widget);
}

void zoom_to_window_center(GtkWidget *widget, float multiplier) {
  PaperViewPrivate *c = paper_view_get_instance_private(PAPER_VIEW(widget));
  int w = gtk_widget_get_allocated_width(widget);
  int h = gtk_widget_get_allocated_height(widget);
  float old = c->doci.zoom;
  /* zoom_around_point(widget, &c->doci, old * multiplier, fz_make_point(20,
   * 20)); */
  zoom_around_point(widget, &c->doci, old * multiplier,
                    fz_make_point(w / 2.0f, h / 2.0f));
}

void center(GtkWidget *widget) {
  PaperViewPrivate *c = paper_view_get_instance_private(PAPER_VIEW(widget));
  int w = gtk_widget_get_allocated_width(widget);
  center_page(w, &c->doci);
}

void fit_width(GtkWidget *widget) {
  int w = gtk_widget_get_allocated_width(widget);
  PaperViewPrivate *c = paper_view_get_instance_private(PAPER_VIEW(widget));
  c->doci.scroll.x = 0;
  c->doci.zoom = ((float)w / get_cur_page(&c->doci)->page_bounds.x1);
}

void fit_height(GtkWidget *widget) {
  int h = gtk_widget_get_allocated_height(widget);
  PaperViewPrivate *c = paper_view_get_instance_private(PAPER_VIEW(widget));
  c->doci.scroll.y = 0;
  c->doci.zoom = ((float)h / get_cur_page(&c->doci)->page_bounds.y1);
}

void unset_selection(GtkWidget *widget) {
  PaperViewPrivate *c = paper_view_get_instance_private(PAPER_VIEW(widget));
  c->doci.selection.is_active = 0;
  c->doci.selection.is_in_progress = 0;
  gtk_widget_queue_draw(widget);
}

void unset_search(GtkWidget *widget) {
  PaperViewPrivate *c = paper_view_get_instance_private(PAPER_VIEW(widget));
  c->doci.search[0] = '\0';
  gtk_widget_queue_draw(widget);
}

void set_search(GtkWidget *widget, char *needle) {
  PaperViewPrivate *c = paper_view_get_instance_private(PAPER_VIEW(widget));
  if (strcmp(needle, c->doci.search)) {
    strcpy(c->doci.search, needle);
    c->doci.search_id++;
    gtk_widget_queue_draw(widget);
  }
}

int load_doc(DocInfo *doci, char *filename, char *accel_filename) {
  // zero it all out - the short way of setting everything to NULL.
  memset(doci, 0, sizeof(*doci));
  fz_context *ctx = fz_new_context(NULL, NULL, FZ_STORE_DEFAULT);
  doci->ctx = ctx;

  fz_try(ctx) { fz_register_document_handlers(ctx); }
  fz_catch(ctx) {
    fprintf(stderr, "cannot register document handlers: %s\n",
            fz_caught_message(ctx));
    fz_drop_context(ctx);
    return 1;
  }
  strcpy(doci->filename, filename);
  if (accel_filename)
    strcpy(doci->accel, accel_filename);

  fz_try(ctx) {
    doci->doc = fz_open_document(ctx, doci->filename);
    // TODO epubs don't seem to open with fz_open_accelerated_document, even
    // when the accel filename is NULL.
    /* doci->doc = fz_open_accelerated_document(ctx, doci->filename,
     * doci->accel); */
  }
  fz_catch(ctx) {
    fz_drop_context(ctx);
    return 0;
  }
  fz_location loc = {0, 0};
  doci->location = loc;
  doci->colorspace = fz_device_rgb(ctx);
  doci->zoom = 1.0f;
  /* Count the number of pages. */
  doci->chapter_count = fz_count_chapters(ctx, doci->doc);
  // invalidate location keys on the page cache
  memset(doci->page_cache.locs, -1, sizeof(doci->page_cache.locs));
  // make zeroed-out seach IDs invalid to current one
  doci->search_id = 1;
  doci->selection.id = 1;
  return 1;
}

PaperView *paper_view_new(char *filename, char *accel_filename) {
  GObject *ret = g_object_new(TYPE_PAPER_VIEW, NULL);
  if (ret == NULL) {
    fprintf(stderr, "paper_view_new: can't allocate\n");
    return NULL;
  }
  PaperView *widget = PAPER_VIEW(ret);
  PaperViewPrivate *c = paper_view_get_instance_private(widget);
  if (!load_doc(&c->doci, filename, accel_filename)) {
    fprintf(stderr, "paper_view_new: could not open %s\n", filename);
    return NULL;
  }
  c->has_mouse_event = FALSE;
  return PAPER_VIEW(ret);
}

static void activate(GtkApplication *app, char *filename) {
  GtkWidget *window = gtk_application_window_new(app);
  gtk_window_set_title(GTK_WINDOW(window), "Window");
  gtk_window_set_default_size(GTK_WINDOW(window), 900, 900);
  PaperView *paper = paper_view_new(filename, NULL);
  if (!paper) {
    exit(EXIT_FAILURE);
  }
  gtk_container_add(GTK_CONTAINER(window), GTK_WIDGET(paper));

  gtk_widget_show(GTK_WIDGET(paper));
  gtk_widget_show_all(window);
}
static void paper_view_finalize(GObject *object) {
  PaperViewPrivate *c = paper_view_get_instance_private(PAPER_VIEW(object));
  fz_context *ctx = c->doci.ctx;
  cairo_surface_destroy(c->image_surf);
  for (int i = 0; i < PAGE_CACHE_LEN; i++) {
    drop_page(ctx, &c->doci.page_cache.pages[i]);
  }
  fz_drop_document(ctx, c->doci.doc);
  fz_drop_outline(ctx, c->doci.outline);
  pdf_drop_document(ctx, c->doci.pdf);
  pdf_drop_annot(ctx, c->doci.selected_annot);
  fz_drop_context(c->doci.ctx);
  G_OBJECT_CLASS(paper_view_parent_class)->finalize(object);
}

static void paper_view_class_init(PaperViewClass *class) {

  /* overwrite methods */
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS(class);
  widget_class->draw = draw_callback;
  /* widget_class->size_allocate = size_allocate_stump; */
  widget_class->button_press_event = button_press_event;
  widget_class->motion_notify_event = motion_notify_event;
  widget_class->button_release_event = button_release_event;
  widget_class->scroll_event = scroll_event;
  widget_class->configure_event = configure_event;
  widget_class->query_tooltip = query_tooltip;
  /* widget_class->leave_notify_event   = cb_zathura_page_widget_leave_notify;
   */
  /* widget_class->popup_menu           = cb_zathura_page_widget_popup_menu; */

  GObjectClass *object_class = G_OBJECT_CLASS(class);
  object_class->finalize = paper_view_finalize;
  /* gtk_widget_class->show = ev_loading_message_show; */
  /* gtk_widget_class->hide = ev_loading_message_hide; */
}

static void paper_view_init(PaperView *self) {
  // TODO when I also add GDK_SMOOTH_SCROLL_MASK all scroll events turn to
  // smooth ones with deltas of 0, I don't know how to find the direction in
  // those cases
  gtk_widget_add_events(GTK_WIDGET(self),
                        GDK_EXPOSURE_MASK | GDK_LEAVE_NOTIFY_MASK |
                            GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK |
                            GDK_BUTTON2_MASK | GDK_BUTTON3_MASK |
                            GDK_POINTER_MOTION_MASK |
                            GDK_POINTER_MOTION_HINT_MASK | GDK_SCROLL_MASK);
  gtk_widget_set_has_tooltip(GTK_WIDGET(self), TRUE);
}

int main(int argc, char **argv) {
  if (argc != 2) {
    fprintf(stderr, "Must supply a file name to open\n");
    exit(EXIT_FAILURE);
  }
  char *filename = argv[1];
  GtkApplication *app;
  int status;

  // fool gtk so it doesn't complain that I didn't register myself as a file
  // opener
  argc = 0;
  argv = NULL;
  app = gtk_application_new("org.gtk.example", G_APPLICATION_FLAGS_NONE);
  g_signal_connect(app, "activate", G_CALLBACK(activate), filename);
  status = g_application_run(G_APPLICATION(app), argc, argv);
  g_object_unref(app);
  return status;
}
