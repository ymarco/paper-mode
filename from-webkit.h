#include "paper-gtk.h"
#include <gtk/gtk.h>

#ifdef DEBUG
#define DEBUG_TEST 1
#else
#define DEBUG_TEST 0
#endif

#define debug_print(fmt, ...)                                           \
  do { if (DEBUG_TEST) fprintf(stderr, fmt, ##__VA_ARGS__); } while (0)

static ssize_t
rio_writen (int fd, void *usrbuf, size_t n)
{
  size_t nleft = n;
  ssize_t nwritten;
  char *bufp = usrbuf;

  while (nleft > 0) {
    if ((nwritten = write (fd, bufp, nleft)) <= 0) {
      if (errno == EINTR)  /* Interrupted by sig handler return */
        nwritten = 0;    /* and call write() again */
      else
        return -1;       /* errno set by write() */
    }
    nleft -= nwritten;
    bufp += nwritten;
  }
  return n;
}

static void
send_to_lisp (Client *c, const char *id, const char *message)
{
  if (id == NULL || message == NULL
      || rio_writen (c->fd, (void *)id, strlen (id)+1) < 0
      || rio_writen (c->fd, (void *)message, strlen (message)+1) < 0)
    g_warning ("Sending to fd: %d; id: %s; message: %s;", c->fd, id, message);
}

void
window_destroy (GtkWidget *window, Client *c)
{
  debug_print ("window destroying %p\n", c);
  if (c->container != NULL)
    gtk_widget_destroy (c->container);
  c->container = NULL;
  send_to_lisp (c, "webkit--close", "");
}
