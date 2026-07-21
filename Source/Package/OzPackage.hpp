#pragma once
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <algorithm>
#include <filesystem>

// ============================================================================
// OzPackage — universal asset container format for Angels95 / OmegaTech
//
// Package types (magic):
//   OZPK  — generic asset package (.ozpak) — models, mtl, scripts
//   OZTX  — texture package     (.oztex) — .png textures
//   OZSD  — sound package       (.ozsnd) — .wav, .mp3, .ogg
//   OZMX  — music package       (.ozmux) — .wav, .mp3
//   OZWN  — world package       (.ozone) — .ozone world files
//
// Format:
//   [HEADER]  32 bytes
//   [DATA_0]  file data (8-byte aligned start)
//   [DATA_1]
//   ...
//   [INDEX]   array of OzPackageEntry
// ============================================================================

constexpr uint32_t OZ_PACKAGE_MAGIC_PK  = 0x4B5A504F; // "OZPK"
constexpr uint32_t OZ_PACKAGE_MAGIC_TX  = 0x58545A4F; // "OZTX"
constexpr uint32_t OZ_PACKAGE_MAGIC_SD  = 0x44535A4F; // "OZSD"
constexpr uint32_t OZ_PACKAGE_MAGIC_MX  = 0x584D5A4F; // "OZMX"
constexpr uint32_t OZ_PACKAGE_MAGIC_WN  = 0x4E575A4F; // "OZWN"

constexpr uint32_t OZ_PACKAGE_VERSION   = 1;
constexpr size_t   OZ_FILENAME_MAX      = 128;

#pragma pack(push, 1)
struct OzPackageHeader {
    uint32_t magic;           // OZPK/OZTX/OZSD/OZMX/OZWN
    uint16_t version;         // 1
    uint16_t flags;           // bit 0: zlib-compressed
    uint32_t entryCount;      // number of files
    uint32_t indexOffset;     // byte offset from start to index array
    uint32_t indexSize;       // byte size of index array
    uint32_t reserved[2];     // future use (8 bytes)
    // total: 32 bytes
};

struct OzPackageEntry {
    char     filename[OZ_FILENAME_MAX]; // null-terminated, forward slashes
    uint64_t offset;          // byte offset from start of file to data
    uint64_t sizeRaw;         // uncompressed size
    uint64_t sizePacked;      // compressed size (= sizeRaw if no compression)
    uint32_t reserved;        // future use
    // total: 156 bytes
};
#pragma pack(pop)

// ============================================================================
// Writer — builds a package from a list of (filename, data) pairs
// ============================================================================
struct OzPackageFile {
    std::string filename;
    std::vector<uint8_t> data;
};

class OzPackageWriter {
public:
    OzPackageWriter(uint32_t magic) : m_magic(magic) {}

    void AddFile(const char* name, const void* data, size_t size) {
        OzPackageFile f;
        f.filename = name;
        const uint8_t* p = (const uint8_t*)data;
        f.data.assign(p, p + size);
        m_files.push_back(std::move(f));
    }

    bool WriteToFile(const char* path) {
        // Ensure parent directory exists
        std::string pathStr(path);
        auto slash = pathStr.find_last_of("/\\");
        if (slash != std::string::npos) {
            std::string dir = pathStr.substr(0, slash);
            std::filesystem::create_directories(dir);
        }
        FILE* f = fopen(path, "wb");
        if (!f) return false;

        // Calculate offsets
        OzPackageHeader hdr;
        memset(&hdr, 0, sizeof(hdr));
        hdr.magic = m_magic;
        hdr.version = OZ_PACKAGE_VERSION;
        hdr.flags = 0;
        hdr.entryCount = (uint32_t)m_files.size();

        uint64_t dataOff = sizeof(OzPackageHeader);

        // Sort files alphabetically for deterministic output
        std::sort(m_files.begin(), m_files.end(),
            [](const OzPackageFile& a, const OzPackageFile& b) {
                return a.filename < b.filename;
            });

        // Build entries array
        std::vector<OzPackageEntry> entries;
        for (auto& fdata : m_files) {
            OzPackageEntry e;
            memset(&e, 0, sizeof(e));
            strncpy(e.filename, fdata.filename.c_str(), OZ_FILENAME_MAX - 1);
            e.offset = dataOff;
            e.sizeRaw = fdata.data.size();
            e.sizePacked = fdata.data.size();
            e.reserved = 0;
            entries.push_back(e);
            dataOff += fdata.data.size();
        }

        hdr.indexOffset = (uint32_t)dataOff;
        hdr.indexSize = (uint32_t)(entries.size() * sizeof(OzPackageEntry));

        // Write header
        fwrite(&hdr, sizeof(hdr), 1, f);

        // Write file data
        for (size_t i = 0; i < m_files.size(); i++) {
            fwrite(m_files[i].data.data(), 1, m_files[i].data.size(), f);
        }

        // Write index
        fwrite(entries.data(), 1, entries.size() * sizeof(OzPackageEntry), f);

        fclose(f);
        return true;
    }

private:
    uint32_t m_magic;
    std::vector<OzPackageFile> m_files;
};

// ============================================================================
// Reader — opens a package and provides random access by filename
// ============================================================================
class OzPackageReader {
public:
    OzPackageReader() : m_data(nullptr), m_size(0), m_owned(false), m_magic(0), m_version(0) {}

    ~OzPackageReader() { Close(); }

    OzPackageReader(const OzPackageReader&) = delete;
    OzPackageReader& operator=(const OzPackageReader&) = delete;

    OzPackageReader(OzPackageReader&& other) noexcept
        : m_data(other.m_data), m_size(other.m_size), m_owned(other.m_owned),
          m_magic(other.m_magic), m_version(other.m_version), m_entries(std::move(other.m_entries)) {
        other.m_data = nullptr;
        other.m_size = 0;
        other.m_owned = false;
    }

    OzPackageReader& operator=(OzPackageReader&& other) noexcept {
        if (this != &other) {
            Close();
            m_data = other.m_data;
            m_size = other.m_size;
            m_owned = other.m_owned;
            m_magic = other.m_magic;
            m_version = other.m_version;
            m_entries = std::move(other.m_entries);
            other.m_data = nullptr;
            other.m_size = 0;
            other.m_owned = false;
        }
        return *this;
    }

    bool Open(const char* path) {
        FILE* f = fopen(path, "rb");
        if (!f) return false;

        OzPackageHeader hdr;
        if (fread(&hdr, sizeof(hdr), 1, f) != 1) { fclose(f); return false; }

        m_magic = hdr.magic;
        m_version = hdr.version;

        // Read all data into memory
        fseek(f, 0, SEEK_END);
        m_size = (size_t)ftell(f);
        fseek(f, 0, SEEK_SET);

        m_data = new uint8_t[m_size];
        if (fread(m_data, 1, m_size, f) != m_size) { fclose(f); Close(); return false; }
        fclose(f);
        m_owned = true;

        return ParseIndex();
    }

    bool OpenMem(const void* data, size_t size) {
        m_data = (uint8_t*)data;
        m_size = size;
        m_owned = false;
        return ParseIndex();
    }

    void Close() {
        if (m_owned && m_data) { delete[] m_data; }
        m_data = nullptr;
        m_size = 0;
        m_entries.clear();
        m_owned = false;
    }

    uint32_t Magic() const { return m_magic; }
    uint32_t EntryCount() const { return (uint32_t)m_entries.size(); }

    // Find entry by exact filename (forward slashes)
    const OzPackageEntry* Find(const char* name) const {
        for (auto& e : m_entries) {
            if (strcmp(e.filename, name) == 0)
                return &e;
        }
        return nullptr;
    }

    // Find entry by basename only (ignores path components)
    const OzPackageEntry* FindBasename(const char* name) const {
        for (auto& e : m_entries) {
            const char* base = strrchr(e.filename, '/');
            if (!base) base = strrchr(e.filename, '\\');
            if (base) base++; else base = e.filename;
            if (strcmp(base, name) == 0)
                return &e;
        }
        return nullptr;
    }

    // Read file data into a buffer. Returns bytes read, 0 if not found.
    size_t Read(const char* name, std::vector<uint8_t>& out) const {
        const OzPackageEntry* e = Find(name);
        if (!e) return 0;
        out.resize((size_t)e->sizeRaw);
        memcpy(out.data(), m_data + e->offset, (size_t)e->sizeRaw);
        return (size_t)e->sizeRaw;
    }

    // Get pointer to file data in the memory-mapped buffer
    const uint8_t* GetData(const char* name, size_t& outSize) const {
        const OzPackageEntry* e = Find(name);
        if (!e) { outSize = 0; return nullptr; }
        outSize = (size_t)e->sizeRaw;
        return m_data + e->offset;
    }

    // List all filenames
    void List(std::vector<std::string>& names) const {
        names.clear();
        names.reserve(m_entries.size());
        for (auto& e : m_entries)
            names.push_back(e.filename);
    }

    const std::vector<OzPackageEntry>& Entries() const { return m_entries; }

private:
    bool ParseIndex() {
        OzPackageHeader* hdr = (OzPackageHeader*)m_data;
        if (hdr->version != OZ_PACKAGE_VERSION) return false;

        uint32_t count = hdr->entryCount;
        OzPackageEntry* idx = (OzPackageEntry*)(m_data + hdr->indexOffset);

        m_entries.clear();
        m_entries.reserve(count);
        for (uint32_t i = 0; i < count; i++) {
            m_entries.push_back(idx[i]);
        }
        return true;
    }

    uint8_t* m_data;
    size_t m_size;
    bool m_owned;
    uint32_t m_magic;
    uint32_t m_version;
    std::vector<OzPackageEntry> m_entries;
};
