/*
 * Inferno Filesystem Patcher.
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

#include "analyser.hpp"
#include "assembler.hpp"
#include "patcher.hpp"
#include <filesystem>
#include <iostream>

struct PatcherCLI {
    union Flags {
        struct {
            std::uint8_t help : 1;
            std::uint8_t revert_only : 1;
            std::uint8_t dry_run : 1;
            std::uint8_t unredact_logs : 1;
            std::uint8_t _rsvd : 4;
        } v;
        std::uint8_t raw;

        explicit constexpr Flags() : raw(0) {}
    } flags;
    std::filesystem::path cache_path;

    static void print_usage(const int argc, char **argv) {
        const auto *const executable = argc == 0 || argv[0] == nullptr ? "inferno_fs_patcher" : argv[0];
        std::cerr << "Usage: " << executable << " [OPTIONS..] <DYLD_CACHE_PATH>\n";
        std::cerr << "\nOptions:\n";
        std::cerr << "  -r, --revert     |  Revert bytes to the original state, without reapplying patches.\n";
        std::cerr << "  -n, --dry-run    |  Revert bytes and run patcher, but do not apply the patch modifications.\n";
        std::cerr << "  --unredact-logs  |  Patch libsystem_trace.dylib to unredact logs.\n";
        std::cerr << "  -h, --help       |  Show usage of this program (this text).\n";
    }

    void check_mutually_exclusive() const {
        if (this->flags.v.revert_only && this->flags.v.dry_run) {
            throw std::runtime_error("--revert and --dry-run are mutually exclusive");
        }
    }

    explicit PatcherCLI() = default;

    explicit PatcherCLI(const int argc, char **argv) {
        if (argc < 2) { throw std::runtime_error("inadequate parameters (expected at least 1)"); }
        if (argc > 5) { throw std::runtime_error("superfluous parameters (expected at most 3)"); }

        for (int i = 1; i < argc; ++i) {
            const std::string_view param = argv[i];

            if (param == "-h" || param == "--help") {
                this->flags.v.help = true;
                return;
            }

            if (!this->flags.v.revert_only && (param == "-r" || param == "--revert")) {
                this->flags.v.revert_only = true;
                this->check_mutually_exclusive();
            } else if (!this->flags.v.dry_run && (param == "-n" || param == "--dry-run")) {
                this->flags.v.dry_run = true;
                this->check_mutually_exclusive();
            } else if (!this->flags.v.unredact_logs && param == "--unredact-logs") {
                this->flags.v.unredact_logs = true;
            } else if (this->cache_path.empty()) {
                this->cache_path = param;
            } else {
                throw std::runtime_error("unexpected combination of parameters");
            }
        }

        if (this->cache_path.empty()) { throw std::runtime_error("missing DYLD_CACHE_PATH parameter"); }
    }

    constexpr bool help() const noexcept { return this->flags.v.help; }
    constexpr bool revert_only() const noexcept { return this->flags.v.revert_only; }
    constexpr bool dry_run() const noexcept { return this->flags.v.dry_run; }
    constexpr bool unredact_logs() const noexcept { return this->flags.v.unredact_logs; }
};

namespace {
    class CoreImagePatches {
        CacheAnalyser::Image image;
        Assembler &assembler;

        public:
        CoreImagePatches(const CacheAnalyser &analyser, Assembler &assembler)
            : image(analyser.find_image(FrameworkMatch("CoreImage"))), assembler(assembler) {}

        void apply() {
            // Force return false to allow software rendering.
            auto gl_is_usable = this->image.resolve_sym("_CIGLIsUsable");
            this->assembler.write_movz_incr(this->image.path, this->image.header, gl_is_usable, GPReg::R0, false, 0);
            this->assembler.write_ret(this->image.path, this->image.header, gl_is_usable);

            // -- Supplemental SW rendering patches for iOS 16+ --

            // Allow widgets to use software rendering.
            try {
                this->assembler.write_ret(this->image.path, this->image.header,
                    this->image.resolve_sym("___isWidget_block_invoke"));
            } catch (std::out_of_range &e) { std::cerr << "Warning: " << e.what() << " (normal for iOS <=16).\n"; }

            // Allow core UI to use software rendering.
            try {
                std::ifstream cache_file(this->image.path);
                auto addr = Assembler::find_cbz(cache_file, this->image.header,
                    this->image.resolve_sym("____ZL13isSWAllowListv_block_invoke"), true, false, 8);
                this->assembler.write_nop_incr(this->image.path, this->image.header, addr);
                this->assembler.write_nop(this->image.path, this->image.header,
                    Assembler::find_cbz(cache_file, this->image.header, addr, false, false, 8));
            } catch (std::out_of_range &e) { std::cerr << "Warning: " << e.what() << " (normal for iOS <=16).\n"; }
        }
    };

    class QuartzCorePatches {
        CacheAnalyser::Image image;
        Assembler &assembler;

        void fix_async_dispatcher(std::istream &stream, const std::uint64_t renderer, const char *sym) {
            auto renderer_call =
                Assembler::find_bl_incr(stream, this->image.header, this->image.resolve_sym(sym), renderer);

            try {
                Assembler::find_cbz(stream, this->image.header, renderer_call, true, false, 1);
                std::cout << "Detected fixed `CA::OGL::AsynchronousDispatcher` logic, skipping `" << sym << "`.\n";
            } catch (std::out_of_range &e) {
                this->assembler.write_nop_incr(this->image.path, image.header, renderer_call);
                this->assembler.write_nop_incr(this->image.path, image.header, renderer_call);
                this->assembler.write_nop_incr(this->image.path, image.header, renderer_call);
                this->assembler.write_nop(this->image.path, image.header,
                    Assembler::find_blra(stream, this->image.header, renderer_call, true, false, false, 4));
            }
        }

        public:
        QuartzCorePatches(const CacheAnalyser &analyser, Assembler &assembler)
            : image(analyser.find_image(FrameworkMatch("QuartzCore"))), assembler(assembler) {}

        void apply() {
            // iOS <=14, bug in two functions: a missing null check on return value of `::renderer` causing a crash.
            const auto renderer = this->image.resolve_sym("__ZN2CA3OGL22AsynchronousDispatcher8rendererEv");
            std::ifstream cache_file(this->image.path);
            fix_async_dispatcher(cache_file, renderer, "__ZN2CA3OGL22AsynchronousDispatcher10stop_timerEv");
            fix_async_dispatcher(cache_file, renderer, "__ZN2CA3OGLL17release_iosurfaceEP11__IOSurface");

            // Neutralise CIF10 support which also neutralises framebuffer AGX/SGX compression.
            this->assembler.write_ret(this->image.path, this->image.header,
                this->image.resolve_sym("___CADeviceSupportsCIF10_block_invoke"));
        }
    };

    class SpringBoardFoundationPatches {
        CacheAnalyser::Image image;
        Assembler &assembler;

        public:
        SpringBoardFoundationPatches(const CacheAnalyser &analyser, Assembler &assembler)
            : image(analyser.find_image(PrivateFrameworkMatch("SpringBoardFoundation"))), assembler(assembler) {}

        void apply() {
            // Force return true, fixes wallpaper settings crash due to missing GPU.
            auto should_use_xpc_service_for_rendering =
                this->image.resolve_sym("+[SBFCARenderer shouldUseXPCServiceForRendering]");
            this->assembler.write_movz_incr(this->image.path, this->image.header, should_use_xpc_service_for_rendering,
                GPReg::R0, false, 1);
            this->assembler.write_ret(this->image.path, this->image.header, should_use_xpc_service_for_rendering);
        }
    };

    class CMCapturePatches {
        CacheAnalyser::Image image;
        Assembler &assembler;

        public:
        CMCapturePatches(const CacheAnalyser &analyser, Assembler &assembler)
            : image(analyser.find_image(PrivateFrameworkMatch("CMCapture"))), assembler(assembler) {}

        void apply() {
            // Neutralise shader precompilation, which requires GPU.
            try {
                this->assembler.write_ret(this->image.path, this->image.header,
                    this->image.resolve_sym("_FigPreloadShaders", "_FigCapturePreloadShaders"));
                this->assembler.write_ret(this->image.path, this->image.header,
                    this->image.resolve_sym("_FigWaitForPreloadShadersCompletion",
                        "_FigCaptureWaitForPreloadShadersCompletion"));
            } catch (std::exception &e) { std::cerr << "Warning: " << e.what() << " (normal for iOS <=14).\n"; }
        }
    };

    class LibTelephonyUtilDynamicPatches {
        CacheAnalyser::Image image;
        Assembler &assembler;
        Patcher &patcher;
        const CacheAnalyser &analyser;

        public:
        LibTelephonyUtilDynamicPatches(const CacheAnalyser &analyser, Assembler &assembler, Patcher &patcher)
            : image(analyser.find_image(ImageMatch("/usr/lib/libTelephonyUtilDynamic.dylib"))), assembler(assembler),
              patcher(patcher), analyser(analyser) {}

        void apply_zeroes(const char *sym) {
            const std::array<std::uint8_t, 4> u32_zeroes = {0, 0, 0, 0};
            const auto vm_addr = this->image.resolve_sym(sym);
            const auto &[off, entry] = this->analyser.find_entry_from_vm_addr(vm_addr);
            this->patcher.write(entry.first, off, u32_zeroes);
        }

        void apply() {
            // Neutralise hardcoded expectations for Baseband.
            this->assembler.write_ret(this->image.path, this->image.header,
                this->image.resolve_sym("__TelephonyRadiosDetermineRadio"));
            this->apply_zeroes("_sTelephonyProduct");
            this->apply_zeroes("_sTelephonyRadio");
            this->apply_zeroes("_sTelephonyRadioVendor");
        }
    };

    class NeutrinoCorePatches {
        CacheAnalyser::Image objc_image;
        CacheAnalyser::Image image;
        Assembler &assembler;

        public:
        NeutrinoCorePatches(const CacheAnalyser &analyser, Assembler &assembler)
            : objc_image(analyser.find_image(ImageMatch("/usr/lib/libobjc.A.dylib"))),
              image(analyser.find_image(PrivateFrameworkMatch("NeutrinoCore"), true)), assembler(assembler) {}

        void apply() {
            const auto objc_alloc_init = this->objc_image.resolve_sym("_objc_alloc_init");
            const auto nu_sw_renderer = this->image.resolve_objc_class("NUSoftwareRenderer");
            auto address = this->image.resolve_sym("-[NUDevice_iOS _newRendererWithCIContextOptions:error:]",
                "-[NUDevice_iOS _newRendererWithOptions:error:]");
            this->assembler.write_adrp_add_incr(this->image.path, this->image.header, address, nu_sw_renderer,
                GPReg::R0);
            this->assembler.write_adrp_add_incr(this->image.path, this->image.header, address, objc_alloc_init,
                GPReg::R1);
            this->assembler.write_blr(this->image.path, this->image.header, address, GPReg::R1);
        }
    };

    class LibSystemTracePatches {
        CacheAnalyser::Image image;
        Assembler &assembler;

        public:
        LibSystemTracePatches(const CacheAnalyser &analyser, Assembler &assembler)
            : image(analyser.find_image(ImageMatch("/usr/lib/system/libsystem_trace.dylib"))), assembler(assembler) {}

        void apply() {
            auto address = this->image.resolve_sym("__os_trace_is_development_build");
            this->assembler.write_movz_incr(this->image.path, this->image.header, address, GPReg::R0, false, 1);
            this->assembler.write_ret(this->image.path, this->image.header, address);
        }
    };

    void run(const PatcherCLI &cli) {
        const CacheAnalyser analyser(cli.cache_path);

        std::cout << "Reverting bytes...\n";
        for (const auto &cache : analyser.caches) { Patcher::revert(cache.first); }
        std::cout << "Bytes reverted successfully.\n";

        if (cli.revert_only()) { return; }

        Patcher patcher;
        Assembler assembler(patcher);

        std::cout << "Building patches...\n";
        CoreImagePatches(analyser, assembler).apply();
        QuartzCorePatches(analyser, assembler).apply();
        SpringBoardFoundationPatches(analyser, assembler).apply();
        CMCapturePatches(analyser, assembler).apply();
        LibTelephonyUtilDynamicPatches(analyser, assembler, patcher).apply();
        NeutrinoCorePatches(analyser, assembler).apply();
        if (cli.unredact_logs()) { LibSystemTracePatches(analyser, assembler).apply(); }
        std::cout << "Patches built successfully.\n";

        patcher.print_changes();

        if (!cli.dry_run()) {
            std::cout << "Applying changes...\n";
            patcher.flush();
            std::cout << "Changes applied successfully.\n";
        }
    }
}    // namespace

auto main(const int argc, char *argv[]) -> int {
    std::cout << std::hex << std::showbase;
    std::cerr << std::hex << std::showbase;

    PatcherCLI cli;
    try {
        cli = PatcherCLI(argc, argv);
    } catch (const std::exception &e) {
        std::cerr << "Error: " << e.what() << ".\n\n";
        PatcherCLI::print_usage(argc, argv);

        return EXIT_FAILURE;
    }

    if (cli.help()) {
        PatcherCLI::print_usage(argc, argv);

        return EXIT_SUCCESS;
    }

    try {
        run(cli);
    } catch (std::exception &e) {
        std::cerr << "Error: " << e.what() << ".\n";

        return EXIT_FAILURE;
    }
}
