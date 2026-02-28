/*
 * DYLD Cache.
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
#include "parse.hpp"
#include "uuid.hpp"
#include <unordered_map>
#include <vector>

struct DyldCacheHeader {
    struct Mapping {
        std::uint64_t base {0};
        std::uint64_t size {0};
        std::uint64_t file_off {0};

        explicit Mapping(std::istream &stream)
            : base(read_u64_le(stream)), size(read_u64_le(stream)), file_off(read_u64_le(stream)) {
            seek_stream(stream, 8, std::ios::cur);
        }
    };

    struct Image {
        std::uint64_t base {0};
        std::string path;

        explicit Image(std::istream &stream) : base(read_u64_le(stream)) {
            seek_stream(stream, 16, std::ios::cur);
            const auto path_off = read_u32_le(stream);
            seek_stream(stream, 4, std::ios::cur);    // pad
            const auto prev_pos = stream.tellg();
            seek_stream(stream, path_off);
            this->path = read_cstr(stream);
            seek_stream(stream, prev_pos);
        }
    };

    struct LocalSymbols {
        std::uint32_t nlist_start_index {0};
        std::uint32_t nlist_count {0};

        explicit LocalSymbols(std::istream &stream)
            : nlist_start_index(read_u32_le(stream)), nlist_count(read_u32_le(stream)) {}
    };

    struct LocalSymbolsInfo {
        using EntryMap = std::unordered_map<std::uint64_t, LocalSymbols>;

        std::uint32_t nlist_off {0};
        std::uint32_t strings_off {0};
        EntryMap entries;

        explicit LocalSymbolsInfo() = default;
        explicit LocalSymbolsInfo(std::istream &stream, const std::streamoff local_info_off, const bool is_64,
            const uint64_t cache_base) {
            if (local_info_off == 0) { return; }

            seek_stream(stream, local_info_off);
            this->nlist_off = read_u32_le(stream);
            seek_stream(stream, 4, std::ios::cur);
            this->strings_off = read_u32_le(stream);
            seek_stream(stream, 4, std::ios::cur);
            const std::streamoff entries_offset = read_u32_le(stream);
            const auto entries_count = read_u32_le(stream);

            this->entries.reserve(entries_count);
            seek_stream(stream, local_info_off + entries_offset);
            for (std::uint32_t i = 0; i < entries_count; ++i) {
                const std::uint64_t dylib_offset = is_64 ? read_u64_le(stream) : read_u32_le(stream);
                const LocalSymbols entry(stream);

                this->entries.emplace(cache_base + dylib_offset, entry);
            }
        }
    };

    struct Subcache {
        std::uint64_t vm_off {0};
        std::string suffix;

        explicit Subcache(std::istream &stream, const std::uint32_t index, const bool is_v1)
            : vm_off(read_u64_le(seek_stream(stream, 16, std::ios::cur))),
              suffix(is_v1 ? "." + std::to_string(index + 1) : read_cstrn(stream, 32)) {}
    };

    std::vector<Mapping> mappings;
    std::vector<Image> images;
    std::uint64_t cache_base {0};
    std::streamoff local_symbols_off {0};
    LocalSymbolsInfo local_symbols;
    std::vector<Subcache> subcaches;
    DyldUUID symbol_file_uuid;

    enum struct Type : std::uint8_t {
        Main,
        Sub,
        Symbols,
    };

    explicit DyldCacheHeader(std::istream &stream, const Type type, const std::uint64_t main_cache_base = 0) {
        seek_stream(stream, 0x10);    // magic
        const auto mapping_off = read_u32_le(stream);
        const auto mapping_count = read_u32_le(stream);

        if (type != Type::Symbols && mapping_off != 0 && mapping_count != 0) {
            seek_stream(stream, mapping_off);
            this->mappings.reserve(mapping_count);
            for (std::uint32_t i = 0; i < mapping_count; ++i) { this->mappings.emplace_back(stream); }
        }

        this->cache_base = read_u64_le(seek_stream(stream, 0xE0));

        if (type == Type::Sub) { return; }

        bool symbol_file_support;
        if (type == Type::Symbols) {
            symbol_file_support = true;
        } else {
            symbol_file_support = mapping_off >= 0x190;    // offsetof symbolFileUUID
            if (symbol_file_support) { this->symbol_file_uuid = DyldUUID(seek_stream(stream, 0x190)); }
        }

        if (type == Type::Symbols || !this->symbol_file_uuid) {
            seek_stream(stream, 0x48);
            this->local_symbols_off = read_u32_le(stream);
            this->local_symbols = LocalSymbolsInfo(stream, this->local_symbols_off, symbol_file_support,
                main_cache_base == 0 ? this->cache_base : main_cache_base);
        }

        if (type != Type::Main) { return; }

        const auto split = mapping_off >= 0x18C;    // offsetof subCacheArrayCount

        if (split) {
            seek_stream(stream, 0x1C0);    // use new image off
        } else {
            seek_stream(stream, 0x18);
        }

        const auto image_off = read_u32_le(stream);
        const auto image_count = read_u32_le(stream);

        // APPLE BUG: `split && image_count == 0` should mean
        // is subcache, but some dyld subcache headers are technically broken,
        // containing the images info copied from the main header.
        if (split && image_count == 0) { throw std::runtime_error("main cache expected, but got a subcache"); }

        if (image_off != 0 && image_count != 0) {
            seek_stream(stream, image_off);
            this->images.reserve(image_count);
            for (std::uint32_t i = 0; i < image_count; ++i) { this->images.emplace_back(stream); }
        }

        if (split) {
            seek_stream(stream, 0x188);
            const auto subcache_off = read_u32_le(stream);
            const auto subcache_count = read_u32_le(stream);

            if (subcache_off != 0 && subcache_count != 0) {
                const auto subcache_v1 = mapping_off <= 0x1C8;    // offsetof cacheSubType
                seek_stream(stream, subcache_off);
                this->subcaches.reserve(subcache_count);
                for (std::uint32_t i = 0; i < subcache_count; ++i) {
                    this->subcaches.emplace_back(stream, i, subcache_v1);
                }
            }
        }
    }

    [[nodiscard]] auto vm_addr_to_file_off(const std::uint64_t vm_addr) const -> std::streamoff {
        for (const auto &mapping : this->mappings) {
            if (vm_addr >= mapping.base && vm_addr < mapping.base + mapping.size) {
                return std::streamoff(mapping.file_off + (vm_addr - mapping.base));
            }
        }
        throw std::out_of_range("address " + std::to_string(vm_addr) + " not found");
    }
};
