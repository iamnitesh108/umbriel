#include "scene/icon_buffer.h"

#include "scene/cairo_buffer.h"

#include <algorithm>
#include <cairo.h>
#ifdef UMBRIEL_SVG_ICONS
#include <cstdint>
#include <cstring>
#include <nanosvg.h>
#include <nanosvgrast.h>
#include <vector>
#endif

namespace umbriel {

  namespace {

    bool loadPng(CairoBuffer& buffer, const std::filesystem::path& path, int size) {
      cairo_surface_t* image = cairo_image_surface_create_from_png(path.c_str());
      const int width = cairo_image_surface_get_width(image);
      const int height = cairo_image_surface_get_height(image);
      if (cairo_surface_status(image) != CAIRO_STATUS_SUCCESS || width <= 0 || height <= 0) {
        cairo_surface_destroy(image);
        return false;
      }
      const double scale = static_cast<double>(size) / std::max(width, height);
      cairo_t* cr = cairo_create(buffer.surface);
      cairo_translate(cr, (size - (width * scale)) / 2.0, (size - (height * scale)) / 2.0);
      cairo_scale(cr, scale, scale);
      cairo_set_source_surface(cr, image, 0, 0);
      cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_GOOD);
      cairo_paint(cr);
      cairo_destroy(cr);
      cairo_surface_destroy(image);
      return true;
    }

#ifdef UMBRIEL_SVG_ICONS
    bool loadSvg(CairoBuffer& buffer, const std::filesystem::path& path, int size) {
      NSVGimage* image = nsvgParseFromFile(path.c_str(), "px", 96.0F);
      if (image == nullptr) {
        return false;
      }
      NSVGrasterizer* rasterizer = image->width > 0 && image->height > 0 ? nsvgCreateRasterizer() : nullptr;
      if (rasterizer == nullptr) {
        nsvgDelete(image);
        return false;
      }
      const float scale = static_cast<float>(size) / std::max(image->width, image->height);
      const float offsetX = (static_cast<float>(size) - (image->width * scale)) / 2.0F;
      const float offsetY = (static_cast<float>(size) - (image->height * scale)) / 2.0F;
      std::vector<uint8_t> rgba(static_cast<size_t>(size) * size * 4);
      nsvgRasterize(rasterizer, image, offsetX, offsetY, scale, rgba.data(), size, size, size * 4);
      nsvgDeleteRasterizer(rasterizer);
      nsvgDelete(image);

      // nanosvg writes straight-alpha RGBA bytes; Cairo wants premultiplied native-endian ARGB words.
      uint8_t* data = cairo_image_surface_get_data(buffer.surface);
      const int stride = cairo_image_surface_get_stride(buffer.surface);
      for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
          const uint8_t* source = &rgba[((static_cast<size_t>(y) * size) + x) * 4];
          const uint32_t alpha = source[3];
          const auto premultiply = [alpha](uint32_t channel) { return ((channel * alpha) + 127) / 255; };
          const uint32_t pixel =
              (alpha << 24) | (premultiply(source[0]) << 16) | (premultiply(source[1]) << 8) | premultiply(source[2]);
          std::memcpy(data + (static_cast<ptrdiff_t>(y) * stride) + (static_cast<ptrdiff_t>(x) * 4), &pixel, 4);
        }
      }
      cairo_surface_mark_dirty(buffer.surface);
      return true;
    }
#endif

  } // namespace

  wlr_buffer* loadIconBuffer(const std::filesystem::path& path, int size) {
    if (size <= 0) {
      return nullptr;
    }
    CairoBuffer* buffer = createCairoBuffer(size, size);
#ifdef UMBRIEL_SVG_ICONS
    const bool loaded = path.extension() == ".svg" ? loadSvg(*buffer, path, size) : loadPng(*buffer, path, size);
#else
    const bool loaded = loadPng(*buffer, path, size);
#endif
    if (!loaded) {
      wlr_buffer_drop(&buffer->base);
      return nullptr;
    }
    cairo_surface_flush(buffer->surface);
    return &buffer->base;
  }

} // namespace umbriel
