/*
 * Parser.
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
#include "endian.hpp"
#include <array>
#include <cstdint>
#include <istream>
#include <string>

auto seek_stream(std::istream &stream, const std::streamoff npos, const std::ios::seekdir dir = std::ios::beg)
    -> std::istream & {
    if (!stream) {
        throw std::runtime_error(
            "seek failed: " + std::to_string(npos) + " direction " + std::to_string(dir) + "; bad stream");
    }
    auto &ret = stream.seekg(npos, dir);
    if (!stream) {
        throw std::out_of_range(
            "seek failed: " + std::to_string(npos) + " direction " + std::to_string(dir) + "; out of range");
    }
    return ret;
}

auto read_stream(std::istream &stream, void *const out_buffer, const std::streamsize size) -> std::istream & {
    if (!stream) {
        throw std::runtime_error("failed to read " + std::to_string(size) + " bytes from stream; bad stream");
    }
    auto &ret = stream.read(static_cast<char *>(out_buffer), size);
    if (!ret) { throw std::runtime_error("failed to read " + std::to_string(size) + " bytes from stream; bad stream"); }
    if (ret.gcount() != size) {
        throw std::out_of_range("failed to read " + std::to_string(size) + " bytes from stream; out of bounds");
    }
    return ret;
}

auto read_cstr(std::istream &stream) -> std::string {
    std::string str;
    char byte;
    while (stream) {
        read_stream(stream, &byte, sizeof(byte));
        if (byte == 0) { break; }
        str.push_back(byte);
    }

    return str;
}

auto read_cstrn(std::istream &stream, const std::uint32_t n) -> std::string {
    std::string str;
    str.reserve(n);
    char byte;
    for (std::uint32_t i = 0; stream && i < n; ++i) {
        read_stream(stream, &byte, sizeof(byte));
        if (byte == 0) {
            seek_stream(stream, -1, std::ios::cur);
            break;
        }
        str.push_back(byte);
    }

    seek_stream(stream, std::streamoff(n - str.size()), std::ios::cur);

    return str;
}

auto read_u8(std::istream &stream) -> std::uint8_t {
    std::uint8_t byte;
    read_stream(stream, &byte, sizeof(byte));
    return byte;
}

auto read_u16_le(std::istream &stream) -> std::uint16_t {
    if (endian::IS_LITTLE) {
        std::uint16_t ret;
        read_stream(stream, &ret, sizeof(ret));
        return ret;
    } else {
        std::array<std::uint8_t, sizeof(std::uint16_t)> buffer;
        read_stream(stream, buffer.data(), buffer.size());
        return std::uint16_t(buffer[0]) | std::uint16_t(std::uint16_t(buffer[1]) << 8);
    }
}

auto read_u32_le(std::istream &stream) -> std::uint32_t {
    if (endian::IS_LITTLE) {
        std::uint32_t ret;
        read_stream(stream, &ret, sizeof(ret));
        return ret;
    } else {
        std::array<std::uint8_t, sizeof(std::uint32_t)> buffer;
        read_stream(stream, buffer.data(), buffer.size());
        return std::uint32_t(buffer[0]) | (std::uint32_t(buffer[1]) << 8) | (std::uint32_t(buffer[2]) << 16) |
               (std::uint32_t(buffer[3]) << 24);
    }
}

auto read_u64_le(std::istream &stream) -> std::uint64_t {
    if (endian::IS_LITTLE) {
        std::uint64_t ret;
        read_stream(stream, &ret, sizeof(ret));
        return ret;
    } else {
        std::array<std::uint8_t, sizeof(std::uint64_t)> buffer;
        read_stream(stream, buffer.data(), buffer.size());
        return std::uint64_t(buffer[0]) | (std::uint64_t(buffer[1]) << 8) | (std::uint64_t(buffer[2]) << 16) |
               (std::uint64_t(buffer[3]) << 24) | (std::uint64_t(buffer[4]) << 32) | (std::uint64_t(buffer[5]) << 40) |
               (std::uint64_t(buffer[6]) << 48) | (std::uint64_t(buffer[7]) << 56);
    }
}
