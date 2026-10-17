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
  gboolean input_in_chat;   // The input line is below the chat window
                            // (else it is at the bottom of the screen)
  int members_width;        // Room members column (0: none)
  gboolean members_on_left; // ... on the left of the chat window
} layout_opts_t;

typedef struct {
  rect_t roster, chat, chatstatus, log, mainstatus, input, members;
  // Frames (when borders is TRUE): rows of the horizontal lines and
  // columns of the vertical lines, -1 if there is none.  The chat status
  // line is drawn in the frame line above the chat window.  frame_isep is
  // the line between the chat window and the input line, frame_msep the
  // line between the chat window and the members column.
  gboolean borders;
  int frame_top, frame_mid, frame_bottom, frame_isep;
  int frame_left, frame_vsep, frame_right, frame_msep;
} ui_layout_t;

void layout_compute(const layout_opts_t *opts, ui_layout_t *l);

#endif /* __MCABBER_LAYOUT_H__ */

/* vim: set et cindent cinoptions=>2\:2(0 ts=2 sw=2:  For Vim users... */
