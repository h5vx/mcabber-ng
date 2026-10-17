#ifndef __MCABBER_XMLSPLIT_H__
#define __MCABBER_XMLSPLIT_H__ 1

/* Helpers for the XMPP console: raw XML stream data. */

#include <glib.h>

//  xml_split_stanzas(data)
// Split stream data into top-level elements (stanzas, stream header...).
// Returns a NULL-terminated array (g_strfreev).
gchar **xml_split_stanzas(const char *data);

//  xml_mask_secrets(xml)
// Replace the content of the SASL <auth/>, <response/> and <password/>
// elements with "***".  Returns a newly allocated string.
gchar *xml_mask_secrets(const char *xml);

#endif /* __MCABBER_XMLSPLIT_H__ */
