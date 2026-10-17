#include "texture_assets.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <string>

#include <nlohmann/json.hpp>
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

namespace texture_assets {
namespace {
namespace fs = std::filesystem;
using Json = nlohmann::json;
constexpr size_t kLimit = 256 * 1024 * 1024;

uint32_t Word(const Bytes &bytes, size_t offset, unsigned count = 4, bool big = false) {
    if (offset > bytes.size() || count > bytes.size() - offset) throw std::runtime_error("truncated texture");
    uint32_t value = 0;
    for (unsigned i = 0; i < count; ++i) value |= uint32_t{bytes[offset + i]} << ((big ? count - 1 - i : i) * 8);
    return value;
}

void Put(Bytes &bytes, uint32_t value, unsigned count = 4, bool big = false) {
    for (unsigned i = 0; i < count; ++i) bytes.push_back(value >> ((big ? count - 1 - i : i) * 8));
}

Bytes Load(const fs::path &path) {
    std::ifstream input(path, std::ios::binary);
    if (!input || fs::file_size(path) > kLimit) throw std::runtime_error("unreadable or oversized asset: " + path.string());
    return Bytes(std::istreambuf_iterator<char>(input), {});
}

size_t Find(const Bytes &bytes, size_t start, size_t end, std::string_view signature) {
    const std::string_view range(reinterpret_cast<const char *>(bytes.data()) + start, end - start);
    const size_t found = range.find(signature);
    return found == std::string_view::npos ? end : start + found;
}

void Save(const fs::path &path, const Bytes &bytes) {
    fs::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(reinterpret_cast<const char *>(bytes.data()), bytes.size());
    output.close();
    if (!output) throw std::runtime_error("cannot write " + path.string());
}

void SaveJson(const fs::path &path, const Json &json) {
    const std::string text = json.dump(2) + "\n";
    Save(path, Bytes(text.begin(), text.end()));
}

std::string Utf8(const fs::path &path) {
    const auto text = path.generic_u8string();
    return {text.begin(), text.end()};
}

fs::path Safe(const fs::path &root, const std::string &name) {
    fs::path relative(std::u8string(name.begin(), name.end()));
    if (relative.empty() || relative.is_absolute() || relative.has_root_name() || name.find(':') != std::string::npos ||
        name.find('\\') != std::string::npos || name.find('\0') != std::string::npos)
        throw std::runtime_error("invalid asset path");
    for (const auto &part : relative) if (part == ".." || part == ".") throw std::runtime_error("invalid asset path");
    return root / relative;
}

std::string Hex(const Bytes &bytes) {
    constexpr char digits[] = "0123456789abcdef";
    std::string text;
    for (auto byte : bytes) { text += digits[byte >> 4]; text += digits[byte & 15]; }
    return text;
}

Bytes Unhex(const std::string &text) {
    if (text.size() % 2 || text.size() > kLimit * 2) throw std::runtime_error("invalid metadata bytes");
    const auto digit = [](char c) -> unsigned {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        throw std::runtime_error("invalid metadata digit");
    };
    Bytes bytes;
    for (size_t i = 0; i < text.size(); i += 2) bytes.push_back(digit(text[i]) * 16 + digit(text[i + 1]));
    return bytes;
}

void Chunk(Bytes &png, const char *tag, const Bytes &payload) {
    Put(png, payload.size(), 4, true);
    size_t begin = png.size();
    png.insert(png.end(), tag, tag + 4);
    png.insert(png.end(), payload.begin(), payload.end());
    uint32_t crc = ~0u;
    for (size_t i = begin; i < png.size(); ++i) {
        crc ^= png[i];
        for (unsigned bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ ((crc & 1) ? 0xedb88320u : 0);
    }
    Put(png, ~crc, 4, true);
}

Bytes Png(unsigned width, unsigned height, const Bytes &pixels, const Bytes &palette) {
    const unsigned channels = palette.empty() ? 4 : 1;
    Bytes png{137, 80, 78, 71, 13, 10, 26, 10}, header;
    Put(header, width, 4, true); Put(header, height, 4, true);
    header.insert(header.end(), {8, uint8_t(palette.empty() ? 6 : 3), 0, 0, 0});
    Chunk(png, "IHDR", header);
    if (!palette.empty()) {
        Bytes rgb, alpha;
        for (size_t i = 0; i < palette.size(); i += 4) {
            rgb.insert(rgb.end(), palette.begin() + i, palette.begin() + i + 3);
            alpha.push_back(palette[i + 3]);
        }
        Chunk(png, "PLTE", rgb); Chunk(png, "tRNS", alpha);
    }
    Bytes rows;
    for (unsigned row = 0; row < height; ++row) {
        rows.push_back(0);
        rows.insert(rows.end(), pixels.begin() + size_t(row) * width * channels,
                    pixels.begin() + size_t(row + 1) * width * channels);
    }
    Bytes deflate{0x78, 0x01};
    for (size_t offset = 0; offset < rows.size();) {
        unsigned count = std::min<size_t>(65535, rows.size() - offset);
        deflate.push_back(offset + count == rows.size() ? 1 : 0);
        Put(deflate, count, 2); Put(deflate, count ^ 65535, 2);
        deflate.insert(deflate.end(), rows.begin() + offset, rows.begin() + offset + count);
        offset += count;
    }
    uint32_t a = 1, b = 0;
    for (auto byte : rows) { a = (a + byte) % 65521; b = (b + a) % 65521; }
    Put(deflate, (b << 16) | a, 4, true);
    Chunk(png, "IDAT", deflate); Chunk(png, "IEND", {});
    return png;
}

struct Image {
    unsigned width = 0, height = 0;
    Bytes pixels, palette;
};

Image Decode(const fs::path &path, bool indexed) {
    const Bytes bytes = Load(path);
    constexpr std::array<uint8_t, 8> signature{137, 80, 78, 71, 13, 10, 26, 10};
    if (bytes.size() < 33 || !std::equal(signature.begin(), signature.end(), bytes.begin()))
        throw std::runtime_error("expected PNG: " + path.string());
    Image image;
    image.width = Word(bytes, 16, 4, true); image.height = Word(bytes, 20, 4, true);
    if (!image.width || !image.height || image.width > 16384 || image.height > 16384 ||
        uint64_t{image.width} * image.height * 4 > kLimit) throw std::runtime_error("oversized PNG");
    if (!indexed) {
        int width, height, channels;
        auto *pixels = stbi_load_from_memory(bytes.data(), bytes.size(), &width, &height, &channels, 4);
        if (!pixels) throw std::runtime_error("cannot decode PNG: " + path.string());
        image.pixels.assign(pixels, pixels + size_t(width) * height * 4);
        stbi_image_free(pixels);
        return image;
    }
    unsigned depth = bytes[24];
    if (bytes[25] != 3 || (depth != 1 && depth != 2 && depth != 4 && depth != 8) || bytes[28])
        throw std::runtime_error("palette texture requires a non-interlaced indexed PNG");
    Bytes compressed, alpha;
    for (size_t cursor = 8; cursor + 12 <= bytes.size();) {
        size_t size = Word(bytes, cursor, 4, true);
        if (size > bytes.size() - cursor - 12) throw std::runtime_error("truncated PNG chunk");
        const char *tag = reinterpret_cast<const char *>(bytes.data() + cursor + 4);
        auto begin = bytes.begin() + cursor + 8;
        if (std::memcmp(tag, "IDAT", 4) == 0) compressed.insert(compressed.end(), begin, begin + size);
        if (std::memcmp(tag, "PLTE", 4) == 0) image.palette.assign(begin, begin + size);
        if (std::memcmp(tag, "tRNS", 4) == 0) alpha.assign(begin, begin + size);
        cursor += size + 12;
    }
    if (image.palette.empty() || image.palette.size() % 3 || image.palette.size() > 768)
        throw std::runtime_error("invalid PNG palette");
    Bytes rgba;
    for (size_t i = 0; i < image.palette.size() / 3; ++i) {
        rgba.insert(rgba.end(), image.palette.begin() + i * 3, image.palette.begin() + i * 3 + 3);
        rgba.push_back(i < alpha.size() ? alpha[i] : 255);
    }
    image.palette = std::move(rgba);
    const size_t stride = (size_t(image.width) * depth + 7) / 8;
    const size_t expected = (stride + 1) * image.height;
    Bytes rows(expected);
    const int decoded = stbi_zlib_decode_buffer(reinterpret_cast<char *>(rows.data()), rows.size(),
                                               reinterpret_cast<const char *>(compressed.data()), compressed.size());
    if (decoded != static_cast<int>(expected)) throw std::runtime_error("invalid PNG raster");
    Bytes previous(stride), current(stride);
    for (unsigned y = 0; y < image.height; ++y) {
        const unsigned filter = rows[size_t(y) * (stride + 1)];
        if (filter > 4) throw std::runtime_error("invalid PNG filter");
        for (size_t x = 0; x < stride; ++x) {
            int left = x ? current[x - 1] : 0, up = previous[x], corner = x ? previous[x - 1] : 0;
            int prediction = 0;
            if (filter == 1) prediction = left;
            if (filter == 2) prediction = up;
            if (filter == 3) prediction = (left + up) / 2;
            if (filter == 4) {
                int p = left + up - corner, dl = std::abs(p - left), du = std::abs(p - up), dc = std::abs(p - corner);
                prediction = dl <= du && dl <= dc ? left : du <= dc ? up : corner;
            }
            current[x] = rows[size_t(y) * (stride + 1) + 1 + x] + prediction;
        }
        for (unsigned x = 0; x < image.width; ++x) {
            uint8_t index = (current[size_t(x) * depth / 8] >> (8 - depth - (x * depth % 8))) & ((1u << depth) - 1);
            if (size_t(index) * 4 >= image.palette.size()) throw std::runtime_error("PNG palette index out of bounds");
            image.pixels.push_back(index);
        }
        previous.swap(current);
    }
    return image;
}

unsigned Csm1(unsigned index) {
    return (index & ~0x18u) | ((index & 8) << 1) | ((index & 16) >> 1);
}

// Conv8to32 stores 16x16 blocks in alternating GS columns, including partial pages.
long Swizzled(unsigned width, unsigned height, unsigned x, unsigned y) {
    constexpr uint8_t lut[128] = {
        0,36,8,44,1,37,9,45,2,38,10,46,3,39,11,47,4,32,12,40,5,33,13,41,6,34,14,42,7,35,15,43,
        16,52,24,60,17,53,25,61,18,54,26,62,19,55,27,63,20,48,28,56,21,49,29,57,22,50,30,58,23,51,31,59,
        4,32,12,40,5,33,13,41,6,34,14,42,7,35,15,43,0,36,8,44,1,37,9,45,2,38,10,46,3,39,11,47,
        20,48,28,56,21,49,29,57,22,50,30,58,23,51,31,59,16,52,24,60,17,53,25,61,18,54,26,62,19,55,27,63};
    unsigned pages_x = (width + 127) / 128, pages_y = (height + 63) / 64;
    unsigned page_width = pages_x == 1 ? width : 128, row_bytes = pages_x == 1 ? width * 2 : 256;
    unsigned rows = pages_y == 1 ? height / 2 : 32;
    unsigned local_x = x % page_width, local_y = y & 63, source = (local_y & 15) * 16 + (local_x & 15);
    unsigned column = source / 64, inverse = 0;
    while (lut[(column & 1) * 64 + inverse] != (source & 63)) ++inverse;
    unsigned output = column * 64 + inverse, row = (local_y / 16) * 8 + output / 32;
    unsigned byte = (local_x / 16) * 32 + output % 32;
    if (row >= rows || byte >= row_bytes) return -1;
    return long(y / 64) * pages_x * row_bytes * rows + long(row_bytes) * (x / page_width) +
           long(row) * row_bytes * pages_x + byte;
}

Bytes Colors(const Bytes &raw, unsigned type, Json &excess) {
    const unsigned stride = type == 1 ? 2 : type == 2 ? 3 : 4;
    Bytes rgba;
    for (size_t i = 0; i < raw.size() / stride; ++i) {
        if (type == 1) {
            unsigned word = Word(raw, i * 2, 2);
            rgba.insert(rgba.end(), {uint8_t((word & 31) << 3), uint8_t(((word >> 5) & 31) << 3),
                                    uint8_t(((word >> 10) & 31) << 3), uint8_t(word & 0x8000 ? 255 : 0)});
        } else {
            rgba.insert(rgba.end(), raw.begin() + i * stride, raw.begin() + i * stride + 3);
            unsigned alpha = stride == 4 ? raw[i * stride + 3] : 128;
            rgba.push_back(std::min(255u, alpha * 2));
            if (alpha > 128) excess.push_back({i, alpha});
        }
    }
    return rgba;
}

Bytes NativeColors(const Bytes &rgba, unsigned type, const Json &excess) {
    Bytes raw;
    for (size_t i = 0; i < rgba.size(); i += 4) {
        if (type == 1) {
            Put(raw, (rgba[i] >> 3) | ((rgba[i + 1] >> 3) << 5) | ((rgba[i + 2] >> 3) << 10) |
                         (rgba[i + 3] >= 128 ? 0x8000 : 0), 2);
        } else {
            raw.insert(raw.end(), rgba.begin() + i, rgba.begin() + i + 3);
            if (type == 3) raw.push_back((unsigned(rgba[i + 3]) + 1) / 2);
        }
    }
    if (type == 3) for (const auto &pair : excess) {
        size_t index = pair.at(0).get<size_t>();
        if (index >= rgba.size() / 4) throw std::runtime_error("invalid alpha metadata");
        if (rgba[index * 4 + 3] == 255) raw[index * 4 + 3] = pair.at(1).get<uint8_t>();
    }
    return raw;
}

void WriteAt(Bytes &target, size_t offset, const Bytes &bytes) {
    if (offset > target.size() || bytes.size() > target.size() - offset) throw std::runtime_error("texture exceeds file layout");
    std::copy(bytes.begin(), bytes.end(), target.begin() + offset);
}

} // namespace

static Bytes ReadLayout(const fs::path &root, const Json &metadata, const Resolver &resolve,
                        const Bytes *container = nullptr) {
    if (metadata.at("schema_version") != 1) throw std::runtime_error("unsupported texture layout schema");
    size_t size = metadata.at("size").get<size_t>();
    if (size > kLimit) throw std::runtime_error("oversized texture source");
    Bytes output(size);
    if (metadata.contains("container"))
        output = container ? *container : Load(Safe(root, metadata.at("container").get<std::string>()));
    if (output.size() != size) throw std::runtime_error("container size differs from texture layout");
    for (const auto &chunk : metadata.at("metadata"))
        WriteAt(output, chunk.at("offset"), Unhex(chunk.at("bytes")));
    for (const auto &level : metadata.at("images")) {
        const std::string name = level.at("png");
        const auto base = Safe(root, name);
        Image image = Decode(resolve ? resolve(base.lexically_relative(root)) : base, level.at("type").get<unsigned>() >= 4);
        unsigned width = level.at("width"), height = level.at("height"), type = level.at("type");
        if (image.width != width || image.height != height) throw std::runtime_error("PNG dimensions differ from texture layout: " + name);
        Bytes raw;
        if (type >= 4) {
            raw.resize(type == 4 ? (size_t(width) * height + 1) / 2 : size_t(width) * height);
            bool swizzled = level.value("swizzled", false);
            for (unsigned y = 0; y < height; ++y) for (unsigned x = 0; x < width; ++x) {
                size_t logical = size_t(y) * width + x;
                long offset = swizzled ? Swizzled(width, height, x, y) : static_cast<long>(logical);
                if (offset < 0) continue;
                if (type == 4) {
                    if (image.pixels[logical] > 15) throw std::runtime_error("4-bit palette index exceeds 15");
                    raw.at(offset / 2) |= image.pixels[logical] << ((offset & 1) * 4);
                } else raw.at(offset) = image.pixels[logical];
            }
            if (level.contains("palette_offset")) {
                unsigned colors = level.at("colors"), palette_type = level.at("palette_type");
                if (image.palette.size() / 4 != colors) throw std::runtime_error("PNG must preserve the palette size");
                Bytes palette = NativeColors(image.palette, palette_type, level.at("palette_alpha"));
                unsigned stride = palette_type == 1 ? 2 : 4;
                if (level.value("csm1", false)) {
                    Bytes reordered(palette.size());
                    for (unsigned index = 0; index < colors; ++index)
                        std::copy_n(palette.begin() + Csm1(index) * stride, stride, reordered.begin() + index * stride);
                    palette = std::move(reordered);
                }
                WriteAt(output, level.at("palette_offset"), palette);
            }
        } else raw = NativeColors(image.pixels, type, level.at("alpha"));
        WriteAt(output, level.at("offset"), raw);
    }
    return output;
}

Bytes Read(const fs::path &root, const fs::path &descriptor, const Resolver &resolve) {
    return ReadLayout(root, Json::parse(Load(descriptor)), resolve);
}

void Convert(const fs::path &root, const std::function<bool()> &proceed) {
    const auto assets = root / "textures";
    const auto catalog_path = assets / "catalog.json";
    if (fs::exists(catalog_path)) return;
    Json catalog{{"schema_version", 1}, {"sources", Json::array()}};
    std::vector<fs::path> files;
    for (const auto &entry : fs::recursive_directory_iterator(root))
        if (entry.is_regular_file() && entry.path().lexically_relative(root).begin()->string() != "textures" &&
            entry.file_size() <= kLimit) files.push_back(entry.path());
    std::sort(files.begin(), files.end());
    struct Pending { fs::path file; Bytes skeleton; bool standalone; };
    std::vector<Pending> pending;
    std::fprintf(stdout, "textures: scanning %zu files for PNG conversion\n", files.size());
    for (const auto &file : files) {
        if (proceed && !proceed()) throw std::runtime_error("texture conversion cancelled");
        Bytes original = Load(file), skeleton = original;
        std::string relative = Utf8(file.lexically_relative(root));
        const auto folder = fs::path("textures") / file.lexically_relative(root);
        Json metadata{{"schema_version", 1}, {"size", original.size()}, {"metadata", Json::array()}, {"images", Json::array()}};
        bool standalone = original.size() >= 4 && (std::memcmp(original.data(), "TIM2", 4) == 0 ||
                          std::memcmp(original.data(), "IMG\0", 4) == 0 || std::memcmp(original.data(), "IM2\0", 4) == 0 ||
                          std::memcmp(original.data(), "IM3\0", 4) == 0);
        size_t serial = 0;
        for (size_t pos = 0; pos + 64 <= original.size(); ++pos) {
            pos = Find(original, pos, original.size(), "TIM2");
            if (pos + 64 > original.size()) break;
            unsigned pictures = Word(original, pos + 6, 2);
            if (original[pos + 5] > 1 || !pictures || pictures > 256) continue;
            size_t picture = pos + (original[pos + 5] ? 128 : 16);
            // Validate a signature before treating it as a texture; embedded binaries may contain literal "TIM2".
            if (picture + 48 > original.size()) continue;
            unsigned first_type = original[picture + 19];
            if (first_type < 1 || first_type > 5 || !Word(original, picture + 20, 2) || !Word(original, picture + 22, 2)) continue;
            bool swizzled = false;
            for (size_t header = 0; header + 16 <= pos; ++header) {
                header = Find(original, header, pos, std::string_view("IM2\0", 4));
                if (header == pos) break;
                unsigned count = Word(original, header + 4);
                if (count > 16384 || header + 16 + size_t(count) * 48 > original.size()) continue;
                for (unsigned index = 0; index < count; ++index)
                    if (header + Word(original, header + 16 + index * 48 + 32) == pos) swizzled = true;
            }
            for (unsigned index = 0; index < pictures; ++index) {
                unsigned total = Word(original, picture), palette_bytes = Word(original, picture + 4), image_bytes = Word(original, picture + 8);
                unsigned header = Word(original, picture + 12, 2), colors = Word(original, picture + 14, 2);
                unsigned mips = original.at(picture + 17), clut_type = original.at(picture + 18), type = original.at(picture + 19);
                unsigned width = Word(original, picture + 20, 2), height = Word(original, picture + 22, 2);
                if (header < 48 || type < 1 || type > 5 || !width || !height || width > 16384 || height > 16384 ||
                    picture + header + uint64_t(image_bytes) + palette_bytes > original.size())
                    throw std::runtime_error("invalid TIM2 in " + relative);
                size_t raster = picture + header, palette_offset = raster + image_bytes;
                Bytes palette;
                Json palette_alpha = Json::array();
                unsigned palette_type = (clut_type & 63) == 1 ? 1 : 3;
                bool csm1 = type == 5 && (clut_type & 0xc0) == 0;
                if (type >= 4 && palette_bytes) {
                    if (!colors || colors > 256 || (csm1 && colors != 256)) throw std::runtime_error("invalid palette in " + relative);
                    unsigned stride = palette_type == 1 ? 2 : 4;
                    if (colors * stride > palette_bytes) throw std::runtime_error("truncated palette");
                    Bytes raw(original.begin() + palette_offset, original.begin() + palette_offset + colors * stride);
                    if (csm1) {
                        Bytes ordered(raw.size());
                        for (unsigned color = 0; color < colors; ++color)
                            std::copy_n(raw.begin() + Csm1(color) * stride, stride, ordered.begin() + color * stride);
                        raw = std::move(ordered);
                    }
                    palette = Colors(raw, palette_type, palette_alpha);
                    std::fill_n(skeleton.begin() + palette_offset, colors * stride, 0);
                }
                if (type >= 4 && image_bytes && palette.empty()) throw std::runtime_error("indexed image without palette");
                if (mips > 16 || (mips > 1 && header < 64 + mips * 4)) throw std::runtime_error("invalid mip header");
                for (unsigned mip = 0; image_bytes && mip < std::max(1u, mips); ++mip) {
                    unsigned w = std::max(1u, width >> mip), h = std::max(1u, height >> mip);
                    size_t bytes = type == 4 ? (size_t(w) * h + 1) / 2 : size_t(w) * h * (type == 1 ? 2 : type == 2 ? 3 : type == 3 ? 4 : 1);
                    if (raster + bytes > picture + header + image_bytes) throw std::runtime_error("mip exceeds TIM2 raster");
                    Bytes raw(original.begin() + raster, original.begin() + raster + bytes), pixels;
                    Json alpha = Json::array();
                    if (type >= 4) {
                        for (unsigned y = 0; y < h; ++y) for (unsigned x = 0; x < w; ++x) {
                            long offset = swizzled && type == 5 ? Swizzled(w, h, x, y) : long(y) * w + x;
                            pixels.push_back(offset < 0 ? 0 : type == 4 ? ((raw.at(offset / 2) >> ((offset & 1) * 4)) & 15) : raw.at(offset));
                        }
                    } else pixels = Colors(raw, type, alpha);
                    const std::string png = Utf8(folder / (std::to_string(serial++) + ".png"));
                    Save(Safe(root, png), Png(w, h, pixels, palette));
                    Json level{{"offset", raster}, {"png", png}, {"width", w}, {"height", h}, {"type", type},
                               {"swizzled", swizzled && type == 5}, {"alpha", alpha}};
                    if (mip == 0 && !palette.empty()) {
                        level["palette_offset"] = palette_offset; level["colors"] = colors;
                        level["palette_type"] = palette_type; level["csm1"] = csm1; level["palette_alpha"] = palette_alpha;
                    }
                    metadata["images"].push_back(level);
                    std::fill_n(skeleton.begin() + raster, bytes, 0);
                    raster += mips > 1 ? Word(original, picture + 64 + mip * 4) : bytes;
                }
                size_t serialized = size_t(header) + image_bytes + palette_bytes;
                picture += total >= serialized && total <= original.size() - picture ? total : serialized;
            }
            pos = std::max(pos, picture - 1);
        }
        if (metadata["images"].empty() && !standalone) continue;
        if (standalone) {
            for (size_t offset = 0; offset < skeleton.size();) {
                if (!skeleton[offset]) { ++offset; continue; }
                size_t end = offset + 1;
                while (end < skeleton.size() && skeleton[end]) ++end;
                metadata["metadata"].push_back({{"offset", offset}, {"bytes", Hex(Bytes(skeleton.begin() + offset, skeleton.begin() + end))}});
                offset = end;
            }
        } else metadata["container"] = relative;
        const auto descriptor = folder / "texture.json";
        SaveJson(root / descriptor, metadata);
        // Verify every byte before replacing any original file.
        if (ReadLayout(root, metadata, {}, &skeleton) != original)
            throw std::runtime_error("PNG round-trip differs: " + relative);
        catalog["sources"].push_back({{"path", relative}, {"descriptor", Utf8(descriptor)}, {"size", original.size()}});
        pending.push_back({file, std::move(skeleton), standalone});
    }
    if (proceed && !proceed()) throw std::runtime_error("texture conversion cancelled");
    SaveJson(catalog_path, catalog);
    for (const auto &item : pending) {
        if (item.standalone) fs::remove(item.file);
        else Save(item.file, item.skeleton);
    }
    std::fprintf(stdout, "textures: verified and converted %zu sources\n", pending.size());
}

} // namespace texture_assets
