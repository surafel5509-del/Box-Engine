#pragma once

#include <string>

#include <android/asset_manager.h>

namespace box::renderer {

/** Reads a text asset, preferring a writable app-private override for development hot reload. */
[[nodiscard]] std::string readTextAsset(AAssetManager* assets,
                                        const std::string& relativePath,
                                        const std::string& overrideDirectory);

}  // namespace box::renderer
