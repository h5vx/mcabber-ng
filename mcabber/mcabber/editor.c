/*
 * editor.c         -- Compose the input line in an external editor
 *
 * Copyright (C) 2026 mcabber-ng contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

// The input line is written to a temporary file, edited, and read back.
// In tmux, the editor runs in another pane (or window, or popup) and
// mcabber-ng keeps running: "tmux wait-for" tells when it is closed.
// Otherwise the screen is suspended while the editor runs in the terminal.
//
// The editor runs in a small shell script that removes the file when the
// editor fails (e.g. ":cq" in Vim): the input line is then left unchanged.
// The exit status of the processes is not used, because the SIGCHLD
// handler of mcabber-ng reaps every child.

#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <sys/wait.h>
#include <glib.h>
#include <glib/gstdio.h>

#include "editor.h"
#include "screen.h"
#include "settings.h"
#include "logprint.h"

static gchar *tmux_file;      // File being edited in tmux
static GPid tmux_waiter;      // The "tmux wait-for" process
static guint tmux_watch;

static const char *editor_command(void)
{
  const char *e = settings_opt_get("editor");
  if (!e || !*e)
    e = g_getenv("VISUAL");
  if (!e || !*e)
    e = g_getenv("EDITOR");
  if (!e || !*e)
    e = "vi";
  return e;
}

//  write_input()
// Write the input line to a new temporary file; returns its path.
static gchar *write_input(void)
{
  const char *text = scr_get_inputline();
  GError *err = NULL;
  gchar *path = NULL;
  gchar *data;
  int fd;

  fd = g_file_open_tmp("mcabber-ng-XXXXXX.txt", &path, &err);
  if (fd < 0) {
    scr_LogPrint(LPRINT_NORMAL, "Editor: %s", err->message);
    g_error_free(err);
    return NULL;
  }
  close(fd);
  // Editors expect a final newline
  data = *text ? g_strconcat(text, "\n", NULL) : g_strdup("");
  if (!g_file_set_contents(path, data, -1, &err)) {
    scr_LogPrint(LPRINT_NORMAL, "Editor: %s", err->message);
    g_error_free(err);
    g_unlink(path);
    g_free(path);
    path = NULL;
  }
  g_free(data);
  return path;
}

//  read_input(path)
// Put the edited text in the input line.
static void read_input(const char *path)
{
  gchar *text;
  gsize len;

  if (!g_file_get_contents(path, &text, &len, NULL)) {
    scr_LogPrint(LPRINT_NORMAL, "Editor: cancelled, the input line has not "
                 "been changed.");
    return;
  }
  g_unlink(path);
  len = strlen(text);   // Ignore what follows a NUL byte
  while (len && (text[len-1] == '\n' || text[len-1] == '\r'))
    text[--len] = '\0';
  scr_set_inputline(text);
  g_free(text);
}

//  shell_script(tmux)
// The command run by sh -c: $1 is the file; in tmux, $2 is the channel to
// signal and $3 is the pane to return to.
static gchar *shell_script(gboolean tmux)
{
  const char *editor = editor_command();
  if (!tmux)
    return g_strdup_printf("%s \"$1\" || rm -f \"$1\"", editor);
  return g_strdup_printf("%s \"$1\" || rm -f \"$1\"; "
                         "if [ -n \"$3\" ]; then "
                         "tmux select-window -t \"$3\"; "
                         "tmux select-pane -t \"$3\"; fi; "
                         "tmux wait-for -S \"$2\"", editor);
}

/* -- In the terminal ----------------------------------------------------- */

static void edit_here(const char *path)
{
  gchar *script = shell_script(FALSE);
  struct sigaction ign, dfl, old_int, old_quit, old_chld;
  pid_t pid;
  int status;

  memset(&ign, 0, sizeof(ign));
  ign.sa_handler = SIG_IGN;
  memset(&dfl, 0, sizeof(dfl));
  dfl.sa_handler = SIG_DFL;

  scr_suspend();
  // Ctrl-C goes to the editor; we wait for the editor ourselves
  sigaction(SIGINT, &ign, &old_int);
  sigaction(SIGQUIT, &ign, &old_quit);
  sigaction(SIGCHLD, &dfl, &old_chld);

  pid = fork();
  if (pid == 0) {
    signal(SIGINT, SIG_DFL);
    signal(SIGQUIT, SIG_DFL);
    execl("/bin/sh", "sh", "-c", script, "sh", path, (char *)NULL);
    _exit(127);
  }
  if (pid > 0)
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR)
      ;

  sigaction(SIGINT, &old_int, NULL);
  sigaction(SIGQUIT, &old_quit, NULL);
  sigaction(SIGCHLD, &old_chld, NULL);
  scr_resume();

  if (pid < 0)
    scr_LogPrint(LPRINT_NORMAL, "Editor: cannot fork: %s", g_strerror(errno));
  else if (pid > 0 && WIFEXITED(status) && WEXITSTATUS(status) == 127)
    scr_LogPrint(LPRINT_NORMAL, "Editor: cannot run \"%s\".",
                 editor_command());
  read_input(path);
  g_unlink(path);
  g_free(script);
}

/* -- In tmux ------------------------------------------------------------- */

static void tmux_reset(void)
{
  if (tmux_watch)
    g_source_remove(tmux_watch);
  tmux_watch = 0;
  if (tmux_waiter > 0)
    kill(tmux_waiter, SIGTERM);
  tmux_waiter = 0;
  if (tmux_file)
    g_unlink(tmux_file);
  g_free(tmux_file);
  tmux_file = NULL;
}

// "tmux wait-for" has exited (end of file on its output): the editor is
// closed.
static gboolean tmux_done(GIOChannel *channel, GIOCondition cond,
                          gpointer data)
{
  gchar *path = tmux_file;

  tmux_file = NULL;
  tmux_watch = 0;
  tmux_waiter = 0;
  g_io_channel_shutdown(channel, FALSE, NULL);
  read_input(path);
  g_unlink(path);
  g_free(path);
  return FALSE;
}

//  tmux_command(mode, path, channel)
// The tmux command that opens the editor, as an argument vector.
static gchar **tmux_command(const char *mode, const char *path,
                            const char *channel)
{
  GPtrArray *argv = g_ptr_array_new();
  const char *args = settings_opt_get("editor_tmux_args");
  const char *pane = g_getenv("TMUX_PANE");
  gchar *script = shell_script(TRUE), *cmd;
  gchar *q[5];
  gchar **extra = NULL;
  int i;

  // The commands target our pane (or its session), not the session that
  // tmux would consider current.
  g_ptr_array_add(argv, g_strdup("tmux"));
  if (!strcmp(mode, "window")) {
    const char *tmux = g_getenv("TMUX");
    const char *sid = tmux ? strrchr(tmux, ',') : NULL;
    g_ptr_array_add(argv, g_strdup("new-window"));
    if (sid) {    // $TMUX is "socket,pid,session"
      g_ptr_array_add(argv, g_strdup("-a"));
      g_ptr_array_add(argv, g_strdup("-t"));
      g_ptr_array_add(argv, g_strdup_printf("$%s:", sid + 1));
    }
  } else {
    if (!strcmp(mode, "popup")) {
      g_ptr_array_add(argv, g_strdup("display-popup"));
      g_ptr_array_add(argv, g_strdup("-E"));
      if (!args)
        args = "-w 80% -h 80%";
    } else {
      g_ptr_array_add(argv, g_strdup("split-window"));
      if (!args)
        args = "-h";
    }
    if (pane) {
      g_ptr_array_add(argv, g_strdup("-t"));
      g_ptr_array_add(argv, g_strdup(pane));
    }
    if (!strcmp(mode, "popup"))
      pane = NULL;    // The focus comes back by itself
  }
  if (args && *args && g_shell_parse_argv(args, NULL, &extra, NULL)) {
    for (i = 0; extra[i]; i++)
      g_ptr_array_add(argv, g_strdup(extra[i]));
    g_strfreev(extra);
  }

  // tmux runs the command with a shell
  q[0] = g_shell_quote(script);
  q[1] = g_shell_quote(path);
  q[2] = g_shell_quote(channel);
  q[3] = g_shell_quote(pane ? pane : "");
  q[4] = NULL;
  cmd = g_strdup_printf("sh -c %s sh %s %s %s", q[0], q[1], q[2], q[3]);
  for (i = 0; q[i]; i++)
    g_free(q[i]);
  g_ptr_array_add(argv, cmd);
  g_ptr_array_add(argv, NULL);
  g_free(script);
  return (gchar **)g_ptr_array_free(argv, FALSE);
}

static gboolean edit_tmux(const char *path, const char *mode)
{
  static guint counter;
  gchar *channel = g_strdup_printf("mcabber-ng-editor-%d-%u", (int)getpid(),
                                   ++counter);
  gchar *wait_argv[] = { "tmux", "wait-for", channel, NULL };
  gchar **argv = tmux_command(mode, path, channel);
  gchar *errout = NULL;
  GError *err = NULL;
  gboolean ok = FALSE;
  gint out = -1, status = 0;

  // Open the editor.  display-popup lasts as long as the popup, so it
  // runs in the background.
  if (!strcmp(mode, "popup")) {
    ok = g_spawn_async(NULL, argv, NULL,
                       G_SPAWN_SEARCH_PATH | G_SPAWN_STDOUT_TO_DEV_NULL |
                       G_SPAWN_STDERR_TO_DEV_NULL, NULL, NULL, NULL, &err);
  } else {
    // Our SIGCHLD handler would reap tmux before g_spawn_sync() does
    struct sigaction dfl, old_chld;
    memset(&dfl, 0, sizeof(dfl));
    dfl.sa_handler = SIG_DFL;
    sigaction(SIGCHLD, &dfl, &old_chld);
    ok = g_spawn_sync(NULL, argv, NULL,
                      G_SPAWN_SEARCH_PATH | G_SPAWN_STDOUT_TO_DEV_NULL,
                      NULL, NULL, NULL, &errout, &status, &err);
    sigaction(SIGCHLD, &old_chld, NULL);
    if (ok && !(WIFEXITED(status) && WEXITSTATUS(status) == 0)) {
      g_strstrip(errout);
      scr_LogPrint(LPRINT_NORMAL, "Editor: tmux failed: %s", errout);
      ok = FALSE;
    }
  }
  if (err) {
    scr_LogPrint(LPRINT_NORMAL, "Editor: cannot run tmux: %s", err->message);
    g_clear_error(&err);
  }
  g_strfreev(argv);
  g_free(errout);

  // Wait for the end of the editor (tmux remembers a signal sent before
  // anybody waits)
  if (ok) {
    ok = g_spawn_async_with_pipes(NULL, wait_argv, NULL, G_SPAWN_SEARCH_PATH,
                                  NULL, NULL, &tmux_waiter, NULL, &out, NULL,
                                  &err);
    if (ok) {
      GIOChannel *ch = g_io_channel_unix_new(out);
      g_io_channel_set_close_on_unref(ch, TRUE);
      tmux_watch = g_io_add_watch(ch, G_IO_IN | G_IO_HUP | G_IO_ERR,
                                  tmux_done, NULL);
      g_io_channel_unref(ch);
      tmux_file = g_strdup(path);
    } else {
      scr_LogPrint(LPRINT_NORMAL, "Editor: %s", err->message);
      g_error_free(err);
    }
  }
  g_free(channel);
  return ok;
}

/* -- /editor ------------------------------------------------------------- */

void editor_command_run(const char *arg)
{
  const char *mode = settings_opt_get("editor_tmux");
  gchar *path;

  if (arg && !strcmp(arg, "cancel")) {
    if (tmux_file) {
      tmux_reset();
      scr_LogPrint(LPRINT_NORMAL, "Editor: forgotten.");
    }
    return;
  }
  if (arg && *arg) {
    scr_LogPrint(LPRINT_NORMAL, "Usage: /editor [cancel]");
    return;
  }
  if (tmux_file) {
    scr_LogPrint(LPRINT_NORMAL, "Editor: already open (\"/editor cancel\" "
                 "to forget it).");
    return;
  }

  path = write_input();
  if (!path)
    return;
  if (!mode || !*mode)
    mode = "split";
  if (g_getenv("TMUX") && strcmp(mode, "off")) {
    if (strcmp(mode, "split") && strcmp(mode, "window") &&
        strcmp(mode, "popup")) {
      scr_LogPrint(LPRINT_NORMAL, "Editor: invalid editor_tmux value \"%s\".",
                   mode);
      mode = "split";
    }
    if (!edit_tmux(path, mode))
      g_unlink(path);
  } else {
    edit_here(path);
  }
  g_free(path);
}

void editor_deinit(void)
{
  tmux_reset();
}

/* vim: set et cindent cinoptions=>2\:2(0 ts=2 sw=2:  For Vim users... */
