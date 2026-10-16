/*
 * layout.c     -- Screen layout computation
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

#include "layout.h"

static void set_rect(rect_t *r, int y, int x, int h, int w)
{
  r->y = y;
  r->x = x;
  r->h = MAX(h, 0);
  r->w = MAX(w, 0);
}

//  layout_compute(opts, layout)
// From top to bottom: chat area (roster + chat window), chat status line,
// log window, main status line, input line.  The log window can be put
// above the chat area and the roster on the right.
// The chat area always has at least one line: the input line and the log
// window are reduced if needed.
void layout_compute(const layout_opts_t *o, ui_layout_t *l)
{
  int lines = MAX(o->lines, 4);
  int cols = MAX(o->cols, 1);
  int input_h, log_h, chat_h, roster_w, chat_y, chatstatus_y, log_y;

  // Fixed lines: the two status lines and at least one chat line
  input_h = CLAMP(o->input_height, 1, lines - 3);
  log_h = CLAMP(o->log_height, 0, lines - 3 - input_h);
  chat_h = lines - 2 - input_h - log_h;

  roster_w = CLAMP(o->roster_width, 0, cols - 1);

  if (o->log_on_top) {
    log_y = 0;
    chatstatus_y = log_h;
    chat_y = log_h + 1;
  } else {
    chat_y = 0;
    chatstatus_y = chat_h;
    log_y = chat_h + 1;
  }

  if (o->roster_on_right) {
    set_rect(&l->roster, chat_y, cols - roster_w, chat_h, roster_w);
    set_rect(&l->chat, chat_y, 0, chat_h, cols - roster_w);
  } else {
    set_rect(&l->roster, chat_y, 0, chat_h, roster_w);
    set_rect(&l->chat, chat_y, roster_w, chat_h, cols - roster_w);
  }
  set_rect(&l->chatstatus, chatstatus_y, 0, 1, cols);
  set_rect(&l->log, log_y, 0, log_h, cols);
  set_rect(&l->mainstatus, lines - input_h - 1, 0, 1, cols);
  set_rect(&l->input, lines - input_h, 0, input_h, cols);
}

/* vim: set et cindent cinoptions=>2\:2(0 ts=2 sw=2:  For Vim users... */
