#ifndef __MCABBER_LAYOUT_H__
#define __MCABBER_LAYOUT_H__ 1

/* Screen layout: position and size of the main windows. */

#include <glib.h>

typedef struct {
  int y, x, h, w;
} rect_t;

typedef struct {
  int lines, cols;          // Terminal size
  int log_height;           // Requested log window height (0: hidden)
  int roster_width;         // Requested roster width (0: hidden)
  gboolean log_on_top;
  gboolean roster_on_right;
  int input_height;         // Requested input line height (>= 1)
  gboolean borders;         // Draw frames around the windows
} layout_opts_t;

typedef struct {
  rect_t roster, chat, chatstatus, log, mainstatus, input;
  // Frames (when borders is TRUE): rows of the horizontal lines and
  // columns of the vertical lines, -1 if there is none.  The chat status
  // line is drawn in the frame line above the chat window.
  gboolean borders;
  int frame_top, frame_mid, frame_bottom;
  int frame_left, frame_vsep, frame_right;
} ui_layout_t;

void layout_compute(const layout_opts_t *opts, ui_layout_t *l);

#endif /* __MCABBER_LAYOUT_H__ */

/* vim: set et cindent cinoptions=>2\:2(0 ts=2 sw=2:  For Vim users... */
