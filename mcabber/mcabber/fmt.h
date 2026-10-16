#ifndef __MCABBER_FMT_H__
#define __MCABBER_FMT_H__ 1

/* Templates with styles, used to format messages, roster items and status
 * lines.
 *
 *   {field}            value of a field
 *   {field:SPEC}       aligned/truncated value; SPEC is [<>^][width][.max]
 *                      ("<" left, ">" right, "^" center), or "*" to use the
 *                      remaining space (the text is truncated with "…")
 *   {field:ARG}        any other SPEC is an argument for the field, like
 *                      {time:%H:%M}
 *   {?field}...{/}     the text is used only if the field is set (not
 *                      empty, not "0");  {!field}...{/} if it is not set
 *   {#style}           following text uses this style;  {#} default style.
 *                      "$field" in the name is replaced by the value of the
 *                      field: {#status_$status}
 *   {>}                the following text is aligned on the right
 *   {{ }}              literal braces
 */

#include <glib.h>

/* -- Styled strings -- */

typedef struct {
  guint start, len;     // Bytes
  const char *style;    // Interned style name, NULL for the default style
} srun_t;

typedef struct {
  GString *text;
  GArray *runs;         // srun_t
  guint width;          // Screen cells
} sstr_t;

sstr_t *sstr_new(void);
void    sstr_free(sstr_t *s);
void    sstr_clear(sstr_t *s);
void    sstr_append(sstr_t *s, const char *text, gssize len,
                    const char *style);
guint   fmt_text_width(const char *text, gssize len);

/* -- Templates -- */

typedef struct fmt fmt_t;

// Return the value of a field (newly allocated), or NULL if it is not set
typedef gchar *(*fmt_getter_t)(const char *name, const char *arg,
                               gpointer data);

fmt_t  *fmt_compile(const char *tmpl, GError **err);
void    fmt_free(fmt_t *f);

//  fmt_render(f, getter, data, width, out)
// Render the template into out (which is not cleared).  If width is not
// zero, flexible fields ("*") and right-aligned text ("{>}") use it as the
// total width of the result.
void    fmt_render(const fmt_t *f, fmt_getter_t getter, gpointer data,
                   guint width, sstr_t *out);

#define FMT_ERROR (fmt_error_quark())
GQuark  fmt_error_quark(void);

#endif /* __MCABBER_FMT_H__ */

/* vim: set et cindent cinoptions=>2\:2(0 ts=2 sw=2:  For Vim users... */
