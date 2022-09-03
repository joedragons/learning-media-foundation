#include <CppUnitTest.h>

// clang-format off
#include <winrt/Windows.Foundation.h>
#include <d3d11_4.h>
#include <d3d9.h>
#include <evr.h>
#include <mfreadwrite.h>
#include <windowsx.h>

// clang-format on
#include <experimental/generator>
#include <filesystem>
#include <spdlog/common.h>
#include <spdlog/spdlog.h>

#include "mf_transform.hpp"

void report_error(HRESULT hr, const char* fname, const spdlog::source_loc& loc) noexcept {
    winrt::hresult_error ex{hr};
    spdlog::log(loc, spdlog::level::err, //
                "{}: {:#08x} {}", fname, static_cast<uint32_t>(ex.code()), winrt::to_string(ex.message()));
}

std::string to_mf_string(const GUID& guid) noexcept;
