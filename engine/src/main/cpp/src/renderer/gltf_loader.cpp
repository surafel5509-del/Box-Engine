// Path: engine/src/main/cpp/src/renderer/gltf_loader.cpp
#include "box/renderer/gltf_loader.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <numeric>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "box/ecs/json.hpp"

namespace box::renderer {
namespace {

using box::ecs::JsonValue;

constexpr std::uint32_t kGlbMagic = 0x46546C67U;
constexpr std::uint32_t kGlbJsonChunk = 0x4E4F534AU;
constexpr std::uint32_t kGlbBinaryChunk = 0x004E4942U;
constexpr std::size_t kMaximumImagesBytes = 128U * 1024U * 1024U;
constexpr std::size_t kMaximumAccessorElements = 1U * 1024U * 1024U;
constexpr std::size_t kMaximumModelVertices = 2U * 1024U * 1024U;
constexpr std::size_t kMaximumModelIndices = 4U * 1024U * 1024U;
constexpr std::size_t kMaximumImageBytes = 32U * 1024U * 1024U;

struct BufferViewData {
    std::size_t bufferIndex = 0U;
    std::size_t byteOffset = 0U;
    std::size_t byteLength = 0U;
    std::size_t byteStride = 0U;
};

struct AccessorData {
    std::int32_t bufferViewIndex = -1;
    std::size_t byteOffset = 0U;
    std::size_t count = 0U;
    std::uint32_t componentType = 0U;
    std::size_t componentCount = 0U;
    std::size_t componentSize = 0U;
    bool normalized = false;
};

[[nodiscard]] bool fail(std::string* error, const std::string& message) {
    if (error != nullptr) *error = message;
    return false;
}

[[nodiscard]] const JsonValue::Array* getArray(const JsonValue& value, const char* key) {
    const JsonValue* child = value.find(key);
    return child != nullptr ? child->asArray() : nullptr;
}

[[nodiscard]] bool readString(const JsonValue* value, std::string& output) {
    if (value == nullptr || value->asString() == nullptr) return false;
    output = *value->asString();
    return true;
}

[[nodiscard]] bool readUnsigned(const JsonValue* value, std::size_t& output,
                                std::size_t maximum = std::numeric_limits<std::size_t>::max()) {
    if (value == nullptr || !value->isNumber()) return false;
    const double number = value->asNumber();
    const double sizeExclusiveLimit =
        std::ldexp(1.0, std::numeric_limits<std::size_t>::digits);
    if (!std::isfinite(number) || number < 0.0 || std::floor(number) != number ||
        number >= sizeExclusiveLimit || number > static_cast<double>(maximum)) {
        return false;
    }
    output = static_cast<std::size_t>(number);
    return true;
}

[[nodiscard]] bool readSigned(const JsonValue* value, std::int32_t& output) {
    if (value == nullptr || !value->isNumber()) return false;
    const double number = value->asNumber();
    if (!std::isfinite(number) || std::floor(number) != number ||
        number < static_cast<double>(std::numeric_limits<std::int32_t>::min()) ||
        number > static_cast<double>(std::numeric_limits<std::int32_t>::max())) {
        return false;
    }
    output = static_cast<std::int32_t>(number);
    return true;
}

[[nodiscard]] bool readFloat(const JsonValue* value, float& output) {
    if (value == nullptr || !value->isNumber()) return false;
    const double number = value->asNumber();
    if (!std::isfinite(number) ||
        number < -static_cast<double>(std::numeric_limits<float>::max()) ||
        number > static_cast<double>(std::numeric_limits<float>::max())) {
        return false;
    }
    output = static_cast<float>(number);
    return true;
}

[[nodiscard]] bool readOptionalFloat(const JsonValue& object, const char* key,
                                     float fallback, float& output) {
    const JsonValue* value = object.find(key);
    if (value == nullptr) {
        output = fallback;
        return true;
    }
    return readFloat(value, output);
}

[[nodiscard]] bool readOptionalIndex(const JsonValue* value, std::int32_t& output) {
    if (value == nullptr) {
        output = -1;
        return true;
    }
    if (!readSigned(value, output) || output < 0) return false;
    return true;
}

[[nodiscard]] bool readVec3(const JsonValue* value, math::Vec3& output) {
    const JsonValue::Array* array = value != nullptr ? value->asArray() : nullptr;
    return array != nullptr && array->size() == 3U &&
           readFloat(&(*array)[0], output.x) && readFloat(&(*array)[1], output.y) &&
           readFloat(&(*array)[2], output.z);
}

[[nodiscard]] bool readVec4(const JsonValue* value, math::Vec4& output) {
    const JsonValue::Array* array = value != nullptr ? value->asArray() : nullptr;
    return array != nullptr && array->size() == 4U &&
           readFloat(&(*array)[0], output.x) && readFloat(&(*array)[1], output.y) &&
           readFloat(&(*array)[2], output.z) && readFloat(&(*array)[3], output.w);
}

[[nodiscard]] bool readU32(const std::vector<std::uint8_t>& data, std::size_t offset,
                           std::uint32_t& value) {
    if (offset > data.size() || data.size() - offset < 4U) return false;
    value = static_cast<std::uint32_t>(data[offset]) |
            (static_cast<std::uint32_t>(data[offset + 1U]) << 8U) |
            (static_cast<std::uint32_t>(data[offset + 2U]) << 16U) |
            (static_cast<std::uint32_t>(data[offset + 3U]) << 24U);
    return true;
}

[[nodiscard]] std::size_t componentSize(std::uint32_t componentType) {
    switch (componentType) {
        case 5120U:
        case 5121U: return 1U;
        case 5122U:
        case 5123U: return 2U;
        case 5125U:
        case 5126U: return 4U;
        default: return 0U;
    }
}

[[nodiscard]] std::size_t accessorComponentCount(const JsonValue* typeValue) {
    if (typeValue == nullptr || typeValue->asString() == nullptr) return 0U;
    const std::string& type = *typeValue->asString();
    if (type == "SCALAR") return 1U;
    if (type == "VEC2") return 2U;
    if (type == "VEC3") return 3U;
    if (type == "VEC4") return 4U;
    if (type == "MAT2") return 4U;
    if (type == "MAT3") return 9U;
    if (type == "MAT4") return 16U;
    return 0U;
}

[[nodiscard]] bool decodeBase64(std::string_view text, std::vector<std::uint8_t>& output) {
    constexpr std::string_view alphabet =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::array<std::int16_t, 256> lookup{};
    lookup.fill(-1);
    for (std::size_t index = 0; index < alphabet.size(); ++index) {
        lookup[static_cast<unsigned char>(alphabet[index])] = static_cast<std::int16_t>(index);
    }

    output.clear();
    std::uint32_t accumulator = 0U;
    unsigned int bitCount = 0U;
    bool paddingStarted = false;
    unsigned int paddingCount = 0U;
    for (const char character : text) {
        if (character == ' ' || character == '\t' || character == '\r' || character == '\n') {
            continue;
        }
        if (character == '=') {
            paddingStarted = true;
            ++paddingCount;
            if (paddingCount > 2U) return false;
            continue;
        }
        if (paddingStarted) return false;
        const std::int16_t digit = lookup[static_cast<unsigned char>(character)];
        if (digit < 0) return false;
        accumulator = (accumulator << 6U) | static_cast<std::uint32_t>(digit);
        bitCount += 6U;
        if (bitCount >= 8U) {
            bitCount -= 8U;
            output.push_back(static_cast<std::uint8_t>((accumulator >> bitCount) & 0xFFU));
            if (output.size() > GltfLoader::kMaximumAssetBytes) return false;
        }
    }
    if (bitCount == 6U || (paddingCount == 1U && bitCount != 2U) ||
        (paddingCount == 2U && bitCount != 4U)) {
        return false;
    }
    return true;
}

[[nodiscard]] bool decodeDataUri(const std::string& uri, std::vector<std::uint8_t>& output,
                                 std::string& mimeType) {
    if (uri.rfind("data:", 0U) != 0U) return false;
    const std::size_t comma = uri.find(',');
    if (comma == std::string::npos) return false;
    const std::string_view header(uri.data() + 5U, comma - 5U);
    const std::size_t semicolon = header.find(';');
    mimeType.assign(header.substr(0U, semicolon));
    const bool base64 = header.find(";base64") != std::string_view::npos;
    const std::string_view payload(uri.data() + comma + 1U, uri.size() - comma - 1U);
    if (base64) return decodeBase64(payload, output);

    output.clear();
    for (std::size_t index = 0; index < payload.size(); ++index) {
        const char character = payload[index];
        if (character == '%') {
            if (payload.size() - index < 3U) return false;
            const auto hex = [](char value) -> int {
                if (value >= '0' && value <= '9') return value - '0';
                if (value >= 'A' && value <= 'F') return value - 'A' + 10;
                if (value >= 'a' && value <= 'f') return value - 'a' + 10;
                return -1;
            };
            const int high = hex(payload[index + 1U]);
            const int low = hex(payload[index + 2U]);
            if (high < 0 || low < 0) return false;
            output.push_back(static_cast<std::uint8_t>((high << 4) | low));
            index += 2U;
        } else {
            output.push_back(static_cast<std::uint8_t>(character));
        }
        if (output.size() > GltfLoader::kMaximumAssetBytes) return false;
    }
    return true;
}

[[nodiscard]] bool decodeUriPath(std::string_view uri, std::string& output) {
    if (uri.empty() || uri.front() == '/' || uri.find(':') != std::string_view::npos ||
        uri.find('\\') != std::string_view::npos) {
        return false;
    }
    output.clear();
    for (std::size_t index = 0; index < uri.size(); ++index) {
        if (uri[index] == '%') {
            if (uri.size() - index < 3U) return false;
            const auto hex = [](char value) -> int {
                if (value >= '0' && value <= '9') return value - '0';
                if (value >= 'A' && value <= 'F') return value - 'A' + 10;
                if (value >= 'a' && value <= 'f') return value - 'a' + 10;
                return -1;
            };
            const int high = hex(uri[index + 1U]);
            const int low = hex(uri[index + 2U]);
            if (high < 0 || low < 0) return false;
            output.push_back(static_cast<char>((high << 4) | low));
            index += 2U;
        } else {
            output.push_back(uri[index]);
        }
    }
    if (output.empty() || output.front() == '/' || output.find('\\') != std::string::npos ||
        output.find(':') != std::string::npos) {
        return false;
    }
    return true;
}

[[nodiscard]] bool resolveAssetPath(const std::string& modelPath, const std::string& uri,
                                   std::string& resolved) {
    std::string decoded;
    if (!decodeUriPath(uri, decoded)) return false;
    std::vector<std::string> segments;
    const std::size_t slash = modelPath.find_last_of('/');
    const std::string_view directory = slash == std::string::npos
                                           ? std::string_view{}
                                           : std::string_view(modelPath.data(), slash);
    const auto appendSegments = [&segments](std::string_view path) {
        std::size_t start = 0U;
        while (start <= path.size()) {
            const std::size_t end = path.find('/', start);
            const std::size_t length = (end == std::string_view::npos ? path.size() : end) - start;
            const std::string_view segment = path.substr(start, length);
            if (segment == "..") {
                if (segments.empty()) return false;
                segments.pop_back();
            } else if (!segment.empty() && segment != ".") {
                segments.emplace_back(segment);
            }
            if (end == std::string_view::npos) break;
            start = end + 1U;
        }
        return true;
    };
    if (!appendSegments(directory) || !appendSegments(decoded) || segments.empty()) return false;
    resolved.clear();
    for (const std::string& segment : segments) {
        if (!resolved.empty()) resolved.push_back('/');
        resolved.append(segment);
    }
    return true;
}

[[nodiscard]] bool readExternalAsset(const std::string& modelPath, const std::string& uri,
                                    const GltfAssetReader& reader,
                                    std::vector<std::uint8_t>& output, std::string* error) {
    std::string path;
    if (!resolveAssetPath(modelPath, uri, path)) {
        return fail(error, "A glTF external URI is invalid or escapes the asset root.");
    }
    std::string readError;
    if (!reader(path, output, &readError)) {
        return fail(error, readError.empty() ? "A glTF external asset could not be read."
                                             : readError);
    }
    if (output.size() > GltfLoader::kMaximumAssetBytes) {
        return fail(error, "A glTF external asset exceeds the 128 MiB safety limit.");
    }
    return true;
}

[[nodiscard]] bool parseGlb(const std::vector<std::uint8_t>& file, std::string& json,
                            std::vector<std::uint8_t>& binary, std::string* error) {
    std::uint32_t magic = 0U;
    if (file.size() < 12U || !readU32(file, 0U, magic) || magic != kGlbMagic) {
        return fail(error, "The asset is not a valid glTF JSON or GLB file.");
    }
    std::uint32_t version = 0U;
    std::uint32_t totalLength = 0U;
    if (!readU32(file, 4U, version) || !readU32(file, 8U, totalLength) || version != 2U ||
        totalLength != file.size()) {
        return fail(error, "The GLB header has an unsupported version or invalid length.");
    }

    bool foundJson = false;
    bool foundBinary = false;
    std::size_t offset = 12U;
    while (offset < file.size()) {
        std::uint32_t chunkLength = 0U;
        std::uint32_t chunkType = 0U;
        if (!readU32(file, offset, chunkLength) || !readU32(file, offset + 4U, chunkType)) {
            return fail(error, "The GLB chunk header is truncated.");
        }
        offset += 8U;
        const std::size_t length = static_cast<std::size_t>(chunkLength);
        if (length > file.size() - offset) return fail(error, "A GLB chunk exceeds the file length.");
        if (chunkType == kGlbJsonChunk) {
            if (foundJson) return fail(error, "The GLB contains more than one JSON chunk.");
            json.assign(reinterpret_cast<const char*>(file.data() + offset), length);
            while (!json.empty() && (json.back() == '\0' || json.back() == ' ' ||
                                     json.back() == '\t' || json.back() == '\r' ||
                                     json.back() == '\n')) {
                json.pop_back();
            }
            foundJson = true;
        } else if (chunkType == kGlbBinaryChunk) {
            if (foundBinary) return fail(error, "The GLB contains more than one BIN chunk.");
            binary.assign(file.begin() + static_cast<std::ptrdiff_t>(offset),
                          file.begin() + static_cast<std::ptrdiff_t>(offset + length));
            foundBinary = true;
        }
        offset += length;
    }
    if (!foundJson) return fail(error, "The GLB is missing its JSON chunk.");
    if (binary.size() > GltfLoader::kMaximumBufferBytes) {
        return fail(error, "The GLB binary chunk exceeds the 128 MiB safety limit.");
    }
    return true;
}

[[nodiscard]] bool parseJsonDocument(const std::vector<std::uint8_t>& file, std::string& json,
                                    std::vector<std::uint8_t>& binary, std::string* error) {
    std::uint32_t magic = 0U;
    if (file.size() >= 4U && readU32(file, 0U, magic) && magic == kGlbMagic) {
        return parseGlb(file, json, binary, error);
    }
    json.assign(reinterpret_cast<const char*>(file.data()), file.size());
    binary.clear();
    return true;
}

[[nodiscard]] bool parseBuffers(const JsonValue& root, const std::string& modelPath,
                                const GltfAssetReader& reader,
                                const std::vector<std::uint8_t>& glbBinary,
                                std::vector<std::vector<std::uint8_t>>& buffers,
                                std::string* error) {
    const JsonValue::Array* array = getArray(root, "buffers");
    if (array == nullptr) {
        if (root.find("buffers") != nullptr) return fail(error, "The glTF buffers member is not an array.");
        buffers.clear();
        return true;
    }
    buffers.clear();
    buffers.reserve(array->size());
    bool usedGlbBinary = false;
    std::size_t totalBytes = 0U;
    for (const JsonValue& item : *array) {
        std::size_t declaredLength = 0U;
        if (!readUnsigned(item.find("byteLength"), declaredLength, GltfLoader::kMaximumBufferBytes)) {
            return fail(error, "A glTF buffer has an invalid or excessive byteLength.");
        }
        std::vector<std::uint8_t> bytes;
        const JsonValue* uriValue = item.find("uri");
        if (uriValue != nullptr) {
            std::string uri;
            if (!readString(uriValue, uri)) return fail(error, "A glTF buffer URI is not a string.");
            if (uri.rfind("data:", 0U) == 0U) {
                std::string ignoredMimeType;
                if (!decodeDataUri(uri, bytes, ignoredMimeType)) {
                    return fail(error, "A glTF buffer data URI is malformed or too large.");
                }
            } else if (!readExternalAsset(modelPath, uri, reader, bytes, error)) {
                return false;
            }
        } else {
            if (glbBinary.empty() || usedGlbBinary) {
                return fail(error, "A glTF buffer without a URI has no matching GLB BIN chunk.");
            }
            bytes = glbBinary;
            usedGlbBinary = true;
        }
        if (bytes.size() < declaredLength) {
            return fail(error, "A glTF buffer is shorter than its declared byteLength.");
        }
        bytes.resize(declaredLength);
        if (declaredLength > GltfLoader::kMaximumBufferBytes - totalBytes) {
            return fail(error, "The glTF buffers exceed the total 128 MiB safety limit.");
        }
        totalBytes += declaredLength;
        buffers.push_back(std::move(bytes));
    }
    return true;
}

[[nodiscard]] bool parseBufferViews(const JsonValue& root,
                                   const std::vector<std::vector<std::uint8_t>>& buffers,
                                   std::vector<BufferViewData>& views, std::string* error) {
    const JsonValue::Array* array = getArray(root, "bufferViews");
    if (array == nullptr) {
        if (root.find("bufferViews") != nullptr) return fail(error, "The glTF bufferViews member is not an array.");
        views.clear();
        return true;
    }
    views.clear();
    views.reserve(array->size());
    for (const JsonValue& item : *array) {
        BufferViewData view{};
        std::size_t bufferIndex = 0U;
        const JsonValue* byteOffset = item.find("byteOffset");
        if (!readUnsigned(item.find("buffer"), bufferIndex) || bufferIndex >= buffers.size() ||
            (byteOffset != nullptr &&
             !readUnsigned(byteOffset, view.byteOffset, GltfLoader::kMaximumBufferBytes)) ||
            !readUnsigned(item.find("byteLength"), view.byteLength, GltfLoader::kMaximumBufferBytes)) {
            return fail(error, "A glTF bufferView has an invalid buffer index or byte range.");
        }
        view.bufferIndex = bufferIndex;
        if (view.byteLength > buffers[bufferIndex].size() ||
            view.byteOffset > buffers[bufferIndex].size() - view.byteLength) {
            return fail(error, "A glTF bufferView references bytes outside its buffer.");
        }
        if (item.find("byteStride") != nullptr &&
            (!readUnsigned(item.find("byteStride"), view.byteStride, 252U) ||
             view.byteStride < 4U)) {
            return fail(error, "A glTF bufferView has an unsupported byteStride.");
        }
        views.push_back(view);
    }
    return true;
}

[[nodiscard]] bool parseAccessors(const JsonValue& root, const std::vector<BufferViewData>& views,
                                  const std::vector<std::vector<std::uint8_t>>& buffers,
                                  std::vector<AccessorData>& accessors, std::string* error) {
    const JsonValue::Array* array = getArray(root, "accessors");
    if (array == nullptr) {
        if (root.find("accessors") != nullptr) return fail(error, "The glTF accessors member is not an array.");
        accessors.clear();
        return true;
    }
    accessors.clear();
    accessors.reserve(array->size());
    for (const JsonValue& item : *array) {
        if (item.find("sparse") != nullptr) {
            return fail(error, "Sparse glTF accessors are not supported by the mobile loader.");
        }
        AccessorData accessor{};
        const JsonValue* viewValue = item.find("bufferView");
        if (viewValue != nullptr && !readOptionalIndex(viewValue, accessor.bufferViewIndex)) {
            return fail(error, "A glTF accessor has an invalid bufferView index.");
        }
        const JsonValue* byteOffset = item.find("byteOffset");
        std::size_t componentType = 0U;
        if ((byteOffset != nullptr &&
             !readUnsigned(byteOffset, accessor.byteOffset, GltfLoader::kMaximumBufferBytes)) ||
            !readUnsigned(item.find("count"), accessor.count, kMaximumAccessorElements) ||
            accessor.count == 0U ||
            !readUnsigned(item.find("componentType"), componentType,
                          std::numeric_limits<std::uint32_t>::max())) {
            return fail(error, "A glTF accessor has an invalid offset, count, or component type.");
        }
        accessor.componentType = static_cast<std::uint32_t>(componentType);
        accessor.componentCount = accessorComponentCount(item.find("type"));
        accessor.componentSize = componentSize(accessor.componentType);
        if (accessor.componentCount == 0U || accessor.componentSize == 0U ||
            accessor.componentCount > std::numeric_limits<std::size_t>::max() / accessor.componentSize) {
            return fail(error, "A glTF accessor has an unsupported type or component size.");
        }
        const JsonValue* normalized = item.find("normalized");
        if (normalized != nullptr && !normalized->isBool()) {
            return fail(error, "A glTF accessor normalized flag is not boolean.");
        }
        accessor.normalized = normalized != nullptr && normalized->asBool();
        if (accessor.bufferViewIndex >= 0) {
            const std::size_t viewIndex = static_cast<std::size_t>(accessor.bufferViewIndex);
            if (viewIndex >= views.size()) return fail(error, "A glTF accessor references a missing bufferView.");
            const BufferViewData& view = views[viewIndex];
            const std::size_t elementSize = accessor.componentCount * accessor.componentSize;
            const std::size_t stride = view.byteStride == 0U ? elementSize : view.byteStride;
            if (stride < elementSize || accessor.byteOffset > view.byteLength ||
                accessor.byteOffset % accessor.componentSize != 0U) {
                return fail(error, "A glTF accessor has invalid alignment or byteStride.");
            }
            const std::size_t finalElementOffset = accessor.count - 1U;
            if (finalElementOffset > 0U && stride >
                    (std::numeric_limits<std::size_t>::max() - accessor.byteOffset) / finalElementOffset) {
                return fail(error, "A glTF accessor byte range overflows the address space.");
            }
            const std::size_t lastOffset = accessor.byteOffset + finalElementOffset * stride;
            if (lastOffset > view.byteLength || elementSize > view.byteLength - lastOffset) {
                return fail(error, "A glTF accessor reads past the end of its bufferView.");
            }
            const std::vector<std::uint8_t>& bytes = buffers[view.bufferIndex];
            if (view.byteOffset > bytes.size() || lastOffset > bytes.size() - view.byteOffset ||
                elementSize > bytes.size() - view.byteOffset - lastOffset) {
                return fail(error, "A glTF accessor reads past the end of its buffer.");
            }
        } else if (accessor.byteOffset != 0U) {
            return fail(error, "A glTF accessor without a bufferView cannot have a byteOffset.");
        }
        accessors.push_back(accessor);
    }
    return true;
}

[[nodiscard]] const std::uint8_t* accessorElement(const AccessorData& accessor,
                                                   std::size_t elementIndex,
                                                   const std::vector<BufferViewData>& views,
                                                   const std::vector<std::vector<std::uint8_t>>& buffers) {
    if (accessor.bufferViewIndex < 0 || elementIndex >= accessor.count) return nullptr;
    const BufferViewData& view = views[static_cast<std::size_t>(accessor.bufferViewIndex)];
    const std::size_t elementSize = accessor.componentCount * accessor.componentSize;
    const std::size_t stride = view.byteStride == 0U ? elementSize : view.byteStride;
    const std::size_t offset = view.byteOffset + accessor.byteOffset + elementIndex * stride;
    return buffers[view.bufferIndex].data() + offset;
}

[[nodiscard]] bool readFloatAttribute(std::size_t accessorIndex, std::size_t expectedComponents,
                                      const std::vector<AccessorData>& accessors,
                                      const std::vector<BufferViewData>& views,
                                      const std::vector<std::vector<std::uint8_t>>& buffers,
                                      std::vector<std::array<float, 4>>& output,
                                      std::string* error) {
    if (accessorIndex >= accessors.size()) return fail(error, "A glTF primitive references a missing accessor.");
    const AccessorData& accessor = accessors[accessorIndex];
    if (accessor.componentType != 5126U || accessor.componentCount != expectedComponents ||
        accessor.normalized) {
        return fail(error, "A glTF vertex attribute has an unsupported component type or shape.");
    }
    output.clear();
    output.resize(accessor.count);
    for (std::size_t elementIndex = 0; elementIndex < accessor.count; ++elementIndex) {
        const std::uint8_t* data = accessorElement(accessor, elementIndex, views, buffers);
        for (std::size_t component = 0; component < expectedComponents; ++component) {
            float value = 0.0f;
            if (data != nullptr) {
                std::uint32_t bits = static_cast<std::uint32_t>(data[component * 4U]) |
                                     (static_cast<std::uint32_t>(data[component * 4U + 1U]) << 8U) |
                                     (static_cast<std::uint32_t>(data[component * 4U + 2U]) << 16U) |
                                     (static_cast<std::uint32_t>(data[component * 4U + 3U]) << 24U);
                value = std::bit_cast<float>(bits);
            }
            if (!std::isfinite(value)) return fail(error, "A glTF vertex attribute contains a non-finite value.");
            output[elementIndex][component] = value;
        }
    }
    return true;
}

[[nodiscard]] bool readIndexAttribute(std::size_t accessorIndex,
                                      const std::vector<AccessorData>& accessors,
                                      const std::vector<BufferViewData>& views,
                                      const std::vector<std::vector<std::uint8_t>>& buffers,
                                      std::vector<std::uint32_t>& output,
                                      std::string* error) {
    if (accessorIndex >= accessors.size()) return fail(error, "A glTF primitive references a missing index accessor.");
    const AccessorData& accessor = accessors[accessorIndex];
    if (accessor.componentCount != 1U || accessor.normalized ||
        (accessor.componentType != 5121U && accessor.componentType != 5123U &&
         accessor.componentType != 5125U)) {
        return fail(error, "A glTF index accessor must use unsigned byte, short, or int values.");
    }
    output.clear();
    output.reserve(accessor.count);
    for (std::size_t index = 0; index < accessor.count; ++index) {
        const std::uint8_t* data = accessorElement(accessor, index, views, buffers);
        std::uint32_t value = 0U;
        if (data != nullptr) {
            if (accessor.componentType == 5121U) {
                value = data[0];
            } else if (accessor.componentType == 5123U) {
                value = static_cast<std::uint32_t>(data[0]) |
                        (static_cast<std::uint32_t>(data[1]) << 8U);
            } else {
                value = static_cast<std::uint32_t>(data[0]) |
                        (static_cast<std::uint32_t>(data[1]) << 8U) |
                        (static_cast<std::uint32_t>(data[2]) << 16U) |
                        (static_cast<std::uint32_t>(data[3]) << 24U);
            }
        }
        output.push_back(value);
    }
    return true;
}

void generateNormalsAndTangents(MeshPrimitive3D& primitive, bool hasNormals,
                                bool hasTangents, bool hasTexcoords) {
    if (!hasNormals) {
        for (Vertex3D& vertex : primitive.vertices) vertex.normal = {};
        for (std::size_t triangle = 0; triangle < primitive.indices.size(); triangle += 3U) {
            const std::uint32_t i0 = primitive.indices[triangle];
            const std::uint32_t i1 = primitive.indices[triangle + 1U];
            const std::uint32_t i2 = primitive.indices[triangle + 2U];
            const math::Vec3 edgeA = primitive.vertices[i1].position - primitive.vertices[i0].position;
            const math::Vec3 edgeB = primitive.vertices[i2].position - primitive.vertices[i0].position;
            const math::Vec3 faceNormal = math::cross(edgeA, edgeB);
            primitive.vertices[i0].normal = primitive.vertices[i0].normal + faceNormal;
            primitive.vertices[i1].normal = primitive.vertices[i1].normal + faceNormal;
            primitive.vertices[i2].normal = primitive.vertices[i2].normal + faceNormal;
        }
        for (Vertex3D& vertex : primitive.vertices) {
            vertex.normal = math::normalized(vertex.normal);
            if (math::length(vertex.normal) <= math::kEpsilon) vertex.normal = {0.0f, 1.0f, 0.0f};
        }
    } else {
        for (Vertex3D& vertex : primitive.vertices) {
            vertex.normal = math::normalized(vertex.normal);
            if (math::length(vertex.normal) <= math::kEpsilon) vertex.normal = {0.0f, 1.0f, 0.0f};
        }
    }

    if (hasTangents) {
        for (Vertex3D& vertex : primitive.vertices) {
            const math::Vec3 tangent{vertex.tangent.x, vertex.tangent.y, vertex.tangent.z};
            const math::Vec3 unitTangent = math::normalized(tangent);
            vertex.tangent.x = unitTangent.x;
            vertex.tangent.y = unitTangent.y;
            vertex.tangent.z = unitTangent.z;
            vertex.tangent.w = vertex.tangent.w < 0.0f ? -1.0f : 1.0f;
        }
        return;
    }

    std::vector<math::Vec3> tangentSums(primitive.vertices.size());
    std::vector<math::Vec3> bitangentSums(primitive.vertices.size());
    if (hasTexcoords) {
        for (std::size_t triangle = 0; triangle < primitive.indices.size(); triangle += 3U) {
            const std::uint32_t i0 = primitive.indices[triangle];
            const std::uint32_t i1 = primitive.indices[triangle + 1U];
            const std::uint32_t i2 = primitive.indices[triangle + 2U];
            const Vertex3D& v0 = primitive.vertices[i0];
            const Vertex3D& v1 = primitive.vertices[i1];
            const Vertex3D& v2 = primitive.vertices[i2];
            const math::Vec3 edgeA = v1.position - v0.position;
            const math::Vec3 edgeB = v2.position - v0.position;
            const math::Vec2 uvA = v1.texcoord - v0.texcoord;
            const math::Vec2 uvB = v2.texcoord - v0.texcoord;
            const float determinant = uvA.x * uvB.y - uvA.y * uvB.x;
            if (std::fabs(determinant) <= math::kEpsilon) continue;
            const float inverseDeterminant = 1.0f / determinant;
            const math::Vec3 tangent =
                (edgeA * uvB.y - edgeB * uvA.y) * inverseDeterminant;
            const math::Vec3 bitangent =
                (edgeB * uvA.x - edgeA * uvB.x) * inverseDeterminant;
            for (const std::uint32_t vertexIndex : {i0, i1, i2}) {
                tangentSums[vertexIndex] = tangentSums[vertexIndex] + tangent;
                bitangentSums[vertexIndex] = bitangentSums[vertexIndex] + bitangent;
            }
        }
    }
    for (std::size_t index = 0; index < primitive.vertices.size(); ++index) {
        const math::Vec3 normal = primitive.vertices[index].normal;
        math::Vec3 tangent = tangentSums[index] - normal * math::dot(normal, tangentSums[index]);
        tangent = math::normalized(tangent);
        if (math::length(tangent) <= math::kEpsilon) {
            const math::Vec3 reference = std::fabs(normal.y) < 0.9f
                                             ? math::Vec3{0.0f, 1.0f, 0.0f}
                                             : math::Vec3{1.0f, 0.0f, 0.0f};
            tangent = math::normalized(math::cross(reference, normal));
        }
        const float handedness = math::dot(math::cross(normal, tangent), bitangentSums[index]) < 0.0f
                                     ? -1.0f
                                     : 1.0f;
        primitive.vertices[index].tangent = {tangent.x, tangent.y, tangent.z, handedness};
    }
}

[[nodiscard]] bool computePrimitiveBounds(MeshPrimitive3D& primitive) {
    if (primitive.vertices.empty()) return false;
    primitive.bounds.minimum = primitive.vertices.front().position;
    primitive.bounds.maximum = primitive.vertices.front().position;
    for (const Vertex3D& vertex : primitive.vertices) {
        primitive.bounds.minimum.x = std::min(primitive.bounds.minimum.x, vertex.position.x);
        primitive.bounds.minimum.y = std::min(primitive.bounds.minimum.y, vertex.position.y);
        primitive.bounds.minimum.z = std::min(primitive.bounds.minimum.z, vertex.position.z);
        primitive.bounds.maximum.x = std::max(primitive.bounds.maximum.x, vertex.position.x);
        primitive.bounds.maximum.y = std::max(primitive.bounds.maximum.y, vertex.position.y);
        primitive.bounds.maximum.z = std::max(primitive.bounds.maximum.z, vertex.position.z);
    }
    return true;
}

[[nodiscard]] bool readTextureInfo(const JsonValue* info, std::int32_t& textureIndex,
                                   std::size_t textureCount, const char* label,
                                   std::string* error) {
    textureIndex = -1;
    if (info == nullptr) return true;
    if (!info->isObject() || !readOptionalIndex(info->find("index"), textureIndex) ||
        textureIndex < 0 || static_cast<std::size_t>(textureIndex) >= textureCount) {
        return fail(error, std::string("A glTF material has an invalid ") + label + " texture index.");
    }
    const JsonValue* texCoord = info->find("texCoord");
    std::int32_t texCoordIndex = 0;
    if (texCoord != nullptr && (!readSigned(texCoord, texCoordIndex) || texCoordIndex != 0)) {
        return fail(error, "Only glTF texture coordinate set zero is supported.");
    }
    return true;
}

[[nodiscard]] bool parseImages(const JsonValue& root, const std::string& modelPath,
                               const GltfAssetReader& reader,
                               const std::vector<BufferViewData>& views,
                               const std::vector<std::vector<std::uint8_t>>& buffers,
                               std::vector<GltfImage>& images, std::string* error) {
    const JsonValue::Array* array = getArray(root, "images");
    if (array == nullptr) {
        if (root.find("images") != nullptr) return fail(error, "The glTF images member is not an array.");
        images.clear();
        return true;
    }
    images.clear();
    images.reserve(array->size());
    std::size_t totalBytes = 0U;
    for (const JsonValue& item : *array) {
        GltfImage image{};
        const JsonValue* name = item.find("name");
        if (name != nullptr && !readString(name, image.name)) {
            return fail(error, "A glTF image name is not a string.");
        }
        const JsonValue* mimeType = item.find("mimeType");
        if (mimeType != nullptr && !readString(mimeType, image.mimeType)) {
            return fail(error, "A glTF image mimeType is not a string.");
        }
        const JsonValue* uriValue = item.find("uri");
        if (uriValue != nullptr) {
            std::string uri;
            if (!readString(uriValue, uri)) return fail(error, "A glTF image URI is not a string.");
            if (uri.rfind("data:", 0U) == 0U) {
                std::string dataMimeType;
                if (!decodeDataUri(uri, image.encodedBytes, dataMimeType)) {
                    return fail(error, "A glTF image data URI is malformed or too large.");
                }
                if (image.mimeType.empty()) image.mimeType = std::move(dataMimeType);
            } else if (!readExternalAsset(modelPath, uri, reader, image.encodedBytes, error)) {
                return false;
            }
        } else {
            std::size_t viewIndex = 0U;
            if (!readUnsigned(item.find("bufferView"), viewIndex) || viewIndex >= views.size() ||
                image.mimeType.empty()) {
                return fail(error, "A glTF embedded image needs a valid bufferView and mimeType.");
            }
            const BufferViewData& view = views[viewIndex];
            const std::vector<std::uint8_t>& bytes = buffers[view.bufferIndex];
            image.encodedBytes.assign(bytes.begin() + static_cast<std::ptrdiff_t>(view.byteOffset),
                                      bytes.begin() + static_cast<std::ptrdiff_t>(view.byteOffset + view.byteLength));
        }
        if (image.encodedBytes.empty() || image.encodedBytes.size() > kMaximumImageBytes ||
            image.encodedBytes.size() > kMaximumImagesBytes - totalBytes) {
            return fail(error, "A glTF image is empty or exceeds the image data safety limits.");
        }
        totalBytes += image.encodedBytes.size();
        images.push_back(std::move(image));
    }
    return true;
}

[[nodiscard]] bool parseSamplers(const JsonValue& root, std::vector<GltfSampler>& samplers,
                                 std::string* error) {
    const JsonValue::Array* array = getArray(root, "samplers");
    if (array == nullptr) {
        if (root.find("samplers") != nullptr) return fail(error, "The glTF samplers member is not an array.");
        samplers.clear();
        return true;
    }
    samplers.clear();
    samplers.reserve(array->size());
    for (const JsonValue& item : *array) {
        GltfSampler sampler{};
        const auto readParameter = [&item](const char* key, std::int32_t fallback,
                                           std::int32_t& result) {
            const JsonValue* value = item.find(key);
            if (value == nullptr) {
                result = fallback;
                return true;
            }
            return readSigned(value, result);
        };
        if (!readParameter("minFilter", 9729, sampler.minFilter) ||
            !readParameter("magFilter", 9729, sampler.magFilter) ||
            !readParameter("wrapS", 10497, sampler.wrapS) ||
            !readParameter("wrapT", 10497, sampler.wrapT)) {
            return fail(error, "A glTF sampler has a non-integer parameter.");
        }
        const bool validMin = sampler.minFilter == 9728 || sampler.minFilter == 9729 ||
                              sampler.minFilter == 9984 || sampler.minFilter == 9985 ||
                              sampler.minFilter == 9986 || sampler.minFilter == 9987;
        const bool validMag = sampler.magFilter == 9728 || sampler.magFilter == 9729;
        const auto validWrap = [](std::int32_t value) {
            return value == 33071 || value == 33648 || value == 10497;
        };
        if (!validMin || !validMag || !validWrap(sampler.wrapS) || !validWrap(sampler.wrapT)) {
            return fail(error, "A glTF sampler specifies an unsupported GLES filter or wrap mode.");
        }
        samplers.push_back(sampler);
    }
    return true;
}

[[nodiscard]] bool parseTextures(const JsonValue& root, const std::vector<GltfImage>& images,
                                 const std::vector<GltfSampler>& samplers,
                                 std::vector<GltfTexture>& textures, std::string* error) {
    const JsonValue::Array* array = getArray(root, "textures");
    if (array == nullptr) {
        if (root.find("textures") != nullptr) return fail(error, "The glTF textures member is not an array.");
        textures.clear();
        return true;
    }
    textures.clear();
    textures.reserve(array->size());
    for (const JsonValue& item : *array) {
        GltfTexture texture{};
        if (!readOptionalIndex(item.find("source"), texture.imageIndex) ||
            texture.imageIndex < 0 || static_cast<std::size_t>(texture.imageIndex) >= images.size() ||
            !readOptionalIndex(item.find("sampler"), texture.samplerIndex) ||
            (texture.samplerIndex >= 0 &&
             static_cast<std::size_t>(texture.samplerIndex) >= samplers.size())) {
            return fail(error, "A glTF texture references an invalid image or sampler.");
        }
        textures.push_back(texture);
    }
    return true;
}

[[nodiscard]] bool parseMaterials(const JsonValue& root, std::size_t textureCount,
                                  std::vector<GltfMaterial>& materials, std::string* error) {
    const JsonValue::Array* array = getArray(root, "materials");
    if (array == nullptr) {
        if (root.find("materials") != nullptr) return fail(error, "The glTF materials member is not an array.");
        materials.clear();
        return true;
    }
    materials.clear();
    materials.reserve(array->size());
    for (const JsonValue& item : *array) {
        GltfMaterial material{};
        const JsonValue* name = item.find("name");
        if (name != nullptr && !readString(name, material.name)) {
            return fail(error, "A glTF material name is not a string.");
        }
        const JsonValue* pbr = item.find("pbrMetallicRoughness");
        if (pbr != nullptr && !pbr->isObject()) {
            return fail(error, "A glTF pbrMetallicRoughness member is not an object.");
        }
        if (pbr != nullptr) {
            const JsonValue* baseColor = pbr->find("baseColorFactor");
            if (baseColor != nullptr && !readVec4(baseColor, material.baseColorFactor)) {
                return fail(error, "A glTF baseColorFactor must contain four finite numbers.");
            }
            if (!readOptionalFloat(*pbr, "metallicFactor", 1.0f, material.metallicFactor) ||
                !readOptionalFloat(*pbr, "roughnessFactor", 1.0f, material.roughnessFactor)) {
                return fail(error, "A glTF metallic-roughness material has invalid factors.");
            }
            if (!readTextureInfo(pbr->find("baseColorTexture"), material.baseColorTexture,
                                 textureCount, "baseColor", error) ||
                !readTextureInfo(pbr->find("metallicRoughnessTexture"),
                                 material.metallicRoughnessTexture, textureCount,
                                 "metallicRoughness", error)) {
                return false;
            }
        }
        const JsonValue* normalTexture = item.find("normalTexture");
        if (normalTexture != nullptr) {
            if (!readTextureInfo(normalTexture, material.normalTexture, textureCount, "normal", error) ||
                !readOptionalFloat(*normalTexture, "scale", 1.0f, material.normalScale)) {
                return fail(error, "A glTF normal texture has invalid texture coordinates or scale.");
            }
        }
        if (!readOptionalFloat(item, "alphaCutoff", 0.5f, material.alphaCutoff)) {
            return fail(error, "A glTF material alphaCutoff is not a finite number.");
        }
        const JsonValue* doubleSided = item.find("doubleSided");
        if (doubleSided != nullptr && !doubleSided->isBool()) {
            return fail(error, "A glTF material doubleSided value is not boolean.");
        }
        material.doubleSided = doubleSided != nullptr && doubleSided->asBool();
        const JsonValue* alphaMode = item.find("alphaMode");
        if (alphaMode != nullptr) {
            std::string mode;
            if (!readString(alphaMode, mode)) return fail(error, "A glTF material alphaMode is not a string.");
            if (mode == "OPAQUE") material.alphaMode = GltfAlphaMode::Opaque;
            else if (mode == "MASK") material.alphaMode = GltfAlphaMode::Mask;
            else if (mode == "BLEND") material.alphaMode = GltfAlphaMode::Blend;
            else return fail(error, "A glTF material has an unsupported alphaMode.");
        }
        material.metallicFactor = std::clamp(material.metallicFactor, 0.0f, 1.0f);
        material.roughnessFactor = std::clamp(material.roughnessFactor, 0.0f, 1.0f);
        material.normalScale = std::clamp(material.normalScale, 0.0f, 2.0f);
        material.alphaCutoff = std::clamp(material.alphaCutoff, 0.0f, 1.0f);
        materials.push_back(std::move(material));
    }
    return true;
}

[[nodiscard]] bool readAttributeIndex(const JsonValue& attributes, const char* name,
                                      std::size_t& output, bool required,
                                      std::string* error) {
    const JsonValue* value = attributes.find(name);
    if (value == nullptr) {
        if (required) return fail(error, std::string("A glTF mesh primitive is missing ") + name + ".");
        output = 0U;
        return false;
    }
    if (!readUnsigned(value, output)) {
        return fail(error, std::string("A glTF mesh attribute ") + name + " has an invalid accessor index.");
    }
    return true;
}

[[nodiscard]] bool parseMeshes(const JsonValue& root, const std::vector<AccessorData>& accessors,
                               const std::vector<BufferViewData>& views,
                               const std::vector<std::vector<std::uint8_t>>& buffers,
                               std::size_t materialCount, std::vector<Mesh3D>& meshes,
                               std::string* error) {
    const JsonValue::Array* array = getArray(root, "meshes");
    if (array == nullptr) {
        if (root.find("meshes") != nullptr) return fail(error, "The glTF meshes member is not an array.");
        meshes.clear();
        return true;
    }
    if (array->size() > GltfLoader::kMaximumMeshes) {
        return fail(error, "The glTF mesh count exceeds the mobile safety limit.");
    }
    meshes.clear();
    meshes.reserve(array->size());
    std::size_t totalPrimitives = 0U;
    std::size_t totalVertices = 0U;
    std::size_t totalIndices = 0U;
    for (const JsonValue& meshValue : *array) {
        Mesh3D mesh{};
        const JsonValue* name = meshValue.find("name");
        if (name != nullptr && !readString(name, mesh.name)) {
            return fail(error, "A glTF mesh name is not a string.");
        }
        const JsonValue::Array* primitives = getArray(meshValue, "primitives");
        if (primitives == nullptr || primitives->empty() ||
            primitives->size() > GltfLoader::kMaximumPrimitives - totalPrimitives) {
            return fail(error, "A glTF mesh has no primitives or exceeds the primitive limit.");
        }
        totalPrimitives += primitives->size();
        mesh.primitives.reserve(primitives->size());
        for (const JsonValue& primitiveValue : *primitives) {
            const JsonValue* modeValue = primitiveValue.find("mode");
            std::int32_t mode = 4;
            if (modeValue != nullptr && !readSigned(modeValue, mode)) {
                return fail(error, "A glTF primitive mode is not an integer.");
            }
            if (mode != 4) return fail(error, "Only glTF TRIANGLES primitive mode is supported.");
            const JsonValue* attributesValue = primitiveValue.find("attributes");
            const JsonValue::Object* attributes =
                attributesValue != nullptr ? attributesValue->asObject() : nullptr;
            if (attributes == nullptr) return fail(error, "A glTF primitive attributes member is not an object.");

            std::size_t positionAccessor = 0U;
            if (!readAttributeIndex(*attributesValue, "POSITION", positionAccessor, true, error)) return false;
            if (positionAccessor >= accessors.size() ||
                accessors[positionAccessor].count > kMaximumModelVertices - totalVertices) {
                return fail(error, "The glTF model exceeds the two-million vertex safety limit.");
            }
            totalVertices += accessors[positionAccessor].count;
            std::vector<std::array<float, 4>> positions;
            if (!readFloatAttribute(positionAccessor, 3U, accessors, views, buffers, positions, error)) {
                return false;
            }
            if (positions.empty()) return fail(error, "A glTF primitive POSITION accessor is empty.");
            MeshPrimitive3D primitive{};
            primitive.vertices.resize(positions.size());
            for (std::size_t index = 0; index < positions.size(); ++index) {
                primitive.vertices[index].position =
                    {positions[index][0], positions[index][1], positions[index][2]};
            }

            bool hasNormals = false;
            bool hasTangents = false;
            bool hasTexcoords = false;
            const auto readOptionalAttribute = [&](const char* attributeName,
                                                  std::size_t componentCount,
                                                  auto&& assign, bool& found) -> bool {
                const auto iterator = attributes->find(attributeName);
                if (iterator == attributes->end()) return true;
                std::size_t accessorIndex = 0U;
                if (!readUnsigned(&iterator->second, accessorIndex)) {
                    return fail(error, std::string("A glTF ") + attributeName +
                                       " accessor index is invalid.");
                }
                std::vector<std::array<float, 4>> values;
                if (!readFloatAttribute(accessorIndex, componentCount, accessors, views, buffers,
                                        values, error)) return false;
                if (values.size() != primitive.vertices.size()) {
                    return fail(error, std::string("A glTF ") + attributeName +
                                       " count differs from POSITION.");
                }
                for (std::size_t index = 0; index < values.size(); ++index) {
                    assign(primitive.vertices[index], values[index]);
                }
                found = true;
                return true;
            };
            if (!readOptionalAttribute("NORMAL", 3U,
                                       [](Vertex3D& vertex, const auto& value) {
                                           vertex.normal = {value[0], value[1], value[2]};
                                       }, hasNormals) ||
                !readOptionalAttribute("TANGENT", 4U,
                                       [](Vertex3D& vertex, const auto& value) {
                                           vertex.tangent = {value[0], value[1], value[2], value[3]};
                                       }, hasTangents) ||
                !readOptionalAttribute("TEXCOORD_0", 2U,
                                       [](Vertex3D& vertex, const auto& value) {
                                           vertex.texcoord = {value[0], value[1]};
                                       }, hasTexcoords)) {
                return false;
            }

            const JsonValue* indexValue = primitiveValue.find("indices");
            if (indexValue != nullptr) {
                std::size_t indexAccessor = 0U;
                if (!readUnsigned(indexValue, indexAccessor)) {
                    return fail(error, "A glTF primitive index accessor is invalid.");
                }
                if (!readIndexAttribute(indexAccessor, accessors, views, buffers,
                                        primitive.indices, error)) return false;
            } else {
                primitive.indices.resize(primitive.vertices.size());
                std::iota(primitive.indices.begin(), primitive.indices.end(), 0U);
            }
            if (primitive.indices.empty() || primitive.indices.size() % 3U != 0U) {
                return fail(error, "A glTF TRIANGLES primitive has an empty or incomplete index list.");
            }
            if (primitive.indices.size() > kMaximumModelIndices - totalIndices) {
                return fail(error, "The glTF model exceeds the four-million index safety limit.");
            }
            totalIndices += primitive.indices.size();
            for (const std::uint32_t index : primitive.indices) {
                if (index >= primitive.vertices.size()) {
                    return fail(error, "A glTF primitive index exceeds its vertex count.");
                }
            }
            const JsonValue* materialValue = primitiveValue.find("material");
            if (!readOptionalIndex(materialValue, primitive.materialIndex) ||
                (primitive.materialIndex >= 0 &&
                 static_cast<std::size_t>(primitive.materialIndex) >= materialCount)) {
                return fail(error, "A glTF primitive references an invalid material.");
            }
            generateNormalsAndTangents(primitive, hasNormals, hasTangents, hasTexcoords);
            if (!computePrimitiveBounds(primitive)) {
                return fail(error, "A glTF primitive has no vertices.");
            }
            mesh.primitives.push_back(std::move(primitive));
        }
        meshes.push_back(std::move(mesh));
    }
    return true;
}

[[nodiscard]] bool parseNodes(const JsonValue& root, std::vector<GltfNode>& nodes,
                              std::string* error) {
    const JsonValue::Array* array = getArray(root, "nodes");
    if (array == nullptr) {
        if (root.find("nodes") != nullptr) return fail(error, "The glTF nodes member is not an array.");
        nodes.clear();
        return true;
    }
    if (array->size() > GltfLoader::kMaximumNodes) {
        return fail(error, "The glTF node count exceeds the mobile safety limit.");
    }
    nodes.clear();
    nodes.resize(array->size());
    for (std::size_t index = 0; index < array->size(); ++index) {
        const JsonValue& item = (*array)[index];
        GltfNode& node = nodes[index];
        const JsonValue* name = item.find("name");
        if (name != nullptr && !readString(name, node.name)) {
            return fail(error, "A glTF node name is not a string.");
        }
        if (!readOptionalIndex(item.find("mesh"), node.meshIndex)) {
            return fail(error, "A glTF node mesh index is invalid.");
        }
        const JsonValue* matrixValue = item.find("matrix");
        const bool hasTrs = item.find("translation") != nullptr || item.find("rotation") != nullptr ||
                            item.find("scale") != nullptr;
        if (matrixValue != nullptr) {
            if (hasTrs) return fail(error, "A glTF node cannot combine matrix and TRS transforms.");
            const JsonValue::Array* matrix = matrixValue->asArray();
            if (matrix == nullptr || matrix->size() != 16U) {
                return fail(error, "A glTF node matrix must contain sixteen numbers.");
            }
            for (std::size_t component = 0; component < 16U; ++component) {
                if (!readFloat(&(*matrix)[component], node.localTransform.values[component])) {
                    return fail(error, "A glTF node matrix contains a non-finite or invalid number.");
                }
            }
        } else {
            math::Vec3 translation{};
            math::Vec3 scale{1.0f, 1.0f, 1.0f};
            math::Vec4 rotation{0.0f, 0.0f, 0.0f, 1.0f};
            if ((item.find("translation") != nullptr &&
                 !readVec3(item.find("translation"), translation)) ||
                (item.find("scale") != nullptr && !readVec3(item.find("scale"), scale)) ||
                (item.find("rotation") != nullptr && !readVec4(item.find("rotation"), rotation))) {
                return fail(error, "A glTF node has a malformed TRS transform.");
            }
            const math::Quat quaternion{rotation.x, rotation.y, rotation.z, rotation.w};
            node.localTransform = math::Mat4::translation(translation) *
                                  math::Mat4::rotation(quaternion) * math::Mat4::scale(scale);
        }
        const JsonValue::Array* children = getArray(item, "children");
        if (children == nullptr && item.find("children") != nullptr) {
            return fail(error, "A glTF node children member is not an array.");
        }
        if (children != nullptr) {
            node.children.reserve(children->size());
            for (const JsonValue& childValue : *children) {
                std::size_t child = 0U;
                if (!readUnsigned(&childValue, child) || child >= nodes.size() || child == index ||
                    nodes[child].parentIndex >= 0) {
                    return fail(error, "A glTF node has an invalid or multiply-parented child.");
                }
                nodes[child].parentIndex = static_cast<std::int32_t>(index);
                node.children.push_back(static_cast<std::uint32_t>(child));
            }
        }
    }
    return true;
}

[[nodiscard]] bool parseScenes(const JsonValue& root, GltfModel& model, std::string* error) {
    const JsonValue::Array* array = getArray(root, "scenes");
    if (array == nullptr) {
        if (root.find("scenes") != nullptr) return fail(error, "The glTF scenes member is not an array.");
        model.scenes.clear();
        model.defaultScene = -1;
        return true;
    }
    model.scenes.clear();
    model.scenes.reserve(array->size());
    for (const JsonValue& item : *array) {
        std::vector<std::uint32_t> roots;
        const JsonValue::Array* nodes = getArray(item, "nodes");
        if (nodes == nullptr && item.find("nodes") != nullptr) {
            return fail(error, "A glTF scene nodes member is not an array.");
        }
        if (nodes != nullptr) {
            roots.reserve(nodes->size());
            for (const JsonValue& value : *nodes) {
                std::size_t nodeIndex = 0U;
                if (!readUnsigned(&value, nodeIndex) || nodeIndex >= model.nodes.size() ||
                    model.nodes[nodeIndex].parentIndex >= 0) {
                    return fail(error, "A glTF scene references an invalid or non-root node.");
                }
                roots.push_back(static_cast<std::uint32_t>(nodeIndex));
            }
        }
        model.scenes.push_back(std::move(roots));
    }
    const JsonValue* defaultScene = root.find("scene");
    if (defaultScene != nullptr) {
        if (!readOptionalIndex(defaultScene, model.defaultScene) ||
            static_cast<std::size_t>(model.defaultScene) >= model.scenes.size()) {
            return fail(error, "The glTF default scene index is invalid.");
        }
    } else if (!model.scenes.empty()) {
        model.defaultScene = 0;
    } else {
        model.defaultScene = -1;
    }
    std::vector<math::Mat4> ignored;
    return model.evaluateWorldTransforms(ignored, error);
}

[[nodiscard]] bool parseModelJson(const std::string& jsonText, const std::string& modelPath,
                                  const GltfAssetReader& reader,
                                  const std::vector<std::uint8_t>& glbBinary,
                                  GltfModel& output, std::string* error) {
    JsonValue::ParseResult parsed = JsonValue::parse(jsonText);
    if (!parsed.succeeded()) {
        return fail(error, "The glTF JSON is malformed at byte " +
                           std::to_string(parsed.errorOffset) + ": " + parsed.error);
    }
    const JsonValue& root = parsed.value;
    if (!root.isObject()) return fail(error, "The glTF JSON root must be an object.");
    const JsonValue* asset = root.find("asset");
    std::string version;
    if (asset == nullptr || !readString(asset->find("version"), version) || version != "2.0") {
        return fail(error, "Only glTF 2.0 assets are supported.");
    }
    const JsonValue* minimumVersion = asset->find("minVersion");
    if (minimumVersion != nullptr) {
        std::string minimumVersionText;
        if (!readString(minimumVersion, minimumVersionText) || minimumVersionText != "2.0") {
            return fail(error, "A glTF minimum version other than 2.0 is unsupported.");
        }
    }
    const JsonValue* requiredExtensions = root.find("extensionsRequired");
    if (requiredExtensions != nullptr) {
        const JsonValue::Array* extensions = requiredExtensions->asArray();
        if (extensions == nullptr || !extensions->empty()) {
            return fail(error, "The glTF asset requires an unsupported extension.");
        }
    }

    std::vector<std::vector<std::uint8_t>> buffers;
    std::vector<BufferViewData> views;
    std::vector<AccessorData> accessors;
    if (!parseBuffers(root, modelPath, reader, glbBinary, buffers, error) ||
        !parseBufferViews(root, buffers, views, error) ||
        !parseAccessors(root, views, buffers, accessors, error)) {
        return false;
    }

    GltfModel candidate{};
    if (!parseImages(root, modelPath, reader, views, buffers, candidate.images, error) ||
        !parseSamplers(root, candidate.samplers, error) ||
        !parseTextures(root, candidate.images, candidate.samplers, candidate.textures, error) ||
        !parseMaterials(root, candidate.textures.size(), candidate.materials, error) ||
        !parseMeshes(root, accessors, views, buffers, candidate.materials.size(),
                     candidate.meshes, error) ||
        !parseNodes(root, candidate.nodes, error)) {
        return false;
    }
    for (const GltfNode& node : candidate.nodes) {
        if (node.meshIndex >= 0 && static_cast<std::size_t>(node.meshIndex) >= candidate.meshes.size()) {
            return fail(error, "A glTF node references a mesh that does not exist.");
        }
    }
    if (!parseScenes(root, candidate, error)) return false;
    output = std::move(candidate);
    if (error != nullptr) error->clear();
    return true;
}

}  // namespace

bool GltfLoader::load(const std::string& assetPath, const GltfAssetReader& reader,
                      GltfModel& output, std::string* error) {
    if (assetPath.empty() || !reader) return fail(error, "A glTF asset path and reader are required.");
    std::vector<std::uint8_t> file;
    std::string readError;
    if (!reader(assetPath, file, &readError)) {
        return fail(error, readError.empty() ? "The glTF asset could not be read." : readError);
    }
    if (file.empty() || file.size() > kMaximumAssetBytes) {
        return fail(error, "The glTF document is empty or exceeds the 128 MiB safety limit.");
    }

    std::string jsonText;
    std::vector<std::uint8_t> glbBinary;
    if (!parseJsonDocument(file, jsonText, glbBinary, error)) return false;
    GltfModel candidate{};
    if (!parseModelJson(jsonText, assetPath, reader, glbBinary, candidate, error)) return false;
    output = std::move(candidate);
    if (error != nullptr) error->clear();
    return true;
}

}  // namespace box::renderer
