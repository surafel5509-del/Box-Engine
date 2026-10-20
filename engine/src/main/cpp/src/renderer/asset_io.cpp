#include "box/renderer/asset_io.hpp"

#include <android/asset_manager.h>

#include <fstream>
#include <iterator>
#include <string>

#include "box/core/log.hpp"

namespace box::renderer {
namespace {

[[nodiscard]] std::string readOverrideFile(const std::string& directory,
                                           const std::string& relativePath) {
    if (directory.empty()) return {};
    const std::string path = directory + "/" + relativePath;
    std::ifstream input(path, std::ios::binary);
    if (!input) return {};
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

[[nodiscard]] std::string readPackagedAsset(AAssetManager* assets,
                                            const std::string& relativePath) {
    if (assets == nullptr) return {};
    AAsset* asset = AAssetManager_open(assets, relativePath.c_str(), AASSET_MODE_BUFFER);
    if (asset == nullptr) return {};

    const off_t assetLength = AAsset_getLength(asset);
    if (assetLength < 0) {
        AAsset_close(asset);
        return {};
    }

    std::string result(static_cast<std::size_t>(assetLength), '\0');
    std::size_t bytesRead = 0;
    while (bytesRead < result.size()) {
        const int amount = AAsset_read(asset, result.data() + bytesRead, result.size() - bytesRead);
        if (amount <= 0) break;
        bytesRead += static_cast<std::size_t>(amount);
    }
    AAsset_close(asset);
    if (bytesRead != result.size()) result.resize(bytesRead);
    return result;
}

}  // namespace

std::string readTextAsset(AAssetManager* assets, const std::string& relativePath,
                          const std::string& overrideDirectory) {
    std::string source = readOverrideFile(overrideDirectory, relativePath);
    if (!source.empty()) {
        BOX_LOG_DEBUG("Loaded shader override: %s", relativePath.c_str());
        return source;
    }
    source = readPackagedAsset(assets, relativePath);
    if (source.empty()) {
        BOX_LOG_WARN("Unable to read packaged asset: %s", relativePath.c_str());
    }
    return source;
}

}  // namespace box::renderer
