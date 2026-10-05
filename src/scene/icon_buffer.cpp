#include "scene/icon_buffer.h"

#include "scene/cairo_buffer.h"

#include <algorithm>
#include <cairo.h>

namespace umbriel {

  wlr_buffer* loadIconBuffer(const std::filesystem::path& path, int size) {
    if (size <= 0) {
      return nullptr;
    }
    cairo_surface_t* image = cairo_image_surface_create_from_png(path.c_str());
    const int width = cairo_image_surface_get_width(image);
    const int height = cairo_image_surface_get_height(image);
    if (cairo_surface_status(image) != CAIRO_STATUS_SUCCESS || width <= 0 || height <= 0) {
      cairo_surface_destroy(image);
      return nullptr;
    }

    CairoBuffer* buffer = createCairoBuffer(size, size);
    const double scale = static_cast<double>(size) / std::max(width, height);
    cairo_t* cr = cairo_create(buffer->surface);
    cairo_translate(cr, (size - (width * scale)) / 2.0, (size - (height * scale)) / 2.0);
    cairo_scale(cr, scale, scale);
    cairo_set_source_surface(cr, image, 0, 0);
    cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_GOOD);
    cairo_paint(cr);
    cairo_destroy(cr);
    cairo_surface_destroy(image);
    cairo_surface_flush(buffer->surface);
    return &buffer->base;
  }

} // namespace umbriel
