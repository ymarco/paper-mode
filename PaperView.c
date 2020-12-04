#include <math.h>
#include <mupdf/fitz.h>
#include <mupdf/pdf.h> /* for pdf specifics and forms */
#include <mupdf/ucdn.h>

#include "PaperView.h"
#include <gtk/gtk.h>

static fz_context *ctx;
const int PAGE_SEPARATOR_HEIGHT = 18;

G_DEFINE_TYPE_WITH_PRIVATE(PaperView, paper_view, GTK_TYPE_DRAWING_AREA);

void ensure_chapter_is_loaded(DocInfo *doci, int chapter) {
  if (doci->pages[chapter])
    return;
  doci->page_count_for_chapter[chapter] =
      fz_count_chapter_pages(ctx, doci->doc, chapter);
  doci->pages[chapter] =
      calloc(sizeof(Page), doci->page_count_for_chapter[chapter]);
}

void ensure_page_is_loaded(DocInfo *doci, fz_location location) {
  ensure_chapter_is_loaded(doci, location.chapter);
  Page *page = &doci->pages[location.chapter][location.page];
  if (page->page)
    return;
  page->page =
      fz_load_chapter_page(ctx, doci->doc, location.chapter, location.page);
  page->page_text = fz_new_stext_page_from_page(ctx, page->page, NULL);
  page->seps = NULL; // TODO seps
  page->links = fz_load_links(ctx, page->page);
  page->page_bounds = fz_bound_page(ctx, page->page);
  page->display_list = fz_new_display_list(ctx, page->page_bounds);
  // populate display_list
  fz_device *device = fz_new_list_device(ctx, page->display_list);
  fz_run_page(ctx, page->page, device, fz_identity, NULL);
  fz_close_device(ctx, device);
  fz_drop_device(ctx, device);
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
    if (res->y < page->page_bounds.y1) {
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
  const int hl_count = 2048;
  // TODO cache hl when selection doesn't change?
  fz_quad hl[hl_count];
  int hits_count =
      fz_highlight_selection(ctx, page->page_text, page->selection_start,
                             page->selection_end, hl, hl_count);
  for (int i = 0; i < hits_count; i++) {
    fz_quad box = fz_transform_quad(hl[i], ctm);
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
  fz_try(ctx) { ensure_page_is_loaded(&c->doci, loc); }
  fz_catch(ctx) {
    fprintf(stderr, "can't load page");
    exit(EXIT_FAILURE);
  }

  Page *page = &c->doci.pages[loc.chapter][loc.page];
  fz_matrix scale_ctm = get_scale_ctm(&c->doci, page);
  fz_point stopped = fz_make_point(-c->doci.scroll.x, -c->doci.scroll.y);
  while (fz_transform_point(stopped, scale_ctm).y < height) {
    fz_matrix scale_ctm = get_scale_ctm(&c->doci, page);
    fz_matrix draw_page_ctm =
        fz_concat(fz_translate(stopped.x, stopped.y), scale_ctm);
    // foreground around page boundry
    fz_clear_pixmap_rect_with_value(
        ctx, pixmap, 0xFF,
        fz_round_rect(fz_transform_rect(page->page_bounds, draw_page_ctm)));
    // highlight text selection
    if ((c->doci.selection_active || c->doci.selecting) &&
        memcmp(&loc, &c->doci.selection_loc_end, sizeof(fz_location)) <= 0) {
      highlight_selection(page, pixmap, draw_page_ctm);
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
    fprintf(stderr, "start: %3.0f %3.0f\n", page->selection_start.x,
            page->selection_start.y);
    gtk_widget_queue_draw(widget);
    switch (event->type) {
    case GDK_BUTTON_PRESS:
      c->doci.selection_mode = FZ_SELECT_CHARS;
      break;
    case GDK_2BUTTON_PRESS:
      c->doci.selection_mode = FZ_SELECT_WORDS;
      fprintf(stderr, "button 2 press\n");
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
  }
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
    doci->selection_active =
        memcmp(&get_page(doci, doci->selection_loc_start)->selection_start,
               &get_page(doci, doci->selection_loc_end)->selection_end,
               sizeof(fz_point)) != 0;
    break;
  }
  return FALSE;
}

static gboolean motion_notify_event(GtkWidget *widget, GdkEventMotion *event) {
  if (event->state & GDK_BUTTON1_MASK) {
    complete_selection(widget, fz_make_point(event->x, event->y));
    gtk_widget_queue_draw(widget);
  }
  return FALSE;
}

/*
 * Move to next/previous pages if scroll.y is past the page bound
 */
static void scroll_pages(DocInfo *doci) {
  while (doci->scroll.y >= get_page(doci, doci->location)->page_bounds.y1) {
    doci->scroll.y -= get_page(doci, doci->location)->page_bounds.y1;
    doci->location = fz_next_page(ctx, doci->doc, doci->location);
  }
  // move to previous pages if scroll.y is negative
  while (doci->scroll.y < 0) {
    doci->location = fz_previous_page(ctx, doci->doc, doci->location);
    doci->scroll.y += get_page(doci, doci->location)->page_bounds.y1;
  }
}

static void scroll(DocInfo *doci, float delta_x, float delta_y) {
  // TODO don't let scroll.x get out of the page
  doci->scroll.x += delta_x;
  doci->scroll.y += delta_y;
  scroll_pages(doci);
}

/*
 * Increase zoom by ZOOM_MULTIPLIER and set scroll.x, scroll.y so that POINT (a
 * point in the bounds of WIDGET) stays on the same pixel as it did before
 * adjusting the zoom.
 */
static void zoom_around_point(GtkWidget *widget, DocInfo *doci,
                              float zoom_multiplier, fz_point point) {
  fz_point original_point_in_page;
  fz_location _original_location;
  trace_point_to_page(widget, doci, point, &original_point_in_page,
                      &_original_location);
  doci->zoom *= zoom_multiplier;
  fz_matrix new_scale_ctm = get_scale_ctm(doci, get_page(doci, doci->location));
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
    zoom_around_point(widget, &c->doci, multiplier,
                      fz_make_point(event->x, event->y));
  } else { // scroll
    float d_x = 0.0f, d_y = 0.0f;
    switch (event->direction) {
    case GDK_SCROLL_UP:
      d_y = -50;
      break;
    case GDK_SCROLL_DOWN:
      d_y = 50;
      break;
    case GDK_SCROLL_LEFT:
      d_x = -50;
      break;
    case GDK_SCROLL_RIGHT:
      d_x = 50;
      break;
    case GDK_SCROLL_SMOOTH:
      d_x = event->delta_x;
      d_y = event->delta_y;
      fprintf(stderr, "Smooth scroll\n");
      break;
    }
    scroll(&c->doci, d_x, d_y);
  }
  gtk_widget_queue_draw(widget);
  return FALSE;
}

void load_doc(DocInfo *doci, char *filename, char *accel_filename) {
  // zero it all out - the short way of setting everything to NULL.
  memset(doci, 0, sizeof(*doci));
  strcpy(doci->filename, filename);
  if (accel_filename)
    strcpy(doci->accel, accel_filename);

  fz_try(ctx) doci->doc =
      fz_open_accelerated_document(ctx, doci->filename, doci->accel);
  fz_catch(ctx) {
    fprintf(stderr, "cannot open document: %s\n", fz_caught_message(ctx));
    fz_drop_context(ctx);
    exit(EXIT_FAILURE);
  }
  fz_location loc = {0, 0};
  doci->location = loc;
  doci->colorspace = fz_device_rgb(ctx);
  doci->zoom = 100.0f;
  /* Count the number of pages. */
  fz_try(ctx) {
    doci->chapter_count = fz_count_chapters(ctx, doci->doc);
    if (!(doci->pages = calloc(sizeof(Page *), doci->chapter_count))) {
      fz_throw(ctx, 1, "Can't allocate");
    }
    if (!(doci->page_count_for_chapter =
              calloc(sizeof(int *), doci->chapter_count))) {
      fz_throw(ctx, 1, "Can't allocate");
    }
  }
  fz_catch(ctx) {
    fprintf(stderr, "cannot count number of pages: %s\n",
            fz_caught_message(ctx));
    fz_drop_document(ctx, doci->doc);
    fz_drop_context(ctx);
    exit(EXIT_FAILURE);
  }
}

PaperView *paper_view_new(char *filename, char *accel_filename) {
  GObject *ret = g_object_new(TYPE_PAPER_VIEW, NULL);
  if (ret == NULL) {
    return NULL;
  }
  PaperView *widget = PAPER_VIEW(ret);
  PaperViewPrivate *c = paper_view_get_instance_private(widget);
  load_doc(&c->doci, filename, accel_filename);
  c->has_mouse_event = FALSE;
  return PAPER_VIEW(ret);
}

static void activate(GtkApplication *app, char *filename) {
  GtkWidget *window = gtk_application_window_new(app);
  gtk_window_set_title(GTK_WINDOW(window), "Window");
  gtk_window_set_default_size(GTK_WINDOW(window), 900, 900);
  PaperView *paper = paper_view_new(filename, NULL);
  gtk_container_add(GTK_CONTAINER(window), GTK_WIDGET(paper));

  gtk_widget_show(GTK_WIDGET(paper));
  gtk_widget_show_all(window);
}


void drop_page(Page *page) {
  fz_drop_stext_page(ctx, page->page_text);
  fz_drop_separations(ctx, page->seps);
  fz_drop_link(ctx, page->links);
  fz_drop_page(ctx, page->page);
  fz_drop_display_list(ctx, page->display_list);
  memset(page, 0, sizeof(*page));
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

  /* GObjectClass *object_class = G_OBJECT_CLASS(class); */
  /* object_class->dispose = zathura_page_widget_dispose; */
  /* object_class->finalize = zathura_page_widget_finalize; */
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

  app = gtk_application_new("org.gtk.example", G_APPLICATION_FLAGS_NONE);
  g_signal_connect(app, "activate", G_CALLBACK(activate), "./amsmath.pdf");
  status = g_application_run(G_APPLICATION(app), argc, argv);
  g_object_unref(app);
  fz_drop_context(ctx);
  return status;
}
