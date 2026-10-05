#include "scene/cairo_buffer.h"

#include <cstddef>
#include <cstdint>
#include <drm_fourcc.h>

namespace umbriel {

  namespace {

    void cairoBufferDestroy(wlr_buffer* wlrBuf) {
      CairoBuffer* buf;
      buf = wl_container_of(wlrBuf, buf, base);
      cairo_surface_destroy(buf->surface);
      delete buf;
    }

    bool cairoBufferBeginDataPtrAccess(
        wlr_buffer* wlrBuf, uint32_t /*flags*/, void** data, uint32_t* format, size_t* stride
    ) {
      CairoBuffer* buf;
      buf = wl_container_of(wlrBuf, buf, base);
      *data = cairo_image_surface_get_data(buf->surface);
      *format = DRM_FORMAT_ARGB8888;
      *stride = static_cast<size_t>(cairo_image_surface_get_stride(buf->surface));
      return true;
    }

    void cairoBufferEndDataPtrAccess(wlr_buffer* /*wlrBuf*/) {}

    const wlr_buffer_impl kCairoBufferImpl = {
        .destroy = cairoBufferDestroy,
        .get_dmabuf = nullptr,
        .get_shm = nullptr,
        .begin_data_ptr_access = cairoBufferBeginDataPtrAccess,
        .end_data_ptr_access = cairoBufferEndDataPtrAccess,
    };

  } // namespace

  CairoBuffer* createCairoBuffer(int width, int height) {
    auto* buf = new CairoBuffer;
    buf->surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
    wlr_buffer_init(&buf->base, &kCairoBufferImpl, width, height);
    return buf;
  }

} // namespace umbriel
