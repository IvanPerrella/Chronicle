#include "../../../tools/texture_assets/texture_assets.hpp"

#include <fstream>
#include <nlohmann/json.hpp>

#include "data_fixture.hpp"
#include "dataread.hpp"
#include "platform/datapacks.hpp"
#include "platform/paths.hpp"

namespace {
using namespace datafix;
using Json = nlohmann::json;

Bytes Tim2(unsigned type, unsigned width = 32, unsigned height = 16) {
    unsigned stride = type == 1 ? 2 : type == 2 ? 3 : type == 3 ? 4 : 1;
    unsigned pixels = type == 4 ? width * height / 2 : width * height * stride;
    unsigned colors = type == 4 ? 16 : type == 5 ? 256 : 0;
    Bytes bytes(64 + pixels + colors * 4);
    std::memcpy(bytes.data(), "TIM2", 4);
    bytes[4] = 4;
    bytes[6] = 1;
    Put32(bytes, 16, bytes.size() - 16);
    Put32(bytes, 20, colors * 4);
    Put32(bytes, 24, pixels);
    bytes[28] = 48;
    bytes[30] = colors & 255;
    bytes[31] = colors >> 8;
    bytes[33] = 1;
    bytes[34] = 3;
    bytes[35] = type;
    bytes[36] = width & 255;
    bytes[37] = width >> 8;
    bytes[38] = height & 255;
    bytes[39] = height >> 8;
    for (size_t i = 64; i < bytes.size(); ++i) bytes[i] = static_cast<uint8_t>(i * 37);
    return bytes;
}

Bytes Img(const Bytes &tim, bool swizzled) {
    Bytes bytes(64);
    std::memcpy(bytes.data(), swizzled ? "IM2\0" : "IMG\0", 4);
    Put32(bytes, 4, 1);
    std::memcpy(bytes.data() + 16, "texture", 8);
    Put32(bytes, 48, 64);
    bytes.insert(bytes.end(), tim.begin(), tim.end());
    return bytes;
}

class TextureAssets : public testing::Test {
protected:
    fs::path root = TempDir("texture_assets");
    void TearDown() override {
        datapacks::Clear();
        fs::remove_all(root);
    }
};

TEST_F(TextureAssets, EveryPixelFormatRoundTripsAndRemovesStandaloneTexture) {
    for (unsigned type = 1; type <= 5; ++type) {
        const auto original = Tim2(type);
        const auto name = std::to_string(type) + ".tm2";
        WriteBytes(root / name, original);
    }
    texture_assets::Convert(root);
    for (unsigned type = 1; type <= 5; ++type) {
        const auto name = std::to_string(type) + ".tm2";
        EXPECT_FALSE(fs::exists(root / name));
        EXPECT_EQ(texture_assets::Read(root, root / "textures" / name / "texture.json"), Tim2(type));
    }
    EXPECT_NO_THROW(texture_assets::Convert(root));
}

TEST_F(TextureAssets, PackedEmbeddedAndSwizzledImagesRetainOffsetsAndBytes) {
    const auto img = Img(Tim2(5, 128, 64), true);
    Bytes embedded(80, 0);
    embedded.insert(embedded.end(), img.begin(), img.end());
    const auto original = MakePack({{"texture.img", img}, {"model.bin", embedded}});
    WriteBytes(root / "pack.pak", original);
    texture_assets::Convert(root);
    const auto skeleton = ReadBytes(root / "pack.pak");
    EXPECT_EQ(skeleton.size(), original.size());
    EXPECT_NE(skeleton, original);
    EXPECT_EQ(texture_assets::Read(root, root / "textures/pack.pak/texture.json"), original);
}

TEST_F(TextureAssets, FileLoaderReconstructsNormalizedStandaloneAndPackMembers) {
    const auto original = Img(Tim2(5), false);
    const auto normalized = Img(Tim2(3), false);
    WriteBytes(root / "sheet.img", original);
    WriteBytes(root / "normalized/sheet.img", normalized);
    WriteBytes(root / "pack.pak", MakePack({{"sheet.img", original}}));
    texture_assets::Convert(root);
    PathsSetDataRoot(root);
    PathsSetSaveRoot(root / "save");
    InitCDFile();
    std::vector<u_int> buffer(4096);
    int size = 0;
    char name[] = "cdrom0:SHEET.IMG";
    ASSERT_EQ(LoadFile2(name, buffer.data(), &size, 0), 1);
    EXPECT_EQ(size, normalized.size());
    EXPECT_EQ(std::memcmp(buffer.data(), normalized.data(), normalized.size()), 0);
    char pack[] = "pack.pak", member[] = "sheet.img";
    ASSERT_EQ(LoadFile2(pack, buffer.data(), &size, 0), 1);
    auto *pixels = GetPackFile(buffer.data(), member, &size);
    ASSERT_NE(pixels, nullptr);
    EXPECT_EQ(std::memcmp(pixels, original.data(), original.size()), 0);
}

TEST_F(TextureAssets, DatapacksOverridePngAndJsonInLoadOrder) {
    const auto data = root / "data";
    WriteBytes(data / "a.tm2", Tim2(3));
    auto other = Tim2(3);
    other[64] ^= 0x7f;
    WriteBytes(data / "b.tm2", other);
    texture_assets::Convert(data);
    const auto packs = root / "datapacks";
    fs::create_directories(packs / "first/textures/a.tm2");
    fs::create_directories(packs / "last/textures/a.tm2");
    fs::copy_file(data / "textures/a.tm2/0.png", packs / "first/textures/a.tm2/0.png");
    fs::copy_file(data / "textures/b.tm2/0.png", packs / "last/textures/a.tm2/0.png");
    std::ofstream(packs / "datapacks.json") << R"({"schema_version":1,"load_order":["first","last"]})";
    datapacks::Load(data);
    const auto resolve = [&](const fs::path &relative) {
        return datapacks::Resolve(relative).value_or(data / relative);
    };
    EXPECT_EQ(texture_assets::Read(data, data / "textures/a.tm2/texture.json", resolve), other);
    EXPECT_FALSE(datapacks::Resolve("../outside.png"));
    EXPECT_FALSE(datapacks::Resolve("textures/../../outside.png"));
}

TEST_F(TextureAssets, InvalidDimensionsCannotOverwriteGameBuffer) {
    WriteBytes(root / "a.tm2", Tim2(3));
    texture_assets::Convert(root);
    const auto descriptor = root / "textures/a.tm2/texture.json";
    std::ifstream input(descriptor);
    auto metadata = Json::parse(input);
    input.close();
    metadata["images"][0]["width"] = 1;
    std::ofstream(descriptor) << metadata;
    EXPECT_THROW(texture_assets::Read(root, descriptor), std::runtime_error);
}
} // namespace
