#include <math.h>
#include <mupdf/fitz.h>
#include <mupdf/pdf.h> /* for pdf specifics and forms */
#include <mupdf/ucdn.h>

#include "PaperView.h"
#include <gtk/gtk.h>

static fz_context *ctx;

G_DEFINE_TYPE_WITH_PRIVATE(PaperView, paper_view, GTK_TYPE_DRAWING_AREA);

void ensure_chapter_is_loaded(DocInfo *doci, int chapter) {
  if (doci->pages[chapter])
    return;
  doci->page_count_for_chapter[chapter] =
      fz_count_chapter_pages(ctx, doci->doc, chapter);
  doci->pages[chapter] =
      calloc(sizeof(Page), doci->page_count_for_chapter[chapter]);
}

void drop_page(Page *page) {
  if (!page)
    return;
  fz_drop_page(ctx, page->page);
  fz_drop_stext_page(ctx, page->page_text);
  fz_drop_separations(ctx, page->seps);
  fz_drop_link(ctx, page->links);
  fz_drop_display_list(ctx, page->display_list);
  free(page->cache.selection_quads);
}

void ensure_page_is_loaded(DocInfo *doci, fz_location location) {
  ensure_chapter_is_loaded(doci, location.chapter);
  Page *page = &doci->pages[location.chapter][location.page];
  if (page->page)
    return;
  fz_try(ctx) {
    page->page =
        fz_load_chapter_page(ctx, doci->doc, location.chapter, location.page);
    page->page_text = fz_new_stext_page_from_page(ctx, page->page, NULL);
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
  }
  fz_catch(ctx) {
    drop_page(page);
    fz_rethrow(ctx);
  }
}

Page *get_page(DocInfo *doci, fz_location loc) {
  ensure_page_is_loaded(doci, loc);
  return &doci->pages[loc.chapter][loc.page];
}

fz_matrix get_scale_ctm(DocInfo *doci, Page *page) {
  return fz_transform_page(page->page_bounds, doci->zoom, doci->rotate);
}

/*
 * Get the position of POINT whithin the boundries of the current or next pages.
 */
static void trace_point_to_page(GtkWidget *widget, DocInfo *doci,
                                fz_point point, fz_point *res,
                                fz_location *loc) {
  *loc = doci->location;
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
  Page *page = get_page(doci, doci->location);
  fz_matrix scale_ctm = get_scale_ctm(doci, page);
  fz_rect scaled_bounds = fz_transform_rect(page->page_bounds, scale_ctm);
  fz_matrix scale_ctm_inv = fz_invert_matrix(scale_ctm);
  fz_point centered_page_start = fz_transform_point(
      fz_make_point(((float)scaled_bounds.x1 - surface_width) / 2, 0),
      scale_ctm_inv);

  doci->scroll.x = centered_page_start.x;
}

static void highlight_selection(Page *page, fz_pixmap *pixmap, fz_matrix ctm) {
  for (int i = 0; i < page->cache.selection_quads_count; i++) {
    fz_quad box = fz_transform_quad(page->cache.selection_quads[i], ctm);
    fz_clear_pixmap_rect_with_value(
        ctx, pixmap, 0xE8,
        fz_round_rect(fz_make_rect(box.ul.x, box.ul.y, box.lr.x, box.lr.y)));
    // TODO actually fill in the quad instead of assuming its a rectangle
  }
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
  GdkRectangle rec = {.width = gtk_widget_get_allocated_width(widget),
                      .height = gtk_widget_get_allocated_height(widget)};
  allocate_pixmap(widget, &rec);

  cairo_surface_t *surface = c->image_surf;

  unsigned int width = cairo_image_surface_get_width(surface);
  unsigned int height = cairo_image_surface_get_height(surface);

  unsigned char *image = cairo_image_surface_get_data(surface);

  fz_irect whole_rect = {.x1 = width, .y1 = height};

  fz_pixmap *pixmap = fz_new_pixmap_with_bbox_and_data(
      ctx, c->doci.colorspace, whole_rect, NULL, 1, image);
  // background
  fz_clear_pixmap_with_value(ctx, pixmap, 0xF0);

  fz_device *draw_device = fz_new_draw_device(ctx, fz_identity, pixmap);
  fz_location loc = c->doci.location;
  Page *page = get_page(&c->doci, loc);
  fz_matrix scale_ctm = get_scale_ctm(&c->doci, page);
  fz_matrix draw_page_ctm;
  fz_point stopped = fz_make_point(-c->doci.scroll.x, -c->doci.scroll.y);

  while (fz_transform_point(stopped, scale_ctm).y < height) {
    scale_ctm = get_scale_ctm(&c->doci, page);
    draw_page_ctm = fz_concat(fz_translate(stopped.x, stopped.y), scale_ctm);
    // foreground around page boundry
    fz_clear_pixmap_rect_with_value(
        ctx, pixmap, 0xFF,
        fz_round_rect(fz_transform_rect(page->page_bounds, draw_page_ctm)));
    // highlight text selection
    if ((c->doci.selection_active || c->doci.selecting) &&
        memcmp(&loc, &c->doci.selection_loc_end, sizeof(fz_location)) <= 0) {
      highlight_selection(page, pixmap, draw_page_ctm);
    }
    // highlight selected link
    if (page->cache.highlighted_link) {
      fz_clear_pixmap_rect_with_value(
          ctx, pixmap, 0xD0,
          fz_round_rect(fz_transform_rect(page->cache.highlighted_link->rect,
                                          draw_page_ctm)));
    }
    /* fz_run_page(ctx, page->page, draw_device, draw_page_ctm, &cookie); */
    fz_run_display_list(ctx, page->display_list, draw_device, draw_page_ctm,
                        page->page_bounds, NULL);
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
    c->doci.selecting = TRUE;
    fz_point orig_point;
    trace_point_to_page(widget, &c->doci, fz_make_point(event->x, event->y),
                        &orig_point, &c->doci.selection_loc_start);
    Page *page = get_page(&c->doci, c->doci.selection_loc_start);
    page->selection_start = orig_point;
    gtk_widget_queue_draw(widget);
    switch (event->type) {
    case GDK_BUTTON_PRESS:
      c->doci.selection_mode = FZ_SELECT_CHARS;
      break;
    case GDK_2BUTTON_PRESS:
      c->doci.selection_mode = FZ_SELECT_WORDS;
      break;
    case GDK_3BUTTON_PRESS:
      c->doci.selection_mode = FZ_SELECT_LINES;
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

static void complete_selection(GtkWidget *widget, fz_point point) {
  PaperViewPrivate *c = paper_view_get_instance_private(PAPER_VIEW(widget));
  fz_point end_point;
  trace_point_to_page(widget, &c->doci, point, &end_point,
                      &c->doci.selection_loc_end);
  Page *sel_end_page = get_page(&c->doci, c->doci.selection_loc_end);
  sel_end_page->selection_end = end_point;
  Page *sel_start_page = get_page(&c->doci, c->doci.selection_loc_start);
  // set all pages between loc_start and loc_and to full selection
  for (fz_location loc = c->doci.selection_loc_start;
       memcmp(&loc, &c->doci.selection_loc_end, sizeof(fz_location)) <= 0;
       loc = fz_next_page(ctx, c->doci.doc, loc)) {
    Page *page = get_page(&c->doci, loc);
    if (page != sel_start_page) {
      page->selection_start.x = page->page_bounds.x0;
      page->selection_start.y = page->page_bounds.y0;
    }
    if (page != sel_end_page) {
      page->selection_end.x = page->page_bounds.x1;
      page->selection_end.y = page->page_bounds.y1;
    }
    fz_snap_selection(ctx, page->page_text, &page->selection_start,
                      &page->selection_end, c->doci.selection_mode);

    int max_quads_count = 1024;
    page->cache.selection_quads = malloc(max_quads_count * sizeof(fz_quad));
    page->cache.selection_quads_count = fz_highlight_selection(
        ctx, page->page_text, page->selection_start, page->selection_end,
        page->cache.selection_quads, max_quads_count);
  }
}

/*
 * Update cache.highlighted_link on the page below mouse_point.
 * Return TRUE if the link was updated.
 */
static gboolean update_highlighted_link(GtkWidget *widget,
                                        fz_point mouse_point) {
  PaperViewPrivate *c = paper_view_get_instance_private(PAPER_VIEW(widget));
  fz_point mouse_page_point;
  fz_location mouse_page_loc;
  trace_point_to_page(widget, &c->doci, mouse_point, &mouse_page_point,
                      &mouse_page_loc);
  Page *page = get_page(&c->doci, mouse_page_loc);
  fz_link *found = NULL;
  for (fz_link *link = page->links; link != NULL; link = link->next) {
    if (fz_is_point_inside_rect(mouse_page_point, link->rect)) {
      found = link;
      break;
    }
  }
  // no intersecting link found
  if (found != page->cache.highlighted_link) {
    page->cache.highlighted_link = found;
    return TRUE;
  }
  return FALSE;
}

static gboolean button_release_event(GtkWidget *widget, GdkEventButton *event) {
  PaperViewPrivate *c = paper_view_get_instance_private(PAPER_VIEW(widget));
  DocInfo *doci = &c->doci;
  switch (event->button) {
  case GDK_BUTTON_PRIMARY:
    if (doci->selecting) {
      doci->selecting = FALSE;
      complete_selection(widget, fz_make_point(event->x, event->y));
      gtk_widget_queue_draw(widget);
    }
    if (memcmp(&get_page(doci, doci->selection_loc_start)->selection_start,
               &get_page(doci, doci->selection_loc_end)->selection_end,
               sizeof(fz_point)) != 0) {
      doci->selection_active = TRUE;
    } else {
      doci->selection_active = FALSE;

      fz_point mouse_point = {event->x, event->y};
      fz_point mouse_page_point;
      fz_location mouse_page_loc;
      trace_point_to_page(widget, &c->doci, mouse_point, &mouse_page_point,
                          &mouse_page_loc);
      Page *page = get_page(&c->doci, mouse_page_loc);
      fz_link *link = page->cache.highlighted_link;
      if (link != NULL) {
        // TODO follow non-internal links
        doci->location = fz_resolve_link(ctx, doci->doc, link->uri,
                                         &doci->scroll.x, &doci->scroll.y);
      }
    }
    break;
  }
  return FALSE;
}

static gboolean motion_notify_event(GtkWidget *widget, GdkEventMotion *event) {
  if (event->state & GDK_BUTTON1_MASK) { // selecting
    complete_selection(widget, fz_make_point(event->x, event->y));
    gtk_widget_queue_draw(widget);
  } else {
    if (update_highlighted_link(widget, fz_make_point(event->x, event->y))) {
      fprintf(stderr, "updating link\n");
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
  while (doci->scroll.y >= get_page(doci, doci->location)->page_bounds.y1 +
                               PAGE_SEPARATOR_HEIGHT) {
    fz_location next = fz_next_page(ctx, doci->doc, doci->location);
    Page *page = get_page(doci, doci->location);
    if (memcmp(&next, &doci->location, sizeof(next)) == 0) {
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
    if (memcmp(&next, &doci->location, sizeof(next)) == 0) {
      // Beginning of document
      doci->scroll.y = 0;
      break;
    }
    doci->location = next;
    doci->scroll.y +=
        get_page(doci, doci->location)->page_bounds.y1 + PAGE_SEPARATOR_HEIGHT;
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
  fz_point original_point_in_page;
  fz_location original_loc;
  trace_point_to_page(widget, doci, point, &original_point_in_page,
                      &original_loc);
  for (fz_location loc = original_loc;
       memcmp(&doci->location, &loc, sizeof(loc)) < 0;
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
    int w = gtk_widget_get_allocated_width(widget);
    int h = gtk_widget_get_allocated_height(widget);
    // scroll 10% of window pixels
    float multiplier = 0.10;
    Page *page = get_page(&c->doci, c->doci.location);
    // don't include rotation
    fz_matrix scale_ctm = fz_transform_page(page->page_bounds, c->doci.zoom, 0);
    fz_matrix scale_ctm_inv = fz_invert_matrix(scale_ctm);
    fz_point scrolled = fz_transform_point(
        fz_make_point(multiplier * w, multiplier * h), scale_ctm_inv);
    fz_point d = {0, 0};
    switch (event->direction) {
    case GDK_SCROLL_UP:
      d.y = -scrolled.y;
      break;
    case GDK_SCROLL_DOWN:
      d.y = scrolled.y;
      break;
    case GDK_SCROLL_LEFT:
      d.x = -scrolled.x;
      break;
    case GDK_SCROLL_RIGHT:
      d.x = scrolled.x;
      break;
    case GDK_SCROLL_SMOOTH:
      d.x = event->delta_x;
      d.y = event->delta_y;
      fprintf(stderr, "Smooth scroll\n");
      break;
    }
    scroll(&c->doci, d);
  }
  update_highlighted_link(widget, fz_make_point(event->x, event->y));
  gtk_widget_queue_draw(widget);
  return FALSE;
}

void load_doc(DocInfo *doci, char *filename, char *accel_filename) {
  // zero it all out - the short way of setting everything to NULL.
  memset(doci, 0, sizeof(*doci));
  strcpy(doci->filename, filename);
  if (accel_filename)
    strcpy(doci->accel, accel_filename);

  doci->doc = fz_open_document(ctx, doci->filename);
  // TODO epubs don't seem to open with fz_open_accelerated_document, even when
  // the accel filename is NULL.
  /* doci->doc = fz_open_accelerated_document(ctx, doci->filename, doci->accel); */
  fz_location loc = {0, 0};
  doci->location = loc;
  doci->colorspace = fz_device_rgb(ctx);
  doci->zoom = 100.0f;
  /* Count the number of pages. */
  doci->chapter_count = fz_count_chapters(ctx, doci->doc);
  if (!(doci->pages = calloc(sizeof(Page *), doci->chapter_count))) {
    fz_throw(ctx, 1, "Can't allocate");
  }
  if (!(doci->page_count_for_chapter =
            calloc(sizeof(int *), doci->chapter_count))) {
    fz_throw(ctx, 1, "Can't allocate");
  }
}

PaperView *paper_view_new(char *filename, char *accel_filename) {
  GObject *ret = g_object_new(TYPE_PAPER_VIEW, NULL);
  if (ret == NULL) {
    fprintf(stderr, "paper_view_new: can't allocate\n");
    return NULL;
  }
  PaperView *widget = PAPER_VIEW(ret);
  PaperViewPrivate *c = paper_view_get_instance_private(widget);
  fz_try(ctx) { load_doc(&c->doci, filename, accel_filename); }
  fz_catch(ctx) {
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
  cairo_surface_destroy(c->image_surf);
  Page *chapter_pages;
  for (int chapter = 0; chapter < c->doci.chapter_count; chapter++) {
    chapter_pages = c->doci.pages[chapter];
    if (chapter_pages) {
      for (int page = 0; page < c->doci.page_count_for_chapter[chapter]; page++)
        drop_page(&chapter_pages[page]);
      free(chapter_pages);
    }
  }
  fz_drop_document(ctx, c->doci.doc);
  fz_drop_outline(ctx, c->doci.outline);
  pdf_drop_document(ctx, c->doci.pdf);
  pdf_drop_annot(ctx, c->doci.selected_annot);
  free(c->doci.pages);
  free(c->doci.page_count_for_chapter);
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
}

int main(int argc, char **argv) {
  if (argc != 2) {
    fprintf(stderr, "Must supply a file name to open\n");
    exit(EXIT_FAILURE);
  }
  char *filename = argv[1];
  ctx = fz_new_context(NULL, NULL, FZ_STORE_DEFAULT);
  GtkApplication *app;
  int status;

  fz_try(ctx) { fz_register_document_handlers(ctx); }
  fz_catch(ctx) {
    fprintf(stderr, "cannot register document handlers: %s\n",
            fz_caught_message(ctx));
    fz_drop_context(ctx);
    return EXIT_FAILURE;
  }

  // fool gtk so it doesn't complain that I didn't register myself as a file opener
  argc = 0;
  argv = NULL;
  app = gtk_application_new("org.gtk.example", G_APPLICATION_FLAGS_NONE);
  g_signal_connect(app, "activate", G_CALLBACK(activate), filename);
  status = g_application_run(G_APPLICATION(app), argc, argv);
  g_object_unref(app);
  fz_drop_context(ctx);
  return status;
}
