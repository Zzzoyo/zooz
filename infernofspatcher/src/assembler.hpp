/*
 * ARM64 Instruction Assembler.
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
#include "patcher.hpp"

enum struct GPReg : std::uint8_t {
    R0,
    R1,
    R2,
    R3,
    R4,
    R5,
    R6,
    R7,
    R8,
    R9,
    R10,
    R11,
    R12,
    R13,
    R14,
    R15,
};

enum struct MOVZShift : std::uint8_t {
    _0,
    _16,
    _32,
    _48,
};

enum struct ADDShift : std::uint8_t {
    _0,
    _12,
};

class Assembler {
    Patcher &patcher;

    using INST = std::uint32_t;

    static constexpr std::uint8_t INST_NUM_BYTES = sizeof(INST);

    using INST_BYTES = std::array<uint8_t, INST_NUM_BYTES>;

    static auto u32_to_bytes(const std::uint32_t value) -> INST_BYTES {
        return INST_BYTES {std::uint8_t(value & 0xFF), std::uint8_t((value >> 8) & 0xFF),
            std::uint8_t((value >> 16) & 0xFF), std::uint8_t((value >> 24) & 0xFF)};
    }

    void write_inst(const std::filesystem::path &path, const DyldCacheHeader &header, const std::uint64_t target,
        const INST inst) {
        this->patcher.write(path, header.vm_addr_to_file_off(target), u32_to_bytes(inst));
    }

    void write_inst_incr(const std::filesystem::path &path, const DyldCacheHeader &header, std::uint64_t &target,
        const INST inst) {
        this->write_inst(path, header, target, inst);
        target += sizeof(INST_BYTES);
    }

    static constexpr INST NOP_INST = 0xD503201FU;
    static constexpr INST RET_INST = 0xD65F03C0U;
    static constexpr INST MOVZ_INST = 0x52800000U;
    static constexpr INST BL_INST = 0x94000000U;
    static constexpr INST BL_INST_MASK = 0xFC000000U;
    static constexpr INST CBZ_INST = 0x34000000U;
    static constexpr INST CBZ_INST_MASK = 0x7F000000U;
    static constexpr INST BLRA_INST = 0xD63F0800U;
    static constexpr INST BLRA_INST_MASK = 0xFEFFF800U;
    static constexpr INST ADRP_INST = 0x90000000;
    static constexpr INST ADD_INST = 0x11000000;
    static constexpr INST BLR_INST = 0xD63F0000;

    static auto make_movz(const GPReg reg, const bool wide, const std::uint16_t imm,
        const MOVZShift shift = MOVZShift::_0) -> INST {
        if (wide && shift != MOVZShift::_0) { throw std::invalid_argument("cannot have a shift for wide movz"); }
        return (INST(wide) << 31) | MOVZ_INST | (INST(shift) << 21) | (INST(imm) << 5) | INST(reg);
    }

    static auto make_nop() -> INST { return NOP_INST; }
    static auto make_ret() -> INST { return RET_INST; }

    static auto disas_bl(const std::uint64_t inst_addr, const std::uint32_t inst) -> std::uint64_t {
        union {
            std::uint32_t u32;
            std::int32_t s32;
        } imm {sign_extend(bit_extract(inst, 0, 26), 25)};
        imm.s32 *= INST_NUM_BYTES;

        if (imm.s32 < 0) {
            return inst_addr - std::uint32_t(-imm.s32);
        } else {
            return inst_addr + std::uint32_t(imm.s32);
        }
    }

    static constexpr auto ADRP_IMM_MAX = make_bit_mask<std::int32_t>(0, 20);
    static constexpr auto ADRP_MAX = make_bit_mask<std::int64_t>(0, 20) << 0xC;

    static auto make_adrp(const std::int32_t off, const GPReg reg) -> INST {
        if (off > ADRP_IMM_MAX || off < -ADRP_IMM_MAX) { throw std::invalid_argument("invalid imm for adrp"); }

        union {
            std::int32_t s32;
            std::uint32_t u32;
        } imm {off};
        return ADRP_INST | (bit_extract(imm.u32, 0, 2) << 29) | (bit_extract(imm.u32, 2, 19) << 5) | INST(reg);
    }

    static auto make_add(const std::uint16_t imm, const bool wide, const GPReg source_reg, const GPReg dest_reg,
        const ADDShift shift = ADDShift::_0) -> INST {
        if (bit_extract(imm, 12, 4) != 0) { throw std::invalid_argument("invalid imm for add"); }

        return ADD_INST | (INST(wide) << 31) | (INST(shift) << 22) | (INST(imm) << 10) | (INST(source_reg) << 5) |
               INST(dest_reg);
    }

    static auto make_blr(const GPReg reg) -> INST { return BLR_INST | (INST(reg) << 5); }

    public:
    explicit Assembler(Patcher &patcher) : patcher(patcher) {}

    void write_movz(const std::filesystem::path &path, const DyldCacheHeader &header, const std::uint64_t target,
        const GPReg reg, const bool wide, const std::uint16_t imm, const MOVZShift shift = MOVZShift::_0) {
        this->write_inst(path, header, target, make_movz(reg, wide, imm, shift));
    }

    void write_movz_incr(const std::filesystem::path &path, const DyldCacheHeader &header, std::uint64_t &target,
        const GPReg reg, const bool wide, const std::uint16_t imm, const MOVZShift shift = MOVZShift::_0) {
        this->write_inst_incr(path, header, target, make_movz(reg, wide, imm, shift));
    }

    void write_nop(const std::filesystem::path &path, const DyldCacheHeader &header, const std::uint64_t target) {
        this->write_inst(path, header, target, make_nop());
    }

    void write_nop_incr(const std::filesystem::path &path, const DyldCacheHeader &header, std::uint64_t &target) {
        this->write_inst_incr(path, header, target, make_nop());
    }

    void write_ret(const std::filesystem::path &path, const DyldCacheHeader &header, const std::uint64_t target) {
        this->write_inst(path, header, target, make_ret());
    }

    void write_ret_incr(const std::filesystem::path &path, const DyldCacheHeader &header, std::uint64_t &target) {
        this->write_inst_incr(path, header, target, make_ret());
    }

    static auto find_cbz(std::istream &stream, const DyldCacheHeader &header, const std::uint64_t start_addr,
        const bool wide, const bool rev = false, const std::uint32_t inst_limit = 0x400) -> std::uint64_t {
        const auto base = header.vm_addr_to_file_off(start_addr);
        seek_stream(stream, base);
        for (std::uint32_t i = 0; i < inst_limit; ++i) {
            const auto inst = read_u32_le(stream);
            if (rev) { seek_stream(stream, -std::streamoff(INST_NUM_BYTES * 2), std::ios::cur); }
            if ((inst & CBZ_INST_MASK) == CBZ_INST && bit_test(inst, 31) == wide) {
                return rev ? (start_addr - (std::uint64_t(i) * INST_NUM_BYTES)) :
                             (start_addr + (std::uint64_t(i) * INST_NUM_BYTES));
            }
        }
        throw std::out_of_range("no cbz instruction found start_addr=" + std::to_string(start_addr) +
                                " wide=" + std::to_string(wide) + " rev=" + std::to_string(rev) +
                                " inst_limit=" + std::to_string(inst_limit));
    }

    static auto find_bl(std::istream &stream, const DyldCacheHeader &header, const std::uint64_t start_addr,
        const std::uint64_t target_addr = -1ULL, const bool rev = false, const std::uint32_t inst_limit = 0x400)
        -> std::uint64_t {
        const auto base = header.vm_addr_to_file_off(start_addr);
        seek_stream(stream, base);
        for (std::uint32_t i = 0; i < inst_limit; ++i) {
            const auto inst_addr = rev ? (start_addr - (std::uint64_t(i) * INST_NUM_BYTES)) :
                                         (start_addr + (std::uint64_t(i) * INST_NUM_BYTES));
            const auto inst = read_u32_le(stream);
            if (rev) { seek_stream(stream, -std::streamoff(INST_NUM_BYTES * 2), std::ios::cur); }
            if ((inst & BL_INST_MASK) == BL_INST) {
                if (target_addr == -1ULL || disas_bl(inst_addr, inst) == target_addr) { return inst_addr; }
            }
        }
        throw std::out_of_range("no bl instruction found start_addr=" + std::to_string(start_addr) +
                                " target_addr=" + std::to_string(target_addr));
    }

    static auto find_bl_incr(std::istream &stream, const DyldCacheHeader &header, const std::uint64_t start_addr,
        const std::uint64_t target_addr = -1ULL, const bool rev = false, const std::uint32_t inst_limit = 0x400)
        -> std::uint64_t {
        return find_bl(stream, header, start_addr, target_addr, rev, inst_limit) + INST_NUM_BYTES;
    }

    static auto find_blra(std::istream &stream, const DyldCacheHeader &header, const std::uint64_t start_addr,
        const bool zero, const bool key_b, const bool rev = false, const std::uint32_t inst_limit = 0x400)
        -> std::uint64_t {
        const auto base = header.vm_addr_to_file_off(start_addr);
        seek_stream(stream, base);
        for (std::uint32_t i = 0; i < inst_limit; ++i) {
            const auto inst_addr = rev ? (start_addr - (std::uint64_t(i) * INST_NUM_BYTES)) :
                                         (start_addr + (std::uint64_t(i) * INST_NUM_BYTES));
            const auto inst = read_u32_le(stream);
            if (rev) { seek_stream(stream, -std::streamoff(INST_NUM_BYTES * 2), std::ios::cur); }
            if ((inst & BLRA_INST_MASK) == BLRA_INST && bit_test(inst, 24) == zero && bit_test(inst, 10) == key_b) {
                return inst_addr;
            }
        }
        throw std::out_of_range("no blra instruction found start_addr=" + std::to_string(start_addr) +
                                " zero=" + std::to_string(zero) + " key_b=" + std::to_string(key_b));
    }

    void write_adrp_add_incr(const std::filesystem::path &path, const DyldCacheHeader &header, std::uint64_t &address,
        const std::uint64_t target, const GPReg reg) {
        const auto pc_page = address & ~0xFFFULL;
        const auto target_page = target & ~0xFFFULL;
        const auto low12 = std::uint16_t(target & 0xFFF);

        if (target_page > pc_page) {
            const auto off_pages = (target_page - pc_page);
            if (off_pages > ADRP_MAX) { throw std::invalid_argument("target too far away"); }
            this->write_inst_incr(path, header, address, make_adrp(std::int32_t(off_pages >> 0xC), reg));
        } else {
            const auto off_pages = pc_page - target_page;
            if (off_pages > ADRP_MAX) { throw std::invalid_argument("target too far away"); }
            this->write_inst_incr(path, header, address, make_adrp(std::int32_t(-(off_pages >> 0xC)), reg));
        }

        this->write_inst_incr(path, header, address, make_add(low12, true, reg, reg));
    }

    void write_blr(const std::filesystem::path &path, const DyldCacheHeader &header, const std::uint64_t address,
        const GPReg reg) {
        this->write_inst(path, header, address, make_blr(reg));
    }
};
