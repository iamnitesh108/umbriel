#pragma once

#include <filesystem>

struct wlr_buffer;

namespace umbriel {

  // Decodes a PNG, or an SVG where supported, into a `size` x `size` buffer, centered with its aspect kept. Null on
  // failure; the caller drops the buffer with wlr_buffer_drop.
  [[nodiscard]] wlr_buffer* loadIconBuffer(const std::filesystem::path& path, int size);

} // namespace umbriel
