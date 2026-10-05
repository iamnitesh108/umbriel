#pragma once

#include <cairo.h>

extern "C" {
#include <wlr/interfaces/wlr_buffer.h>
}

namespace umbriel {

  // A wlr_buffer over a Cairo ARGB32 image surface it owns, freed when the buffer is dropped.
  struct CairoBuffer {
    wlr_buffer base;
    cairo_surface_t* surface = nullptr;
  };

  [[nodiscard]] CairoBuffer* createCairoBuffer(int width, int height);

} // namespace umbriel
