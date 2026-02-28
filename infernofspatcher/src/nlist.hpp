/*
 * BSD Name List.
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
#include <cstdint>

struct NList {
    enum Type : std::uint8_t {
        Section = 0x7,
    };

    std::uint32_t strx {0};
    union TypeFlags {
        struct {
            std::uint8_t ext : 1;
            Type type : 3;
            std::uint8_t pext : 1;
            std::uint8_t stab : 3;
        } bits;
        std::uint8_t raw;

        explicit constexpr TypeFlags() : raw(0) {}
        explicit constexpr TypeFlags(const std::uint8_t raw) : raw(raw) {}
    } type_flags;
    std::uint8_t sect {0};
    std::uint16_t desc {0};
    std::uint64_t value {0};

    explicit NList(std::istream &stream)
        : strx(read_u32_le(stream)), type_flags(read_u8(stream)), sect(read_u8(stream)), desc(read_u16_le(stream)),
          value(read_u64_le(stream)) {}
};
