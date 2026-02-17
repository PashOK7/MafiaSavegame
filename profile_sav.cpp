#include "profile_sav.hpp"

#include <algorithm>
#include <cstring>
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

float ReadF32LERaw(const std::uint8_t* data) {
    const std::uint32_t bits = ReadU32LERaw(data);
    float out = 0.0f;
    std::memcpy(&out, &bits, sizeof(out));
    return out;
}

void WriteF32LERaw(std::uint8_t* data, float value) {
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    WriteU32LERaw(data, bits);
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

bool ParseMrProfileSave(const std::vector<std::uint8_t>& raw, MrProfileSaveData* out, std::string* error) {
    if (out == nullptr) {
        if (error != nullptr) {
            *error = "null output mr profile save struct";
        }
        return false;
    }
    if (raw.size() != 136u || (raw.size() % 4u) != 0u) {
        if (error != nullptr) {
            *error = "mr profile save must be exactly 136 bytes";
        }
        return false;
    }

    MrProfileSaveData parsed;
    parsed.rawSize = raw.size();
    parsed.words.resize(raw.size() / 4u);
    for (std::size_t i = 0; i < parsed.words.size(); ++i) {
        parsed.words[i] = ReadU32LERaw(raw.data() + (i * 4u));
    }

    if (parsed.words.size() != 34u) {
        if (error != nullptr) {
            *error = "mr profile save word count mismatch";
        }
        return false;
    }

    *out = std::move(parsed);
    return true;
}

bool BuildRaw(const MrProfileSaveData& save, std::vector<std::uint8_t>* out, std::string* error) {
    if (out == nullptr) {
        if (error != nullptr) {
            *error = "null output byte vector";
        }
        return false;
    }
    if (save.words.size() != 34u) {
        if (error != nullptr) {
            *error = "mr profile save expects exactly 34 u32 values";
        }
        return false;
    }

    out->assign(save.words.size() * 4u, 0u);
    for (std::size_t i = 0; i < save.words.size(); ++i) {
        WriteU32LERaw(out->data() + (i * 4u), save.words[i]);
    }
    return true;
}

bool ParseMrTimesSave(const std::vector<std::uint8_t>& raw, MrTimesSaveData* out, std::string* error) {
    if (out == nullptr) {
        if (error != nullptr) {
            *error = "null output mrtimes save struct";
        }
        return false;
    }
    if (raw.size() < 44u || ((raw.size() - 4u) % 40u) != 0u) {
        if (error != nullptr) {
            *error = "mrtimes save has unexpected size";
        }
        return false;
    }

    MrTimesSaveData parsed;
    parsed.rawSize = raw.size();
    parsed.count = ReadU32LERaw(raw.data());
    const std::size_t recCount = (raw.size() - 4u) / 40u;
    parsed.records.resize(recCount);
    for (std::size_t i = 0; i < recCount; ++i) {
        const std::size_t off = 4u + (i * 40u);
        auto& rec = parsed.records[i];
        std::copy(raw.begin() + static_cast<std::ptrdiff_t>(off),
                  raw.begin() + static_cast<std::ptrdiff_t>(off + 32u),
                  rec.nameRaw.begin());
        rec.valueA = ReadU32LERaw(raw.data() + off + 32u);
        rec.valueB = ReadU32LERaw(raw.data() + off + 36u);
    }

    *out = std::move(parsed);
    return true;
}

bool BuildRaw(const MrTimesSaveData& save, std::vector<std::uint8_t>* out, std::string* error) {
    if (out == nullptr) {
        if (error != nullptr) {
            *error = "null output byte vector";
        }
        return false;
    }
    if (save.records.empty()) {
        if (error != nullptr) {
            *error = "mrtimes save must contain at least one record";
        }
        return false;
    }

    out->assign(4u + save.records.size() * 40u, 0u);
    WriteU32LERaw(out->data(), save.count);
    for (std::size_t i = 0; i < save.records.size(); ++i) {
        const std::size_t off = 4u + (i * 40u);
        const auto& rec = save.records[i];
        std::copy(rec.nameRaw.begin(), rec.nameRaw.end(), out->begin() + static_cast<std::ptrdiff_t>(off));
        WriteU32LERaw(out->data() + off + 32u, rec.valueA);
        WriteU32LERaw(out->data() + off + 36u, rec.valueB);
    }
    return true;
}

bool ParseMrSeg0Save(const std::vector<std::uint8_t>& raw, MrSeg0SaveData* out, std::string* error) {
    if (out == nullptr) {
        if (error != nullptr) {
            *error = "null output mrseg0 save struct";
        }
        return false;
    }
    if (raw.size() < 24u || ((raw.size() - 12u) % 12u) != 0u) {
        if (error != nullptr) {
            *error = "mrseg0 save has unexpected size";
        }
        return false;
    }

    MrSeg0SaveData parsed;
    parsed.rawSize = raw.size();
    parsed.headerA = ReadU32LERaw(raw.data());
    parsed.headerB = ReadU32LERaw(raw.data() + 4u);
    parsed.headerC = ReadU32LERaw(raw.data() + 8u);

    const std::size_t pointCount = (raw.size() - 12u) / 12u;
    parsed.points.resize(pointCount);
    for (std::size_t i = 0; i < pointCount; ++i) {
        const std::size_t off = 12u + (i * 12u);
        auto& p = parsed.points[i];
        p.x = ReadF32LERaw(raw.data() + off);
        p.y = ReadF32LERaw(raw.data() + off + 4u);
        p.z = ReadF32LERaw(raw.data() + off + 8u);
    }

    *out = std::move(parsed);
    return true;
}

bool BuildRaw(const MrSeg0SaveData& save, std::vector<std::uint8_t>* out, std::string* error) {
    if (out == nullptr) {
        if (error != nullptr) {
            *error = "null output byte vector";
        }
        return false;
    }
    if (save.points.empty()) {
        if (error != nullptr) {
            *error = "mrseg0 save must contain at least one point";
        }
        return false;
    }

    out->assign(12u + save.points.size() * 12u, 0u);
    WriteU32LERaw(out->data(), save.headerA);
    WriteU32LERaw(out->data() + 4u, save.headerB);
    WriteU32LERaw(out->data() + 8u, save.headerC);
    for (std::size_t i = 0; i < save.points.size(); ++i) {
        const std::size_t off = 12u + (i * 12u);
        const auto& p = save.points[i];
        WriteF32LERaw(out->data() + off, p.x);
        WriteF32LERaw(out->data() + off + 4u, p.y);
        WriteF32LERaw(out->data() + off + 8u, p.z);
    }
    return true;
}

}  // namespace profile_sav
