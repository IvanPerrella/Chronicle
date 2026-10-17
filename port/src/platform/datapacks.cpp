#include "platform/datapacks.hpp"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <stdexcept>
#include <unordered_map>
#include <utility>

#include <nlohmann/json.hpp>


namespace datapacks {
namespace {

/**
 * Stores one pack's canonical root and case-insensitive asset index.
 */
struct Pack {
    std::filesystem::path root; /**< Canonical directory containing the pack. */
    std::unordered_map<std::string, std::filesystem::path> files; /**< Relative asset keys mapped to on-disk paths. */
};

std::filesystem::path retail_root;
/** Enabled packs in increasing priority order. */
std::vector<Pack> packs;
/**
 * Converts a filesystem path to UTF-8 for keys and diagnostics.
 */
std::string Display(const std::filesystem::path &path) {
    const auto utf8 = path.generic_u8string();
    return std::string(utf8.begin(), utf8.end());
}

/**
 * Builds an ASCII case-insensitive asset lookup key.
 */
std::string Key(const std::filesystem::path &path) {
    std::string key = Display(path);
    for (char &character : key) {
        if (character >= 'A' && character <= 'Z') character += 'a' - 'A';
    }
    return key;
}

/**
 * Accepts relative asset paths without traversal or alternate-root syntax.
 */
bool Relative(const std::filesystem::path &path) {
    const auto text = Display(path);
    if (path.empty() || path.is_absolute() || path.has_root_name() || path.has_root_directory() ||
        text.find('\\') != std::string::npos || text.find(':') != std::string::npos ||
        text.find('\0') != std::string::npos) return false;
    for (const auto &part : path) {
        if (part.empty() || part == "." || part == "..") return false;
    }
    return true;
}

/**
 * Checks that a canonical path is a strict descendant of its permitted root.
 */
bool Within(const std::filesystem::path &root, const std::filesystem::path &path) {
    auto base = root.begin();
    auto candidate = path.begin();
    for (; base != root.end(); ++base, ++candidate) {
        if (candidate == path.end()) return false;
#ifdef _WIN32
        if (Key(*base) != Key(*candidate)) return false;
#else
        if (*base != *candidate) return false;
#endif
    }
    return candidate != path.end();
}

/**
 * Rejects missing files and symlinks that escape their pack root.
 */
bool SafeFile(const Pack &pack, const std::filesystem::path &path) {
    std::error_code error;
    const auto canonical = std::filesystem::weakly_canonical(path, error);
    return !error && Within(pack.root, canonical) && std::filesystem::is_regular_file(canonical, error) && !error;
}

/**
 * Looks up a pack asset and rechecks its current filesystem containment.
 */
std::optional<std::filesystem::path> PackFile(const Pack &pack, const std::filesystem::path &relative) {
    const auto found = pack.files.find(Key(relative));
    if (found == pack.files.end() || !SafeFile(pack, found->second)) return std::nullopt;
    return found->second;
}

/**
 * Reports a rejected configuration or asset with its path.
 */
void Diagnostic(const std::filesystem::path &path, const std::string &message) {
    std::fprintf(stderr, "datapacks: %s: %s\n", Display(path).c_str(), message.c_str());
}

} // namespace

/**
 * Clears all configured pack roots and their asset indexes.
 */
void Clear() {
    packs.clear();
    retail_root.clear();
}

/**
 * Loads and validates ordered pack folders beside the converted-data root.
 */
void Load(const std::filesystem::path &port_data_root) {
    Clear();
    std::error_code error;
    retail_root = std::filesystem::weakly_canonical(port_data_root, error);
    if (error) {
        Diagnostic(port_data_root, "cannot establish the converted-data root; packs disabled");
        return;
    }
    const auto directory = retail_root.parent_path() / "datapacks";
    const auto config = directory / "datapacks.json";
    if (!std::filesystem::exists(config, error) && !error) return;
    try {
        if (error || !std::filesystem::is_regular_file(config) || std::filesystem::file_size(config) > 1024 * 1024)
            throw std::runtime_error("configuration is unreadable or exceeds 1 MiB");
        std::ifstream input(config, std::ios::binary);
        if (!input) throw std::runtime_error("cannot open configuration");
        const auto root = nlohmann::json::parse(input);
        if (!root.is_object() || !root.contains("schema_version") || !root["schema_version"].is_number_integer() ||
            root["schema_version"] != 1 || !root.contains("load_order") || !root["load_order"].is_array())
            throw std::runtime_error("expected schema_version 1 and a load_order array");
        const auto pack_directory = std::filesystem::weakly_canonical(directory);
        std::vector<Pack> configured;
        for (const auto &item : root["load_order"]) {
            if (!item.is_string()) throw std::runtime_error("load_order entries must be folder names");
            const auto folder = item.get<std::string>();
            const std::filesystem::path name(std::u8string(folder.begin(), folder.end()));
            if (!Relative(name) || std::distance(name.begin(), name.end()) != 1)
                throw std::runtime_error("load_order contains an unsafe folder name");
            Pack pack;
            pack.root = std::filesystem::weakly_canonical(directory / name);
            if (!Within(pack_directory, pack.root)) throw std::runtime_error("pack folder escapes datapacks");
            if (!std::filesystem::is_directory(pack.root)) {
                Diagnostic(directory / name, "pack folder is missing; skipped");
                continue;
            }
            for (const auto &file : std::filesystem::recursive_directory_iterator(pack.root)) {
                if (!file.is_regular_file() || !SafeFile(pack, file.path())) continue;
                const auto relative = file.path().lexically_relative(pack.root);
                const auto key = Key(relative);
                if (!pack.files.emplace(key, file.path()).second)
                    throw std::runtime_error("pack contains paths differing only by ASCII case");
            }
            configured.push_back(std::move(pack));
        }
        packs = std::move(configured);
    } catch (const std::exception &exception) {
        packs.clear();
        Diagnostic(config, std::string(exception.what()) + "; packs disabled");
    }
}

/**
 * Resolves a relative game-data asset from the highest-priority valid pack.
 */
std::optional<std::filesystem::path> Resolve(const std::filesystem::path &relative) {
    if (!Relative(relative)) return std::nullopt;
    for (auto pack = packs.rbegin(); pack != packs.rend(); ++pack) {
        if (auto path = PackFile(*pack, relative)) return path;
    }
    return std::nullopt;
}

} // namespace datapacks
