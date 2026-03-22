/**
 * Minimal Pango stub for headless/WASM builds.
 * Only defines types needed to compile headers that reference Pango,
 * without any actual rendering functionality.
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* Basic glib types used by pango headers */
typedef char   gchar;
typedef int    gint;
typedef unsigned int guint;
typedef void*  gpointer;

/* Opaque layout/context objects — forward declarations only */
typedef struct _PangoContext  PangoContext;
typedef struct _PangoLayout   PangoLayout;

/* Rectangle in Pango units (1/PANGO_SCALE of a pixel) */
typedef struct {
    gint x, y, width, height;
} PangoRectangle;

/* Ellipsization mode */
typedef enum {
    PANGO_ELLIPSIZE_NONE,
    PANGO_ELLIPSIZE_START,
    PANGO_ELLIPSIZE_MIDDLE,
    PANGO_ELLIPSIZE_END
} PangoEllipsizeMode;

/* Text alignment */
typedef enum {
    PANGO_ALIGN_LEFT,
    PANGO_ALIGN_CENTER,
    PANGO_ALIGN_RIGHT
} PangoAlignment;

/* Wrap mode */
typedef enum {
    PANGO_WRAP_WORD,
    PANGO_WRAP_CHAR,
    PANGO_WRAP_WORD_CHAR
} PangoWrapMode;

/* Scale factor: Pango units per pixel */
#define PANGO_SCALE 1024

/* Font weight */
typedef enum {
    PANGO_WEIGHT_THIN       = 100,
    PANGO_WEIGHT_ULTRALIGHT = 200,
    PANGO_WEIGHT_LIGHT      = 300,
    PANGO_WEIGHT_SEMILIGHT  = 350,
    PANGO_WEIGHT_BOOK       = 380,
    PANGO_WEIGHT_NORMAL     = 400,
    PANGO_WEIGHT_MEDIUM     = 500,
    PANGO_WEIGHT_SEMIBOLD   = 600,
    PANGO_WEIGHT_BOLD       = 700,
    PANGO_WEIGHT_ULTRABOLD  = 800,
    PANGO_WEIGHT_HEAVY      = 900,
    PANGO_WEIGHT_ULTRAHEAVY = 1000
} PangoWeight;

/* Font style */
typedef enum {
    PANGO_STYLE_NORMAL,
    PANGO_STYLE_OBLIQUE,
    PANGO_STYLE_ITALIC
} PangoStyle;

/* Layout line (opaque) */
typedef struct _PangoLayoutLine PangoLayoutLine;

/* Layout iteration and line functions */
static inline int   pango_layout_get_line_count(PangoLayout* /*layout*/)         { return 0; }
static inline void  pango_layout_set_line_spacing(PangoLayout* /*layout*/, float /*factor*/) {}
static inline PangoLayoutLine* pango_layout_get_line_readonly(PangoLayout* /*layout*/, int /*index*/) { return (PangoLayoutLine*)0; }
static inline void  pango_layout_line_ref(PangoLayoutLine* /*line*/)              {}
static inline void  pango_layout_line_unref(PangoLayoutLine* /*line*/)            {}
static inline void  pango_layout_line_get_extents(PangoLayoutLine* /*line*/, PangoRectangle* /*ink*/, PangoRectangle* /*logical*/) {}

#ifdef __cplusplus
} // extern "C"
#endif
