/*
 * DYLD Cache Analyser.
 *
 * Copyright (c) 2026 Visual Ehrmanntraut (VisualEhrmanntraut).
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once
#include "bit.hpp"
#include "cache.hpp"
#include "macho.hpp"
#include "nlist.hpp"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

class IImageMatch {
    public:
    explicit IImageMatch() = default;
    explicit IImageMatch(const IImageMatch &) = default;
    explicit IImageMatch(IImageMatch &&) = delete;
    auto operator=(const IImageMatch &) -> IImageMatch & = default;
    auto operator=(IImageMatch &&) -> IImageMatch & = delete;
    virtual ~IImageMatch() = default;
    [[nodiscard]] virtual auto matches(const std::string_view &str) const noexcept -> bool = 0;
    [[nodiscard]] virtual auto name() const -> std::string = 0;
};

class ImageMatch : public IImageMatch {
    std::string_view path;

    public:
    explicit constexpr ImageMatch(std::string_view path) : path(path) {}

    [[nodiscard]] auto matches(const std::string_view &str) const noexcept -> bool override {
        return this->path == str;
    }

    [[nodiscard]] auto name() const -> std::string override { return std::string(this->path); }
};

class IFrameworkMatch : public IImageMatch {
    std::string_view framework_name;

    static constexpr std::string_view FRAMEWORK_EXT = ".framework/";
    static constexpr std::size_t FRAMEWORK_EXT_SIZE = FRAMEWORK_EXT.size();
    static constexpr std::string_view VERSIONS_A = "Versions/A/";
    static constexpr std::size_t VERSIONS_A_SIZE = VERSIONS_A.size();

    public:
    explicit constexpr IFrameworkMatch(std::string_view name) : framework_name(name) {}

    [[nodiscard]] auto name() const -> std::string override { return std::string(this->framework_name) + ".framework"; }

    protected:
    [[nodiscard]] auto matches_with_base(const std::string_view &base_dir, const std::string_view &str) const noexcept
        -> bool {
        const auto name_size = this->framework_name.size();
        const auto base_dir_size = base_dir.size();
        const auto name_off = base_dir_size + name_size;
        const auto name_framework_off = name_off + FRAMEWORK_EXT_SIZE;
        const auto name_versions_a_off = name_framework_off + VERSIONS_A_SIZE;
        try {
            return str.substr(0, base_dir_size) == base_dir &&
                   str.substr(base_dir_size, name_size) == this->framework_name &&
                   str.substr(name_off, FRAMEWORK_EXT_SIZE) == FRAMEWORK_EXT &&
                   (str.substr(name_framework_off, name_size) == this->framework_name ||
                       (str.substr(name_framework_off, VERSIONS_A_SIZE) == VERSIONS_A &&
                           str.substr(name_versions_a_off, name_size) == this->framework_name));
        } catch (std::out_of_range const &) { return false; }
    }
};

class FrameworkMatch : public IFrameworkMatch {
    public:
    explicit constexpr FrameworkMatch(const std::string_view &name) : IFrameworkMatch(name) {}

    [[nodiscard]] auto matches(const std::string_view &str) const noexcept -> bool override {
        return this->matches_with_base("/System/Library/Frameworks/", str);
    }
};

class PrivateFrameworkMatch : public IFrameworkMatch {
    public:
    explicit constexpr PrivateFrameworkMatch(const std::string_view &name) : IFrameworkMatch(name) {}

    [[nodiscard]] auto matches(const std::string_view &str) const noexcept -> bool override {
        return this->matches_with_base("/System/Library/PrivateFrameworks/", str);
    }
};

struct CacheAnalyser {
    using SymbolMap = std::unordered_map<std::string, std::uint64_t>;
    using ObjCClassMap = std::unordered_map<std::string, std::uint64_t>;

    struct Image {
        const std::filesystem::path &path;
        const DyldCacheHeader &header;
        std::streamoff file_off {0};
        std::uint64_t vm_addr {0};
        SymbolMap symbols;
        ObjCClassMap objc_classes;

        explicit Image(const std::filesystem::path &path, const DyldCacheHeader &header, const std::streamoff file_off,
            const std::uint64_t vm_addr, SymbolMap &&symbols, ObjCClassMap &&objc_classes)
            : path(path), header(header), file_off(file_off), vm_addr(vm_addr), symbols(std::move(symbols)),
              objc_classes(std::move(objc_classes)) {}

        template<typename... Variants>
        [[nodiscard]] auto resolve_sym(Variants... variants) const
            -> std::enable_if_t<(sizeof...(Variants) != 0), std::uint64_t> {
            const auto variant_list = {variants...};
            for (const auto &variant : variant_list) {
                if (const auto sym = this->symbols.find(std::string {variant}); sym != this->symbols.end()) {
                    return sym->second;
                }
            }
            throw std::out_of_range("symbol `" + std::string(*variant_list.begin()) + "` not found");
        }

        [[nodiscard]] auto resolve_objc_class(const std::string &name) const -> std::uint64_t {
            const auto cls = this->objc_classes.find(name);
            if (cls == this->objc_classes.end()) {
                throw std::out_of_range("Objective-C class `" + name + "` not found");
            }
            return cls->second;
        }
    };

    using Entry = std::pair<std::filesystem::path, DyldCacheHeader>;

    std::vector<Entry> caches;
    std::unique_ptr<Entry> symbols_cache;

    explicit CacheAnalyser(const std::filesystem::path &base_path) {
        std::ifstream main_file(base_path, std::ios::binary | std::ios::in);
        if (!main_file.is_open()) { throw std::runtime_error("failed to open main cache file"); }

        DyldCacheHeader temp_main_header(main_file, DyldCacheHeader::Type::Main);
        this->caches.reserve(1 + temp_main_header.subcaches.size());
        const auto &main_header = this->caches.emplace_back(base_path, std::move(temp_main_header)).second;

        for (const auto &subcache : main_header.subcaches) {
            auto sub_path = base_path;
            sub_path += subcache.suffix;

            std::ifstream sub_file(sub_path, std::ios::binary | std::ios::in);
            if (!sub_file.is_open()) {
                throw std::runtime_error("failed to open subcache at `" + sub_path.string() + '`');
            }

            this->caches.emplace_back(std::move(sub_path),
                DyldCacheHeader(sub_file, DyldCacheHeader::Type::Sub, main_header.cache_base));
        }

        if (main_header.symbol_file_uuid) {
            auto sub_path = base_path;
            sub_path += ".symbols";

            std::ifstream sub_file(sub_path, std::ios::binary | std::ios::in);
            if (!sub_file.is_open()) {
                throw std::runtime_error("failed to open symbols subcache at `" + sub_path.string() + '`');
            }

            this->symbols_cache = std::make_unique<Entry>(std::move(sub_path),
                DyldCacheHeader(sub_file, DyldCacheHeader::Type::Symbols, main_header.cache_base));
        }
    }

    [[nodiscard]] auto find_entry_from_vm_addr(const std::uint64_t vm_addr) const
        -> std::pair<std::streamoff, const Entry &> {
        for (const auto &entry : this->caches) {
            try {
                const auto offset = entry.second.vm_addr_to_file_off(vm_addr);
                return {offset, entry};
            } catch (const std::out_of_range &e) { continue; }
        }
        throw std::out_of_range("address " + std::to_string(vm_addr) + " not found");
    }

    [[nodiscard]] auto main_cache() const -> const Entry & { return this->caches.front(); }

    // A bit of a brute-force approach, as I don't want to parse the chained fixups just
    // for this crap. A sincere fuck you and go to hell to however came up with that crap!
    [[nodiscard]] auto read_ptr_at(std::ifstream &cache_file, const std::uint64_t image_base,
        const std::streamoff off) const -> std::uint64_t {
        seek_stream(cache_file, off);
        const auto fixup = read_u64_le(cache_file);
        std::uint64_t val;
        if (bit_test(fixup, 63)) {        // let's see if it's auth_rebase
            if (bit_test(fixup, 62)) {    // bind bit set, is auth_bind
                throw std::runtime_error("stumbled upon auth_bind pointer (" + std::to_string(fixup) + ")");
            }
            if (bit_extract(fixup, 32, 19) == 0) {    // reserved bits zero, is bind
                throw std::runtime_error("stumbled upon bind pointer (" + std::to_string(fixup) + ")");
            }
            // Okay, it's probably auth_rebase. Extract only the target.
            val = bit_extract(fixup, 0, 32);
        } else {
            // We hit rebase, hopefully.
            val = bit_extract(fixup, 0, 36);
        }
        return val > image_base ? val : (val + this->main_cache().second.cache_base);
    }

    [[nodiscard]] auto read_ptr_at(const std::uint64_t image_base, const std::uint64_t vm_addr) const -> std::uint64_t {
        const auto [off, entry] = this->find_entry_from_vm_addr(vm_addr);
        std::ifstream cache_file(entry.first);
        return this->read_ptr_at(cache_file, image_base, off);
    }

    [[nodiscard]] auto find_image(const IImageMatch &image_match, bool with_objc_classes = false) const -> Image {
        const auto &main_cache = this->main_cache();
        const auto &images = main_cache.second.images;
        const auto image = std::find_if(images.begin(), images.end(),
            [&image_match](const auto &image) -> bool { return image_match.matches(image.path); });
        if (image == images.end()) { throw std::out_of_range("image `" + image_match.name() + "` not found"); }

        const auto [image_off, image_cache_entry] = this->find_entry_from_vm_addr(image->base);

        SymbolMap symbols;

        std::ifstream image_cache_file(image_cache_entry.first);

        seek_stream(image_cache_file, image_off);
        const MachHeader image_header(image_cache_file);

        const auto &linkedit = image_header.find_segment("__LINKEDIT");
        const auto linkedit_base = linkedit.vm_addr - linkedit.file_off;

        if (image_header.symtab) {
            const auto &symtab = *image_header.symtab;
            const auto [symtab_off, symtab_entry] = this->find_entry_from_vm_addr(linkedit_base + symtab.sym_off);
            const auto [str_off, str_entry] = this->find_entry_from_vm_addr(linkedit_base + symtab.str_off);
            std::ifstream symtab_file(symtab_entry.first);
            seek_stream(symtab_file, symtab_off);
            std::ifstream str_file(str_entry.first);

            symbols.reserve(symtab.sym_count);
            for (std::uint32_t sym_i = 0; sym_i < symtab.sym_count; ++sym_i) {
                const NList sym_entry(symtab_file);

                if (sym_entry.strx == 0) { continue; }
                if (sym_entry.type_flags.bits.type != NList::Type::Section) { continue; }

                seek_stream(str_file, str_off + std::streamoff(sym_entry.strx));
                auto name = read_cstr(str_file);

                if (name.empty() || name == "<redacted>") { continue; }

                symbols.emplace(std::move(name), sym_entry.value);
            }
        }

        const auto &symbols_cache = this->symbols_cache ? *this->symbols_cache : main_cache;
        const auto &symbols_cache_header = symbols_cache.second;
        const auto symbols_off = symbols_cache_header.local_symbols_off;
        const auto &local_symbols = symbols_cache_header.local_symbols;
        const auto symbols_entry = local_symbols.entries.find(image->base);
        if (symbols_entry != local_symbols.entries.end()) {
            symbols.reserve(symbols.size() + symbols_entry->second.nlist_count);
            std::ifstream symbols_file(symbols_cache.first);
            seek_stream(symbols_file, symbols_off + local_symbols.nlist_off +
                                          std::streamoff(symbols_entry->second.nlist_start_index * sizeof(NList)));
            const auto strings_off = local_symbols.strings_off;
            for (std::uint32_t i = 0; i < symbols_entry->second.nlist_count; ++i) {
                const NList nlist(symbols_file);

                if (nlist.strx == 0) { continue; }
                if (nlist.type_flags.bits.type != NList::Type::Section) { continue; }

                const auto prev_pos = symbols_file.tellg();
                seek_stream(symbols_file, symbols_off + strings_off + std::streamoff(nlist.strx));
                auto name = read_cstr(symbols_file);
                seek_stream(symbols_file, prev_pos);

                if (name.empty() || name == "<redacted>") { continue; }

                symbols.emplace(std::move(name), nlist.value);
            }
        }

        ObjCClassMap objc_classes;

        if (with_objc_classes) {
            const auto &class_list = image_header.find_section("__DATA_CONST", "__objc_classlist");
            const auto [class_list_off, class_list_entry] = this->find_entry_from_vm_addr(class_list.vm_addr);
            std::ifstream class_list_cache_file(class_list_entry.first);
            const auto end_off = class_list_off + std::streamoff(class_list.vm_size);
            for (std::streamoff cur_off = class_list_off; cur_off < end_off; cur_off += 8) {
                const auto class_addr = this->read_ptr_at(class_list_cache_file, image->base, cur_off);
                const auto class_ro_addr = this->read_ptr_at(class_list_cache_file, image->base,
                    class_list_entry.second.vm_addr_to_file_off(class_addr + 0x20));
                const auto class_name_addr = this->read_ptr_at(class_list_cache_file, image->base,
                    class_list_entry.second.vm_addr_to_file_off(class_ro_addr + 0x18));
                const auto [class_name_file_off, class_name_entry] = this->find_entry_from_vm_addr(class_name_addr);
                std::ifstream class_name_file(class_name_entry.first);
                seek_stream(class_name_file, class_name_file_off);
                auto class_name = read_cstr(class_name_file);
                objc_classes.emplace(std::move(class_name), class_addr);
            }
        }

        return Image(image_cache_entry.first, image_cache_entry.second, image_off, image->base, std::move(symbols),
            std::move(objc_classes));
    }
};
