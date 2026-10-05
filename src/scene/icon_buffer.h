#pragma once

#include <filesystem>

struct wlr_buffer;

namespace umbriel {

  // Decodes a PNG icon into a `size`-pixel square buffer, aspect ratio kept and centered. Returns null when the
  // file cannot be decoded. The caller owns the buffer and drops it with wlr_buffer_drop.
  [[nodiscard]] wlr_buffer* loadIconBuffer(const std::filesystem::path& path, int size);

} // namespace umbriel
