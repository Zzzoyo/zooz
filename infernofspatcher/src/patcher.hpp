/*
 * Patcher.
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
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

class Patcher {
    std::unordered_map<std::filesystem::path, std::map<std::streamoff, std::vector<std::uint8_t>>> write_queue;

    static constexpr std::string_view ORIG_BYTE_FILE_EXT = ".InfernoOriginalBytes";

    public:
    explicit Patcher() = default;

    static void revert(const std::filesystem::path &path) {
        auto orig_bytes_path = path;
        orig_bytes_path += ORIG_BYTE_FILE_EXT;

        if (!std::filesystem::exists(orig_bytes_path)) { return; }

        std::ifstream orig_bytes_file(orig_bytes_path);
        if (!orig_bytes_file || !orig_bytes_file.is_open()) {
            throw std::runtime_error("failed to open orig bytes file at `" + orig_bytes_path.string() + "`");
        }

        std::fstream file(path, std::ios::binary | std::ios::in | std::ios::out);
        if (!file || !file.is_open()) { throw std::runtime_error("failed to open file at `" + path.string() + "`"); }

        std::string line;
        while (orig_bytes_file >> line) {
            if (line.back() == ':') {
                file.seekp(std::stoll(line.substr(0, line.size() - 1), nullptr, 16));
                if (!file) { throw std::runtime_error("failed to seek to offset (malformed revert file?)"); }
                continue;
            }
            const auto value = std::stoi(line, nullptr, 16);
            const auto byte = std::uint8_t(value & 0xFF);
            if (byte != value) { throw std::runtime_error("malformed revert file; byte too large"); }
            file.write(reinterpret_cast<const char *>(&byte), sizeof(byte));
            if (!file) { throw std::runtime_error("failed to restore a byte (malformed revert file?)"); }
        }

        file.flush();
        orig_bytes_file.close();
        std::filesystem::remove(orig_bytes_path);
    }

    void write(const std::filesystem::path &path, const std::streamoff file_off, const std::uint8_t *bytes,
        const std::size_t nbytes) {
        this->write_queue.try_emplace(path);
        this->write_queue[path].emplace(file_off, std::vector(bytes, bytes + nbytes));
    }

    template<const std::size_t N>
    void write(const std::filesystem::path &path, const std::streamoff file_off,
        const std::array<std::uint8_t, N> bytes) {
        this->write(path, file_off, bytes.data(), N);
    }

    void print_changes() const {
        for (const auto &cache_entries : this->write_queue) {
            std::cout << "  " << cache_entries.first << ":\n";
            for (const auto &entry : cache_entries.second) {
                std::cout << "    " << entry.first << ": ";
                for (const auto byte : entry.second) { std::cout << std::uint16_t(byte) << " "; }
                std::cout << "\n";
            }
        }
    }

    void flush() const {
        for (const auto &cache_entries : this->write_queue) {
            std::fstream cache_file(cache_entries.first, std::ios::binary | std::ios::in | std::ios::out);
            if (!cache_file.is_open()) {
                throw std::runtime_error("failed to open cache file suffix=" + cache_entries.first.string());
            }
            auto orig_bytes_path = cache_entries.first;
            orig_bytes_path += ORIG_BYTE_FILE_EXT;
            std::ofstream diff_file(orig_bytes_path, std::ios::out | std::ios::trunc);
            if (!diff_file.is_open()) {
                throw std::runtime_error("failed to open orig bytes file suffix=" + cache_entries.first.string());
            }
            diff_file << std::hex;
            for (const auto &entry : cache_entries.second) {
                seek_stream(cache_file, entry.first);
                std::vector<std::uint8_t> orig_bytes(entry.second.size());
                read_stream(cache_file, orig_bytes.data(), std::streamsize(orig_bytes.size()));
                cache_file.seekp(entry.first);
                if (!cache_file) {
                    throw std::runtime_error("failed to seek cache file to " + std::to_string(entry.first));
                }
                cache_file.write(reinterpret_cast<const char *>(entry.second.data()),
                    std::streamsize(entry.second.size()));
                if (!cache_file) {
                    throw std::runtime_error("failed to write " + std::to_string(entry.second.size()) +
                                             " cache file bytes to " + std::to_string(entry.first));
                }
                diff_file << entry.first << ": ";
                for (const auto byte : orig_bytes) { diff_file << std::uint16_t(byte) << " "; }
                diff_file << "\n";
            }
            cache_file.flush();
            diff_file.flush();
        }
    }
};
