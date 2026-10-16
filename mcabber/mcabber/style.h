#ifndef __MCABBER_STYLE_H__
#define __MCABBER_STYLE_H__ 1

/* Styles: colors and attributes.
 *
 * A style is written like "fg=#c0caf5 bg=default bold", "brightred",
 * "white on blue underline" or "fg=208 italic".
 * Colors: default, black, red, green, yellow, blue, magenta, cyan, white
 * (with an optional "bright" prefix), 0-255 (terminal palette) or #rrggbb.
 * Attributes: bold, dim, italic, underline, reverse, blink, standout.
 */

#include <glib.h>

typedef enum {
  STYLE_COLOR_UNSET = 0,    // Not specified
  STYLE_COLOR_DEFAULT,      // Terminal default color
  STYLE_COLOR_INDEX,        // Palette color (0-255)
  STYLE_COLOR_RGB           // 0xRRGGBB
} style_color_kind_t;

typedef struct {
  style_color_kind_t kind;
  guint32 value;
} style_color_t;

#define STYLE_BOLD      (1U<<0)
#define STYLE_DIM       (1U<<1)
#define STYLE_ITALIC    (1U<<2)
#define STYLE_UNDERLINE (1U<<3)
#define STYLE_REVERSE   (1U<<4)
#define STYLE_BLINK     (1U<<5)
#define STYLE_STANDOUT  (1U<<6)

typedef struct {
  style_color_t fg, bg;
  guint attrs;
} style_spec_t;

#define STYLE_ERROR (style_error_quark())
GQuark   style_error_quark(void);

gboolean style_parse_color(const char *s, style_color_t *c, GError **err);
gboolean style_parse(const char *spec, style_spec_t *out, GError **err);

// RGB value of a palette color (xterm 256 colors)
guint32  style_palette_rgb(guint index);
// Nearest palette color: in the 256 colors (16-255 only) or the 16 colors
guint    style_rgb_to_256(guint32 rgb);
guint    style_rgb_to_16(guint32 rgb);

#endif /* __MCABBER_STYLE_H__ */

/* vim: set et cindent cinoptions=>2\:2(0 ts=2 sw=2:  For Vim users... */
