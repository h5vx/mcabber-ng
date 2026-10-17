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

/* Without frames, from top to bottom:
 *   chat area: roster and chat column (chat window, chat status line,
 *              and the input line when input_in_chat is set)
 *   log window
 *   main status line
 *   input line (when input_in_chat is not set)
 * The log window can be above the chat area, the roster on the right.
 *
 * With frames (input_in_chat):
 *   ╭─ Roster ─┬─ chat status ─────────┬─ Members ─╮
 *   │ roster   │ chat                  │ members   │
 *   │          ├───────────────────────┴───────────┤
 *   │          │ input                             │
 *   ├─ Log ────┴───────────────────────────────────┤
 *   │ log                                          │
 *   ╰──────────────────────────────────────────────╯
 *    main status line
 */

#include "layout.h"

static void set_rect(rect_t *r, int y, int x, int h, int w)
{
  r->y = y;
  r->x = x;
  r->h = MAX(h, 0);
  r->w = MAX(w, 0);
}

//  compute(opts, layout, borders)
// Returns FALSE if the terminal is too small for this layout.
static gboolean compute(const layout_opts_t *o, ui_layout_t *l, gboolean b)
{
  int lines = MAX(o->lines, 4);
  int cols = MAX(o->cols, 1);
  gboolean in_chat = o->input_in_chat;
  int avail = lines - 1;          // All the rows but the main status line
  int frames = b ? 2 : 0;         // Top and bottom frame lines
  int status_row = b ? 0 : 1;     // Chat status line (in a frame if b)
  int isep = (b && in_chat) ? 1 : 0;
  int input_h, log_h, chat_h, log_rows, y;
  int inner_x, inner_w, roster_w, col_x, col_w, members_w;
  int roster_x, chat_x, chat_w, members_x;

  l->borders = b;
  l->frame_top = l->frame_mid = l->frame_bottom = l->frame_isep = -1;
  l->frame_left = l->frame_vsep = l->frame_right = l->frame_msep = -1;

  // Rows.  The chat window has at least one line; the input line is
  // reduced first, then the log window.
  input_h = CLAMP(o->input_height, 1,
                  MAX(avail - frames - status_row - isep - 1, 1));
  if (avail - frames - status_row - isep - input_h < 1)
    return FALSE;
  log_h = CLAMP(o->log_height, 0,
                avail - frames - status_row - isep - input_h - 1 - (b ? 1:0));
  log_rows = log_h ? log_h + (b ? 1 : 0) : 0;
  chat_h = avail - frames - status_row - isep - input_h - log_rows;
  if (chat_h < 1)
    return FALSE;

  // Columns
  inner_x = b ? 1 : 0;
  inner_w = b ? cols - 2 : cols;
  if (inner_w < 3)
    return FALSE;
  roster_w = CLAMP(o->roster_width, 0, inner_w - 2);
  col_w = inner_w - roster_w - (b && roster_w ? 1 : 0);
  if (o->roster_on_right) {
    col_x = inner_x;
    roster_x = col_x + col_w + (b && roster_w ? 1 : 0);
    if (b && roster_w)
      l->frame_vsep = col_x + col_w;
  } else {
    roster_x = inner_x;
    col_x = inner_x + roster_w + (b && roster_w ? 1 : 0);
    if (b && roster_w)
      l->frame_vsep = inner_x + roster_w;
  }
  members_w = CLAMP(o->members_width, 0, col_w / 2);
  chat_w = col_w - members_w - (b && members_w ? 1 : 0);
  if (o->members_on_left) {
    members_x = col_x;
    chat_x = col_x + members_w + (b && members_w ? 1 : 0);
    if (b && members_w)
      l->frame_msep = col_x + members_w;
  } else {
    chat_x = col_x;
    members_x = col_x + chat_w + (b && members_w ? 1 : 0);
    if (b && members_w)
      l->frame_msep = chat_x + chat_w;
  }
  if (b) {
    l->frame_left = 0;
    l->frame_right = cols - 1;
  }

  // Rows, from the top
  y = 0;
  if (b)
    l->frame_top = y++;
  if (o->log_on_top && log_h) {
    set_rect(&l->log, y, inner_x, log_h, inner_w);
    y += log_h;
    if (b)
      l->frame_mid = y++;
  }
  // Chat area
  {
    int area_y = y;
    int area_h;

    if (!b && !in_chat && o->log_on_top) {
      // The chat status line is above the chat window, full width
      set_rect(&l->chatstatus, y++, 0, 1, cols);
      area_y = y;
    }
    set_rect(&l->chat, y, chat_x, chat_h, chat_w);
    set_rect(&l->members, y, members_x, chat_h, members_w);
    if (b)
      set_rect(&l->chatstatus, y - 1, chat_x + 1, 1, MAX(chat_w - 2, 1));
    y += chat_h;
    if (!b && (in_chat || !o->log_on_top)) {
      if (in_chat)
        set_rect(&l->chatstatus, y++, col_x, 1, col_w);
      else
        set_rect(&l->chatstatus, y++, 0, 1, cols);
    }
    if (in_chat) {
      if (b)
        l->frame_isep = y++;
      set_rect(&l->input, y, col_x, input_h, col_w);
      y += input_h;
    }
    area_h = y - area_y;
    if (!b && !in_chat && !o->log_on_top)
      area_h--;     // The chat status line is below the roster
    set_rect(&l->roster, area_y, roster_x, area_h, roster_w);
  }
  if (!o->log_on_top && log_h) {
    if (b)
      l->frame_mid = y++;
    set_rect(&l->log, y, inner_x, log_h, inner_w);
    y += log_h;
  }
  if (!log_h)
    set_rect(&l->log, y, inner_x, 0, inner_w);
  if (b)
    l->frame_bottom = y++;
  set_rect(&l->mainstatus, y++, 0, 1, cols);
  if (!in_chat)
    set_rect(&l->input, y, 0, input_h, cols);
  return TRUE;
}

void layout_compute(const layout_opts_t *o, ui_layout_t *l)
{
  if (o->borders && compute(o, l, TRUE))
    return;
  if (compute(o, l, FALSE))
    return;
  // Tiny terminal: use the minimal layout
  {
    layout_opts_t small = *o;
    small.log_height = 0;
    small.input_height = 1;
    small.members_width = 0;
    small.lines = MAX(o->lines, 4);
    if (!compute(&small, l, FALSE)) {
      small.input_in_chat = FALSE;
      compute(&small, l, FALSE);
    }
  }
}

/* vim: set et cindent cinoptions=>2\:2(0 ts=2 sw=2:  For Vim users... */
