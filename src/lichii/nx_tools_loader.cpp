// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// MinGW builds cannot link the MSVC-built nxformat library, so they load the pinned nxformat.dll
// from the executable's folder when the NX tools are first used. Lichii starts without it.

#include <string>
#include <type_traits>

#include <nxformat.h>

#include "lichii/nx_tools.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace {

struct Api {
    decltype(&nxformat_abi_version) abi_version;
    decltype(&nxformat_version) version;
    decltype(&nxformat_cancel_new) cancel_new;
    decltype(&nxformat_cancel_set) cancel_set;
    decltype(&nxformat_cancel_free) cancel_free;
    decltype(&nxformat_run) run;
    decltype(&nxformat_source_new) source_new;
    decltype(&nxformat_source_path) source_path;
    decltype(&nxformat_source_free) source_free;
};

Api api{};

std::string LoadApi() {
    std::wstring path(MAX_PATH, L'\0');
    DWORD length;
    while ((length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()))) ==
           path.size()) {
        path.resize(path.size() * 2);
    }
    path.resize(length);
    path = path.substr(0, path.find_last_of(L"\\/") + 1) + L"nxformat.dll";
    if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) {
        return "nxformat.dll is missing.";
    }
    HMODULE module = LoadLibraryExW(path.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (module == nullptr) {
        const DWORD code = GetLastError();
        if (code == ERROR_MOD_NOT_FOUND) {
            return "nxformat.dll needs the Microsoft Visual C++ 2015-2022 Redistributable (x64).";
        }
        return "nxformat.dll could not be loaded (Windows error " + std::to_string(code) + ").";
    }
    Api loaded{};
    bool found = true;
    const auto get = [&](auto& function, const char* name) {
        using Function = std::remove_reference_t<decltype(function)>;
        function = reinterpret_cast<Function>(
            reinterpret_cast<void (*)()>(GetProcAddress(module, name)));
        found = found && function != nullptr;
    };
    get(loaded.abi_version, "nxformat_abi_version");
    get(loaded.version, "nxformat_version");
    get(loaded.cancel_new, "nxformat_cancel_new");
    get(loaded.cancel_set, "nxformat_cancel_set");
    get(loaded.cancel_free, "nxformat_cancel_free");
    get(loaded.run, "nxformat_run");
    get(loaded.source_new, "nxformat_source_new");
    get(loaded.source_path, "nxformat_source_path");
    get(loaded.source_free, "nxformat_source_free");
    if (!found || loaded.abi_version() != NXFORMAT_ABI_VERSION) {
        FreeLibrary(module);
        return "nxformat.dll is not the version this build of Lichii expects (1.0.3).";
    }
    api = loaded;
    return {};
}

} // namespace

namespace NxFormatDynamic {

bool Load(std::string& error) {
    static const std::string load_error = LoadApi();
    error = load_error;
    return load_error.empty();
}

} // namespace NxFormatDynamic

extern "C" {

uint32_t nxformat_abi_version(void) {
    return api.abi_version();
}

const char* nxformat_version(void) {
    return api.version();
}

nxformat_cancel* nxformat_cancel_new(void) {
    return api.cancel_new();
}

void nxformat_cancel_set(nxformat_cancel* cancel) {
    api.cancel_set(cancel);
}

void nxformat_cancel_free(nxformat_cancel* cancel) {
    api.cancel_free(cancel);
}

int nxformat_run(const char* const* args, size_t count, nxformat_line_fn on_line, void* user,
                 nxformat_cancel* cancel, char* error, size_t error_size) {
    return api.run(args, count, on_line, user, cancel, error, error_size);
}

nxformat_source* nxformat_source_new(const char* name, uint64_t size, nxformat_read_fn read,
                                     void* user) {
    return api.source_new(name, size, read, user);
}

const char* nxformat_source_path(const nxformat_source* source) {
    return api.source_path(source);
}

void nxformat_source_free(nxformat_source* source) {
    api.source_free(source);
}

} // extern "C"
