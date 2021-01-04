#include "paper-module.h"
#include "PaperView.h"
#include "emacs-module.h"
#include "from-webkit.h"
#include <gtk/gtk.h>

int plugin_is_GPL_compatible;

static void provide(emacs_env *env, const char *feature) {
  emacs_value Qfeat = env->intern(env, feature);
  env->funcall(env, Qprovide, 1, (emacs_value[]){Qfeat});
}

emacs_value signal_memory_full(emacs_env *env) {
  env->non_local_exit_signal(env, env->intern(env, "memory-full"),
                             env->intern(env, "nil"));
  return Qnil;
}

static void signal_error(emacs_env *env, const char *msg) {
  env->non_local_exit_signal(env, env->intern(env, msg),
                             env->make_string(env, msg, strlen(msg)));
}

static gboolean paper_view_close(Client *c) {
  debug_print("c %p paper_view_close\n", c);
  send_to_lisp(c, "paper--close", "");
  return TRUE;
}

static emacs_value Fpaper_new(emacs_env *env, ptrdiff_t nargs,
                              emacs_value args[], void *data) {
  UNUSED(nargs);
  UNUSED(data);
  emacs_value channel = args[0];
  emacs_value open_new_window = args[1];
  emacs_value filename = args[2];
  emacs_value accel_filename = args[3];

  int argc = 0;
  char **argv = NULL;
  if (!gtk_init_check(&argc, &argv)) {
    env->non_local_exit_signal(env, env->intern(env, "paper-init-gtk-failed"),
                               env->intern(env, "nil"));
    return Qnil;
  }

  Client *c;
  if (!(c = calloc(1, sizeof(Client)))) {
    return signal_memory_full(env);
  }

  c->fd = env->open_channel(env, channel);
  if (env->non_local_exit_check(env) != emacs_funcall_exit_return)
    return Qnil;

  ptrdiff_t len = PATH_MAX;
  char filename_c[len];
  char _accel_filename_c[len];
  char *accel_filename_c = _accel_filename_c;
  env->copy_string_contents(env, filename, filename_c, &len);
  if (len > PATH_MAX)
    return Qnil;
  len = PATH_MAX;
  if (env->is_not_nil(env, accel_filename)) {
    env->copy_string_contents(env, accel_filename, accel_filename_c, &len);
    if (len > PATH_MAX)
      return Qnil;
  } else {
    accel_filename_c = NULL;
  }

  c->view = GTK_WIDGET(paper_view_new(filename_c, accel_filename_c));

  if (!c->view) {
    signal_error(env, "paper-module-couldnt-create-widget");
    return Qnil;
  }
  /* set lifetime of c->view to be same as c which is owend by Emacs user_ptr */
  g_object_ref(c->view);
  g_object_ref_sink(c->view);
  gtk_widget_set_can_focus(GTK_WIDGET(c->view), FALSE);
  // gtk_widget_set_focus_on_click (GTK_WIDGET (c->view), FALSE);

#ifdef DEBUG
  // print_widget_tree (gtk_window_list_toplevels());
#endif
  if (env->is_not_nil(env, open_new_window)) {
    c->container = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    // gtk_window_set_default_size
    //  (GTK_WINDOW(c->container),
    //   (n > 2) ? env->extract_integer (env, args[2]) : 400
    //   (n > 3) ? env->extract_integer (env, args[3]) : 600);

    g_signal_connect(G_OBJECT(c->container), "destroy",
                     G_CALLBACK(window_destroy), c);

    gtk_container_add(GTK_CONTAINER(c->container), GTK_WIDGET(c->view));
    gtk_widget_show_all(c->container);
  } else {
    fprintf(stderr, "in own window\n");
    GtkFixed *fixed = find_focused_fixed_widget();
    if (fixed == NULL) {
      fprintf(stderr, "no focused\n");
      signal_error(env, "paper-module-no-focused-fixed-widget");
      return Qnil;
    }
    client_change_container(c, fixed);
    gtk_widget_show_all(GTK_WIDGET(c->view));
    fprintf(stderr, "finished opening!\n");
  }

  // g_signal_connect (G_OBJECT (c->view), "destroy",
  //                  G_CALLBACK(webview_destroy), c);
  /* g_signal_connect(G_OBJECT(c->view), "close", G_CALLBACK(paper_view_close),
     c); */
  /* g_signal_connect(webkit_web_view_get_find_controller(c->view), */
  /*                  "counted-matches", */
  /*                  G_CALLBACK(findcontroller_counted_matches), c) */;

  return env->make_user_ptr(env, client_free, (void *)c);
}

emacs_value Fpaper_scroll(emacs_env *env, ptrdiff_t nargs, emacs_value args[],
                          void *data) {
  UNUSED(nargs);
  UNUSED(data);
  Client *c = env->get_user_ptr(env, args[0]);
  double x = env->extract_float(env, args[1]);
  double y = env->extract_float(env, args[2]);
  scroll_relatively(c->view, fz_make_point(x, y));
  return Qnil;
}

emacs_value Fpaper_page_scroll(emacs_env *env, ptrdiff_t nargs,
                               emacs_value args[], void *data) {
  UNUSED(nargs);
  UNUSED(data);
  Client *c = env->get_user_ptr(env, args[0]);
  int x = env->extract_integer(env, args[1]);
  scroll_whole_pages(c->view, x);
  return Qnil;
}

emacs_value Fpaper_zoom(emacs_env *env, ptrdiff_t nargs, emacs_value args[],
                        void *data) {
  UNUSED(nargs);
  UNUSED(data);
  Client *c = env->get_user_ptr(env, args[0]);
  double m = env->extract_float(env, args[1]);
  zoom_to_window_center(c->view, m);
  return Qnil;
}

#define BIND_WIDGET(new_name, Fname)                                           \
  emacs_value new_name(emacs_env *env, ptrdiff_t nargs, emacs_value args[],    \
                       void *data) {                                           \
    UNUSED(nargs);                                                             \
    UNUSED(data);                                                              \
    Client *c = env->get_user_ptr(env, args[0]);                               \
    Fname(c->view);                                                            \
    return Qnil;                                                               \
  }

BIND_WIDGET(Fpaper_center, center);
BIND_WIDGET(Fpaper_goto_first_page, goto_first_page);
BIND_WIDGET(Fpaper_goto_last_page, goto_last_page);
BIND_WIDGET(Fpaper_scroll_to_page_start, scroll_to_page_start);
BIND_WIDGET(Fpaper_scroll_to_page_end, scroll_to_page_end);
BIND_WIDGET(Fpaper_fit_width, fit_width);
BIND_WIDGET(Fpaper_fit_height, fit_height);

emacs_value Fpaper_get_selection(emacs_env *env, ptrdiff_t nargs,
                                 emacs_value args[], void *data) {
  UNUSED(nargs);
  UNUSED(data);
  Client *c = env->get_user_ptr(env, args[0]);
  size_t len = 0;
  char *sel = get_selection(c->view, &len);
  if (!sel)
    return Qnil;
  emacs_value res = env->make_string(env, sel, len);
  free(sel);
  return res;
}

static void mkfn(emacs_env *env, ptrdiff_t min_arity, ptrdiff_t max_arity,
                 emacs_value (*func)(emacs_env *env, ptrdiff_t nargs,
                                     emacs_value *args, void *data),
                 const char *name, const char *docstring) {
  emacs_value Sfun =
      env->make_function(env, min_arity, max_arity, func, docstring, NULL);
  emacs_value Qsym = env->intern(env, name);

  env->funcall(env, Qfset, 2, (emacs_value[]){Qsym, Sfun});
}

int emacs_module_init(struct emacs_runtime *ert) {
  emacs_env *env = ert->get_environment(ert);

  // Symbols
  Qnil = env->make_global_ref(env, env->intern(env, "nil"));
  Qfset = env->make_global_ref(env, env->intern(env, "fset"));
  Qprovide = env->make_global_ref(env, env->intern(env, "provide"));
  Qargs_out_of_range =
      env->make_global_ref(env, env->intern(env, "args-out-of-range"));
  // Functions
  mkfn(env, 4, 4, Fpaper_new, "paper--new",
       "\\fn(PIPE, IN-OWN-WINDOW, FILENAME, ACCEL_FILENAME)\n");
  mkfn(env, 2, 2, client_move_to_frame, "paper--move-to-frame", "");
  mkfn(env, 1, 1, client_show, "paper--show", "");
  mkfn(env, 1, 1, client_hide, "paper--hide", "");
  mkfn(env, 5, 5, client_resize, "paper--resize", "");
  mkfn(env, 1, 1, client_destroy, "paper--destroy", "");
  mkfn(env, 3, 3, Fpaper_scroll, "paper--scroll", "\\fn(ID, X, Y)");
  mkfn(env, 2, 2, Fpaper_page_scroll, "paper--scroll-pagewise", "\\fn(ID, I)");
  mkfn(env, 2, 2, Fpaper_zoom, "paper--zoom", "\\fn(ID, MULTIPLIER)");
  mkfn(env, 1, 1, Fpaper_center, "paper--center", "");
  mkfn(env, 1, 1, Fpaper_goto_first_page, "paper--goto-first-page", "");
  mkfn(env, 1, 1, Fpaper_goto_last_page, "paper--goto-last-page", "");
  mkfn(env, 1, 1, Fpaper_scroll_to_page_start, "paper--scroll-to-page-start",
       "");
  mkfn(env, 1, 1, Fpaper_scroll_to_page_end, "paper--scroll-to-page-end", "");
  mkfn(env, 1, 1, Fpaper_fit_width, "paper--fit-width", "");
  mkfn(env, 1, 1, Fpaper_fit_height, "paper--fit-height", "");
  mkfn(env, 1, 1, Fpaper_get_selection, "paper--get-selection", "");

  // done
  provide(env, "paper-module");
  return 0;
}
