#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace profile_sav {

constexpr std::size_t kFileHeaderSize = 24;
constexpr std::size_t kCoreSize = 84;
constexpr std::size_t kBlock720Size = 720;
constexpr std::size_t kBlock92Size = 92;
constexpr std::size_t kBlock156Size = 156;
constexpr std::uint32_t kMagicForP = 0x50726F66u;
constexpr std::uint32_t kVersion1 = 1u;

struct ProfileSaveData {
    std::array<std::uint8_t, kFileHeaderSize> fileHeader{};
    std::vector<std::uint8_t> core84;
    std::vector<std::uint8_t> block720;
    std::vector<std::uint8_t> block92;
    std::vector<std::uint8_t> block156;
    std::size_t rawSize = 0;
};

struct MrProfileSaveData {
    std::vector<std::uint32_t> words;
    std::size_t rawSize = 0;
};

struct MrTimesRecord {
    std::array<std::uint8_t, 32> nameRaw{};
    std::uint32_t valueA = 0;
    std::uint32_t valueB = 0;
};

struct MrTimesSaveData {
    std::uint32_t count = 0;
    std::vector<MrTimesRecord> records;
    std::size_t rawSize = 0;
};

struct MrSeg0Point {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

struct MrSeg0SaveData {
    std::uint32_t headerA = 0;
    std::uint32_t headerB = 0;
    std::uint32_t headerC = 0;
    std::vector<MrSeg0Point> points;
    std::size_t rawSize = 0;
};

bool ParseProfileSave(const std::vector<std::uint8_t>& raw, ProfileSaveData* out, std::string* error = nullptr);
bool BuildRaw(const ProfileSaveData& save, std::vector<std::uint8_t>* out, std::string* error = nullptr);
bool ParseMrProfileSave(const std::vector<std::uint8_t>& raw, MrProfileSaveData* out, std::string* error = nullptr);
bool BuildRaw(const MrProfileSaveData& save, std::vector<std::uint8_t>* out, std::string* error = nullptr);
bool ParseMrTimesSave(const std::vector<std::uint8_t>& raw, MrTimesSaveData* out, std::string* error = nullptr);
bool BuildRaw(const MrTimesSaveData& save, std::vector<std::uint8_t>* out, std::string* error = nullptr);
bool ParseMrSeg0Save(const std::vector<std::uint8_t>& raw, MrSeg0SaveData* out, std::string* error = nullptr);
bool BuildRaw(const MrSeg0SaveData& save, std::vector<std::uint8_t>* out, std::string* error = nullptr);

std::uint32_t ReadU32LE(const std::vector<std::uint8_t>& bytes, std::size_t offset);
void WriteU32LE(std::vector<std::uint8_t>* bytes, std::size_t offset, std::uint32_t value);

}  // namespace profile_sav
