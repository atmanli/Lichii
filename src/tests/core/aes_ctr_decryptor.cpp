// SPDX-FileCopyrightText: Copyright 2026 Lichii contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <array>
#include <random>
#include <thread>
#include <vector>
#include <catch2/catch_test_macros.hpp>
#include "core/crypto/aes_util.h"
#include "core/crypto/key_manager.h"
#include "core/file_sys/fssystem/fssystem_aes_ctr_counter_extended_storage.h"

namespace {
using FileSys::AesCtrCounterExtendedStorage;
using Key = std::array<u8, 16>;

std::vector<u8> Reference(std::vector<u8> data, const Key& key, const Key& iv) {
    Core::Crypto::AESCipher<Core::Crypto::Key128> cipher(key, Core::Crypto::Mode::CTR);
    cipher.SetIV(iv);
    cipher.Transcode(data.data(), data.size(), data.data(), Core::Crypto::Op::Decrypt);
    return data;
}

bool MatchesReference(std::mt19937_64& rng, AesCtrCounterExtendedStorage::IDecryptor& decryptor,
                      const Key& key, size_t size) {
    Key iv;
    for (auto& b : iv) {
        b = static_cast<u8>(rng());
    }
    std::vector<u8> data(size);
    for (auto& b : data) {
        b = static_cast<u8>(rng());
    }
    const auto expected = Reference(data, key, iv);
    decryptor.Decrypt(data.data(), data.size(), key, iv);
    return data == expected;
}
} // namespace

TEST_CASE("Software AES-CTR decryptor matches a fresh cipher across key changes",
          "[aes-ctr-decryptor]") {
    std::unique_ptr<AesCtrCounterExtendedStorage::IDecryptor> decryptor;
    REQUIRE(R_SUCCEEDED(AesCtrCounterExtendedStorage::CreateSoftwareDecryptor(&decryptor)));

    std::mt19937_64 rng(7);
    std::array<Key, 3> keys;
    for (auto& key : keys) {
        for (auto& b : key) {
            b = static_cast<u8>(rng());
        }
    }
    // Same key repeated reuses the cached cipher; alternating keys must rebuild it.
    for (const size_t i : {0, 0, 1, 1, 0, 2, 2, 1, 0}) {
        for (const size_t size : {size_t{16}, size_t{4096}, size_t{0x12340}}) {
            REQUIRE(MatchesReference(rng, *decryptor, keys[i], size));
        }
    }
}

TEST_CASE("Software AES-CTR decryptor is correct from several threads", "[aes-ctr-decryptor]") {
    std::unique_ptr<AesCtrCounterExtendedStorage::IDecryptor> decryptor;
    REQUIRE(R_SUCCEEDED(AesCtrCounterExtendedStorage::CreateSoftwareDecryptor(&decryptor)));

    std::array<bool, 4> ok{};
    std::vector<std::thread> threads;
    for (size_t t = 0; t < ok.size(); ++t) {
        threads.emplace_back([&, t] {
            std::mt19937_64 rng(100 + t);
            Key key;
            for (auto& b : key) {
                b = static_cast<u8>(rng());
            }
            ok[t] = true;
            for (int i = 0; i < 200; ++i) {
                ok[t] = ok[t] && MatchesReference(rng, *decryptor, key, 0x1000);
            }
        });
    }
    for (auto& thread : threads) {
        thread.join();
    }
    for (const bool result : ok) {
        CHECK(result);
    }
}
