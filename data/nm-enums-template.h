/*** BEGIN file-header ***/

#include <glib-object.h>

G_BEGIN_DECLS
/*** END file-header ***/

/*** BEGIN file-production ***/

/* enumerations from "@filename@" */
/*** END file-production ***/

/*** BEGIN value-header ***/
GType @enum_name@_get_type (void) G_GNUC_CONST;
#define @ENUMPREFIX@_TYPE_@ENUMSHORT@ (@enum_name@_get_type ())

/* Define type-specific symbols */
#undef __NM_IS_ENUM__
#undef __NM_IS_FLAGS__
#define __NM_IS_@TYPE@__

#if defined __NM_IS_ENUM__
const gchar *@enum_name@_get_string (int val);
#endif

#if defined __NM_IS_FLAGS__
gchar *@enum_name@_build_string_from_mask (int mask);
#endif

gboolean @enum_name@_get_value (const gchar *str, int *out_val);

/*** END value-header ***/

/*** BEGIN file-tail ***/
G_END_DECLS

/*** END file-tail ***/
