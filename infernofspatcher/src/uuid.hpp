/*
 * DYLD UUID.
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
#include <array>
#include <cstdint>

class DyldUUID {
    using Data = std::array<uint8_t, 16>;

    Data data;

    public:
    explicit DyldUUID() noexcept : data({}) {}
    explicit DyldUUID(std::istream &stream) {
        read_stream(stream, this->data.data(), std::streamsize(this->data.size()));
    }

    bool operator==(const DyldUUID &other) const noexcept { return this->data == other.data; }
    bool operator!=(const DyldUUID &other) const noexcept { return !(*this == other); }

    bool null() const noexcept { return *this == DyldUUID(); }
    operator bool() const noexcept { return !this->null(); }
};
