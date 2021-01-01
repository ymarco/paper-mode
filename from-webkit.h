#ifndef __FROM_WEBKIT_H_
#define __FROM_WEBKIT_H_

#include "PaperView.h"
#include "paper-module.h"
#include <gtk/gtk.h>

#ifdef DEBUG
#define DEBUG_TEST 1
#else
#define DEBUG_TEST 0
#endif

#define debug_print(fmt, ...)                                                  \
  do {                                                                         \
    if (DEBUG_TEST)                                                            \
      fprintf(stderr, fmt, ##__VA_ARGS__);                                     \
  } while (0)

static ssize_t rio_writen(int fd, void *usrbuf, size_t n) {
  size_t nleft = n;
  ssize_t nwritten;
  char *bufp = usrbuf;

  while (nleft > 0) {
    if ((nwritten = write(fd, bufp, nleft)) <= 0) {
      if (errno == EINTR) /* Interrupted by sig handler return */
        nwritten = 0;     /* and call write() again */
      else
        return -1; /* errno set by write() */
    }
    nleft -= nwritten;
    bufp += nwritten;
  }
  return n;
}

static void send_to_lisp(Client *c, const char *id, const char *message) {
  if (id == NULL || message == NULL ||
      rio_writen(c->fd, (void *)id, strlen(id) + 1) < 0 ||
      rio_writen(c->fd, (void *)message, strlen(message) + 1) < 0)
    g_warning("Sending to fd: %d; id: %s; message: %s;", c->fd, id, message);
}

void window_destroy(GtkWidget *window, Client *c) {
  debug_print("window destroying %p\n", c);
  if (c->container != NULL)
    gtk_widget_destroy(c->container);
  c->container = NULL;
  send_to_lisp(c, "webkit--close", "");
}

static GtkFixed *find_fixed_widget(GList *widgets) {
  for (GList *l = widgets; l != NULL; l = l->next) {
    debug_print("widget %p; fixed %d; type %s\n", l->data,
                GTK_IS_FIXED(l->data), G_OBJECT_TYPE_NAME(l->data));
    if (GTK_IS_FIXED(l->data))
      return l->data;
    if (GTK_IS_BOX(l->data)) {
      GtkFixed *fixed =
          find_fixed_widget(gtk_container_get_children(GTK_CONTAINER(l->data)));
      if (fixed != NULL)
        return fixed;
    }
  }
  return NULL;
}

static GtkFixed *find_focused_fixed_widget() {
  GList *widgets = gtk_window_list_toplevels();
  for (GList *l = widgets; l != NULL; l = l->next) {
    debug_print("window %p focused %d\n", l->data,
                gtk_window_has_toplevel_focus(l->data));
    if (gtk_window_has_toplevel_focus(l->data))
      return find_fixed_widget(
          gtk_container_get_children(GTK_CONTAINER(l->data)));
  }
  return NULL;
}

int container_child_prop_helper(GtkWidget *container, gpointer child,
                                const char *prop) {
  GValue v = G_VALUE_INIT;
  g_value_init(&v, G_TYPE_INT);
  gtk_container_child_get_property(GTK_CONTAINER(container), GTK_WIDGET(child),
                                   prop, &v);
  return g_value_get_int(&v);
}

static void client_change_container(Client *c, GtkFixed *fixed) {
  debug_print("c %p change_container from %p to %p\n", c, c->container, fixed);
  if (c->container != NULL)
    gtk_container_remove(GTK_CONTAINER(c->container), GTK_WIDGET(c->view));
  c->container = GTK_WIDGET(fixed);

  /* play nice with child frames (should webkit always go under child frames?)
   */
  GList *widgets = gtk_container_get_children(GTK_CONTAINER(c->container));

  gtk_fixed_put(GTK_FIXED(c->container), GTK_WIDGET(c->view), 0, 0);
  // gtk_container_add (GTK_CONTAINER (c->container), GTK_WIDGET (c->view));

  for (GList *l = widgets; l != NULL; l = l->next) {
    int x = container_child_prop_helper(c->container, l->data, "x");
    int y = container_child_prop_helper(c->container, l->data, "y");
    debug_print("c %p removing child with x: %d, y: %d\n", c, x, y);
    g_object_ref(l->data);
    gtk_container_remove(GTK_CONTAINER(c->container), GTK_WIDGET(l->data));
    gtk_fixed_put(GTK_FIXED(c->container), GTK_WIDGET(l->data), x, y);
    g_object_unref(l->data);
  }
}

static emacs_value client_destroy(emacs_env *env, ptrdiff_t n,
                                  emacs_value *args, void *ptr) {
  Client *c = (Client *)env->get_user_ptr(env, args[0]);
  debug_print("c %p webkit_destroy\n", c);
  if (c != NULL) {
    if (GTK_IS_WINDOW(c->container))
      gtk_widget_destroy(c->container);

    c->container = NULL;
  }
  return Qnil;
}

static void client_free(void *ptr) {
  debug_print("c %p client_free\n", ptr);
  Client *c = (Client *)ptr;
  assert(c->container == NULL);
  gtk_widget_destroy(GTK_WIDGET(c->view));
  g_object_unref(c->view);
  free(c);
}

static emacs_value client_move_to_frame(emacs_env *env, ptrdiff_t n,
                                        emacs_value *args, void *ptr) {
  Client *c = (Client *)env->get_user_ptr(env, args[0]);
  intmax_t window_id = env->extract_integer(env, args[1]);
#ifdef DEBUG
  // print_widget_tree (gtk_window_list_toplevels());
#endif
  debug_print("c %p move_to_frame %p\n", c, (void *)window_id);
  if (c != NULL) {
    // webkit_move_to_focused_frame_internal (c, env);
    GList *widgets = gtk_window_list_toplevels();
    for (GList *l = widgets; l != NULL; l = l->next) {
      debug_print("window %p focused %d\n", l->data,
                  gtk_window_has_toplevel_focus(l->data));
      if (l->data == (void *)window_id) {
        GtkFixed *fixed = find_fixed_widget(
            gtk_container_get_children(GTK_CONTAINER(l->data)));
        if (fixed != NULL) {
          client_change_container(c, fixed);
          return Qnil;
        }
      }
    }
  }
  env->non_local_exit_signal(env,
                             env->intern(env, "webkit-module-no-fixed-widget"),
                             env->intern(env, "nil"));
  return Qnil;
}
static emacs_value client_show(emacs_env *env, ptrdiff_t n, emacs_value *args,
                               void *ptr) {
  Client *c = (Client *)env->get_user_ptr(env, args[0]);
  if (c != NULL)
    gtk_widget_show(GTK_WIDGET(c->view));
  return Qnil;
}

static emacs_value client_hide(emacs_env *env, ptrdiff_t n, emacs_value *args,
                               void *ptr) {
  Client *c = (Client *)env->get_user_ptr(env, args[0]);
  if (c != NULL)
    gtk_widget_hide(GTK_WIDGET(c->view));
  return Qnil;
}

static emacs_value client_resize(emacs_env *env, ptrdiff_t n, emacs_value *args,
                                 void *ptr) {
  Client *c = (Client *)env->get_user_ptr(env, args[0]);
  int x = env->extract_integer(env, args[1]);
  int y = env->extract_integer(env, args[2]);
  int w = env->extract_integer(env, args[3]);
  int h = env->extract_integer(env, args[4]);

  debug_print("c %p resize (x:%d y:%d w:%d h:%d)\n", c, x, y, w, h);
  if ((env->non_local_exit_check(env) == emacs_funcall_exit_return) &&
      (c != NULL)) {
    if (GTK_IS_FIXED(c->container))
      gtk_fixed_move(GTK_FIXED(c->container), GTK_WIDGET(c->view), x, y);
    else if (GTK_IS_WINDOW(c->container))
      gtk_window_move(GTK_WINDOW(c->container), x, y);
    else
      assert(0);
    gtk_widget_set_size_request(GTK_WIDGET(c->view), w, h);
  }
  return Qnil;
}

#endif // __FROM_WEBKIT_H_
