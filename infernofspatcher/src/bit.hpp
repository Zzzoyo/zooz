/*
 * Bit Manipulation.
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
#include <cstdint>
#include <type_traits>

template<typename T>
[[nodiscard]] auto constexpr make_bit_mask(const std::uint8_t start, const std::uint8_t length)
    -> std::enable_if_t<std::is_integral_v<T>, T> {
    return T(((T(1) << length) - 1) << start);
}

template<typename T>
[[nodiscard]] auto constexpr bit_test(const T val, const std::uint8_t i)
    -> std::enable_if_t<std::is_integral_v<T>, bool> {
    return (val & make_bit_mask<T>(i, 1)) != 0;
}

template<typename T>
[[nodiscard]] auto constexpr bit_extract(const T val, const std::uint8_t start, const std::uint8_t length)
    -> std::enable_if_t<std::is_integral_v<T>, T> {
    return T((val & make_bit_mask<T>(start, length)) >> start);
}

template<typename T>
[[nodiscard]] auto constexpr sign_extend(const T val, const std::uint8_t i)
    -> std::enable_if_t<std::is_integral_v<T>, T> {
    return bit_test(val, i) ? (val | make_bit_mask<T>(i, (sizeof(T) * 8) - i)) : val;
}
