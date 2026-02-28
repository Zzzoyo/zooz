/*
 * Mach-O Executable Format.
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
#include <istream>
#include <map>
#include <optional>

enum MachMagic : std::uint32_t {
    MH_MAGIC_64 = 0xFEEDFACF,
};

enum CPUType : std::uint32_t {
    CPU_TYPE_VAX = 1,
    CPU_TYPE_MC680x0 = 6,
    CPU_TYPE_X86 = 7,
    CPU_TYPE_MIPS = 8,
    CPU_TYPE_MC98000 = 10,
    CPU_TYPE_HPPA = 11,
    CPU_TYPE_ARM = 12,
    CPU_TYPE_MC88000 = 13,
    CPU_TYPE_SPARC = 14,
    CPU_TYPE_I860 = 15,
    CPU_TYPE_ALPHA = 16,
    CPU_TYPE_POWERPC = 18,
    CPU_ARCH_ABI64 = 0x01000000,
    CPU_ARCH_ABI64_32 = 0x02000000,
    CPU_TYPE_X86_64 = CPU_TYPE_X86 | CPU_ARCH_ABI64,
    CPU_TYPE_ARM64 = CPU_TYPE_ARM | CPU_ARCH_ABI64,
    CPU_TYPE_ARM64_32 = CPU_TYPE_ARM | CPU_ARCH_ABI64_32,
    CPU_TYPE_POWERPC64 = CPU_TYPE_POWERPC | CPU_ARCH_ABI64,
};

enum LoadCommand : std::uint32_t {
    LC_SYMTAB = 0x2,
    LC_SEGMENT_64 = 0x19,
    LC_REQ_DYLD = 0x80000000,
    LC_DYLD_EXPORTS_TRIE = 0x33 | LC_REQ_DYLD,
    LC_DYLD_CHAINED_FIXUPS = 0x34 | LC_REQ_DYLD,
};

struct LoadCommandHeader {
    LoadCommand cmd;
    std::uint32_t cmdsize {0};

    explicit LoadCommandHeader(std::istream &stream)
        : cmd(LoadCommand(read_u32_le(stream))), cmdsize(read_u32_le(stream)) {}
};

struct Section {
    std::uint64_t vm_addr {0};
    std::uint64_t vm_size {0};
    std::uint32_t file_off {0};
    std::uint32_t align {0};
    std::uint32_t reloc_off {0};
    std::uint32_t reloc_count {0};
    std::uint32_t flags {0};

    explicit Section(std::istream &stream)
        : vm_addr(read_u64_le(stream)), vm_size(read_u64_le(stream)), file_off(read_u32_le(stream)),
          align(read_u32_le(stream)), reloc_off(read_u32_le(stream)), reloc_count(read_u32_le(stream)),
          flags(read_u32_le(stream)) {
        seek_stream(stream, 12, std::ios::cur);
    }
};

struct Segment {
    std::uint64_t vm_addr {0};
    std::uint64_t vm_size {0};
    std::uint64_t file_off {0};
    std::uint64_t file_size {0};
    std::uint32_t max_prot {0};
    std::uint32_t init_prot {0};
    std::uint32_t flags {0};
    std::map<std::string, Section> sections;

    explicit Segment(std::istream &stream)
        : vm_addr(read_u64_le(stream)), vm_size(read_u64_le(stream)), file_off(read_u64_le(stream)),
          file_size(read_u64_le(stream)), max_prot(read_u32_le(stream)), init_prot(read_u32_le(stream)) {
        const auto sect_count = read_u32_le(stream);
        this->flags = read_u32_le(stream);
        for (std::uint32_t i = 0; i < sect_count; ++i) {
            auto sect_name = read_cstrn(stream, 16);
            this->sections.emplace(std::move(sect_name), seek_stream(stream, 16, std::ios::cur));
        }
    }

    auto find_section(const std::string &name) const -> const Section & {
        const auto sect = this->sections.find(name);
        if (sect == this->sections.end()) { throw std::out_of_range("cannot find `" + name + "` section"); }
        return sect->second;
    }
};

struct SymTabCommand {
    std::uint32_t sym_off {0};
    std::uint32_t sym_count {0};
    std::uint32_t str_off {0};
    std::uint32_t str_size {0};

    explicit SymTabCommand(std::istream &stream)
        : sym_off(read_u32_le(stream)), sym_count(read_u32_le(stream)), str_off(read_u32_le(stream)),
          str_size(read_u32_le(stream)) {}
};

struct LinkEditDataCommand {
    std::uint32_t data_off {0};
    std::uint32_t data_size {0};

    explicit LinkEditDataCommand(std::istream &stream)
        : data_off(read_u32_le(stream)), data_size(read_u32_le(stream)) {}
};

struct MachHeader {
    std::uint32_t magic {0};
    CPUType cpu_type {0};
    std::map<std::string, Segment> segments;
    std::optional<SymTabCommand> symtab;
    // std::optional<LinkEditDataCommand> exports_trie;
    // std::optional<LinkEditDataCommand> chained_fixups;

    explicit MachHeader(std::istream &stream) : magic(read_u32_le(stream)), cpu_type(CPUType(read_u32_le(stream))) {
        if (this->magic != MH_MAGIC_64) { throw std::runtime_error("invalid magic: " + std::to_string(this->magic)); }

        seek_stream(stream, 8, std::ios::cur);
        const auto ncmds = read_u32_le(stream);
        seek_stream(stream, 12, std::ios::cur);

        for (std::uint32_t i = 0; i < ncmds; ++i) {
            const LoadCommandHeader cmd(stream);
            const auto cmd_end_pos = stream.tellg() + std::streamoff(cmd.cmdsize - sizeof(cmd));

            switch (cmd.cmd) {
                case LC_SYMTAB: {
                    this->symtab = SymTabCommand(stream);
                } break;
                case LC_SEGMENT_64: {
                    auto seg_name = read_cstrn(stream, 16);
                    this->segments.emplace(std::move(seg_name), stream);
                } break;
                // case LC_DYLD_EXPORTS_TRIE: {
                //     this->exports_trie = LinkEditDataCommand(stream);
                // } break;
                // case LC_DYLD_CHAINED_FIXUPS: {
                //     this->chained_fixups = LinkEditDataCommand(stream);
                // } break;
                default: {
                } break;
            }
            seek_stream(stream, cmd_end_pos);
        }
    }

    auto find_segment(const std::string &name) const -> const Segment & {
        const auto seg = this->segments.find(name);
        if (seg == this->segments.end()) { throw std::out_of_range("cannot find `" + name + "` segment"); }
        return seg->second;
    }

    auto find_section(const std::string &seg_name, const std::string &sect_name) const -> const Section & {
        return this->find_segment(seg_name).find_section(sect_name);
    }
};
