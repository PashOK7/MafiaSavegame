#include "profile_sav.hpp"

#include <algorithm>
#include <sstream>

namespace profile_sav {

namespace {

struct CipherState {
    std::uint32_t key1 = 0x23101976u;
    std::uint32_t key2 = 0x10072002u;
};

std::uint32_t ReadU32LERaw(const std::uint8_t* data) {
    return static_cast<std::uint32_t>(data[0]) | (static_cast<std::uint32_t>(data[1]) << 8) |
           (static_cast<std::uint32_t>(data[2]) << 16) | (static_cast<std::uint32_t>(data[3]) << 24);
}

void WriteU32LERaw(std::uint8_t* data, std::uint32_t value) {
    data[0] = static_cast<std::uint8_t>(value & 0xFFu);
    data[1] = static_cast<std::uint8_t>((value >> 8) & 0xFFu);
    data[2] = static_cast<std::uint8_t>((value >> 16) & 0xFFu);
    data[3] = static_cast<std::uint8_t>((value >> 24) & 0xFFu);
}

void DecryptInPlace(std::vector<std::uint8_t>* bytes, CipherState* state) {
    if (bytes == nullptr || state == nullptr) {
        return;
    }
    const std::size_t fullWords = bytes->size() / 4;
    for (std::size_t i = 0; i < fullWords; ++i) {
        const std::size_t off = i * 4;
        const std::uint32_t cipher = ReadU32LERaw(bytes->data() + off);
        const std::uint32_t plain = state->key1 ^ cipher;
        WriteU32LERaw(bytes->data() + off, plain);
        state->key2 += plain;
        state->key1 += state->key2;
    }
}

void EncryptInPlace(std::vector<std::uint8_t>* bytes, CipherState* state) {
    if (bytes == nullptr || state == nullptr) {
        return;
    }
    const std::size_t fullWords = bytes->size() / 4;
    for (std::size_t i = 0; i < fullWords; ++i) {
        const std::size_t off = i * 4;
        const std::uint32_t plain = ReadU32LERaw(bytes->data() + off);
        state->key2 += plain;
        const std::uint32_t cipher = plain ^ state->key1;
        WriteU32LERaw(bytes->data() + off, cipher);
        state->key1 += state->key2;
    }
}

bool ReadEncryptedBlock(const std::vector<std::uint8_t>& raw,
                        std::size_t* cursor,
                        std::size_t size,
                        CipherState* state,
                        std::vector<std::uint8_t>* out,
                        std::string* error) {
    if (cursor == nullptr || state == nullptr || out == nullptr) {
        if (error != nullptr) {
            *error = "internal null pointer while reading profile block";
        }
        return false;
    }
    if (*cursor + size > raw.size()) {
        if (error != nullptr) {
            *error = "profile block exceeds file size";
        }
        return false;
    }
    out->assign(raw.begin() + static_cast<std::ptrdiff_t>(*cursor),
                raw.begin() + static_cast<std::ptrdiff_t>(*cursor + size));
    DecryptInPlace(out, state);
    *cursor += size;
    return true;
}

}  // namespace

std::uint32_t ReadU32LE(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
    return static_cast<std::uint32_t>(bytes[offset]) | (static_cast<std::uint32_t>(bytes[offset + 1]) << 8) |
           (static_cast<std::uint32_t>(bytes[offset + 2]) << 16) |
           (static_cast<std::uint32_t>(bytes[offset + 3]) << 24);
}

void WriteU32LE(std::vector<std::uint8_t>* bytes, std::size_t offset, std::uint32_t value) {
    (*bytes)[offset] = static_cast<std::uint8_t>(value & 0xFFu);
    (*bytes)[offset + 1] = static_cast<std::uint8_t>((value >> 8) & 0xFFu);
    (*bytes)[offset + 2] = static_cast<std::uint8_t>((value >> 16) & 0xFFu);
    (*bytes)[offset + 3] = static_cast<std::uint8_t>((value >> 24) & 0xFFu);
}

bool ParseProfileSave(const std::vector<std::uint8_t>& raw, ProfileSaveData* out, std::string* error) {
    if (out == nullptr) {
        if (error != nullptr) {
            *error = "null output profile save struct";
        }
        return false;
    }

    const std::size_t minSize = kFileHeaderSize + kCoreSize + kBlock720Size + kBlock92Size + kBlock156Size;
    if (raw.size() != minSize) {
        if (error != nullptr) {
            std::ostringstream oss;
            oss << "unexpected .sav size " << raw.size() << ", expected " << minSize;
            *error = oss.str();
        }
        return false;
    }

    ProfileSaveData parsed;
    parsed.rawSize = raw.size();
    std::copy(raw.begin(), raw.begin() + static_cast<std::ptrdiff_t>(kFileHeaderSize), parsed.fileHeader.begin());

    std::size_t cursor = kFileHeaderSize;
    CipherState state;
    if (!ReadEncryptedBlock(raw, &cursor, kCoreSize, &state, &parsed.core84, error)) {
        return false;
    }
    if (!ReadEncryptedBlock(raw, &cursor, kBlock720Size, &state, &parsed.block720, error)) {
        return false;
    }
    if (!ReadEncryptedBlock(raw, &cursor, kBlock92Size, &state, &parsed.block92, error)) {
        return false;
    }
    if (!ReadEncryptedBlock(raw, &cursor, kBlock156Size, &state, &parsed.block156, error)) {
        return false;
    }

    if (ReadU32LERaw(parsed.fileHeader.data()) != kMagicForP || ReadU32LERaw(parsed.fileHeader.data() + 8) != kVersion1) {
        if (error != nullptr) {
            *error = "invalid profile file header (expected forP/version1)";
        }
        return false;
    }
    if (parsed.core84.size() < 8 || ReadU32LE(parsed.core84, 0) != kMagicForP || ReadU32LE(parsed.core84, 4) != kVersion1) {
        if (error != nullptr) {
            *error = "invalid decrypted core84 block (forP/version1 mismatch)";
        }
        return false;
    }

    *out = std::move(parsed);
    return true;
}

bool BuildRaw(const ProfileSaveData& save, std::vector<std::uint8_t>* out, std::string* error) {
    if (out == nullptr) {
        if (error != nullptr) {
            *error = "null output byte vector";
        }
        return false;
    }
    if (save.core84.size() != kCoreSize || save.block720.size() != kBlock720Size || save.block92.size() != kBlock92Size ||
        save.block156.size() != kBlock156Size) {
        if (error != nullptr) {
            *error = "profile block sizes are invalid";
        }
        return false;
    }

    std::vector<std::uint8_t> raw;
    raw.insert(raw.end(), save.fileHeader.begin(), save.fileHeader.end());

    CipherState state;
    auto pushBlock = [&](const std::vector<std::uint8_t>& plain) {
        std::vector<std::uint8_t> cipher = plain;
        EncryptInPlace(&cipher, &state);
        raw.insert(raw.end(), cipher.begin(), cipher.end());
    };

    pushBlock(save.core84);
    pushBlock(save.block720);
    pushBlock(save.block92);
    pushBlock(save.block156);

    *out = std::move(raw);
    return true;
}

}  // namespace profile_sav

