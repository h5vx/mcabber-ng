#ifndef __MCABBER_INPUTLAYOUT_H__
#define __MCABBER_INPUTLAYOUT_H__ 1

/* Layout of the (multi-line) input buffer on the screen. */

#include <glib.h>

typedef struct {
  GArray *row_start;    // Byte offset of the beginning of each row (gsize)
  GArray *row_end;      // Byte offset of the end of each row (gsize)
  guint rows;
  guint cursor_row;
  guint cursor_col;     // In screen cells
} il_layout_t;

void il_layout_init(il_layout_t *l);
void il_layout_free(il_layout_t *l);

//  il_layout(buf, cursor, width, layout)
// Split the buffer into screen rows: rows end at '\n' or when the next
// character does not fit in "width" cells.  "cursor" is a byte offset.
void il_layout(const char *buf, gsize cursor, guint width, il_layout_t *l);

//  il_offset_at(buf, layout, row, col)
// Return the byte offset of the character displayed at (row, col), or of
// the end of the row if it is shorter.
gsize il_offset_at(const char *buf, const il_layout_t *l, guint row,
                   guint col);

#define il_row_start(l, i) g_array_index((l)->row_start, gsize, (i))
#define il_row_end(l, i)   g_array_index((l)->row_end, gsize, (i))

#endif /* __MCABBER_INPUTLAYOUT_H__ */

/* vim: set et cindent cinoptions=>2\:2(0 ts=2 sw=2:  For Vim users... */
