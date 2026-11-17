#include "filesystem.h"
#include "retail_text.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shlobj.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <limits>
#include <memory>
#include <stdexcept>
#include <system_error>

namespace {
using File = std::unique_ptr<std::FILE, decltype(&std::fclose)>;

class ResourceStream final : public IFileStream {
public:
    ResourceStream(File file, unsigned long long length, bool writable = false)
        : file(std::move(file)), length(length), writable(writable) {}

    unsigned long long size() const override { return length; }
    bool canRead() const override { return true; }
    bool canWrite() const override { return writable; }

    unsigned long long read(unsigned char* destination, unsigned long long count) override
    {
        const auto bytes = std::min(count, static_cast<unsigned long long>(std::numeric_limits<size_t>::max()));
        return std::fread(destination, 1, static_cast<size_t>(bytes), file.get());
    }

    unsigned long long write(const unsigned char* source, unsigned long long count) override
    {
        if (!writable) return 0;
        const auto bytes = std::min(count, static_cast<unsigned long long>(std::numeric_limits<size_t>::max()));
        const auto written = std::fwrite(source, 1, static_cast<size_t>(bytes), file.get());
        length = std::max(length, tell());
        return written;
    }

    void seek(unsigned long long position) override
    {
        _fseeki64(file.get(), static_cast<long long>(std::min(position, length)), SEEK_SET);
    }

    unsigned long long tell() const override
    {
        return static_cast<unsigned long long>(std::max(0LL, _ftelli64(file.get())));
    }

private:
    File file;
    unsigned long long length;
    bool writable;
};

IFileStream* openRead(const std::filesystem::path& path)
{
    File file(_wfopen(path.c_str(), L"rb"), std::fclose);
    if (!file || _fseeki64(file.get(), 0, SEEK_END) != 0) return nullptr;
    const auto length = _ftelli64(file.get());
    if (length < 0 || _fseeki64(file.get(), 0, SEEK_SET) != 0) return nullptr;
    return new ResourceStream(std::move(file), static_cast<unsigned long long>(length));
}

bool within(const std::filesystem::path& child, const std::filesystem::path& parent)
{
    auto part = child.begin();
    for (const auto& prefix : parent) {
        if (prefix.empty()) continue;
        if (part == child.end() || CompareStringOrdinal(part->c_str(), -1, prefix.c_str(), -1, TRUE) != CSTR_EQUAL)
            return false;
        ++part;
    }
    return true;
}

std::filesystem::path saveName(const char* name)
{
    auto path = std::filesystem::u8path(name);
    const auto& text = path.native();
    if (text.empty() || text.back() == L'.' || text.back() == L' ' ||
        text.find_first_of(L"<>:\"/\\|?*") != std::wstring::npos)
        return {};
    return path;
}

void requireSeparateSaveRoot(const std::filesystem::path& saves, const std::filesystem::path& resources)
{
    if (within(std::filesystem::weakly_canonical(saves), std::filesystem::weakly_canonical(resources)))
        throw std::runtime_error("The save directory must be outside the retail data directory.");
}

[[noreturn]] void fileError(const char* action, DWORD code = GetLastError())
{
    throw std::system_error(static_cast<int>(code), std::system_category(), action);
}
}

std::filesystem::path defaultSaveDirectory()
{
    PWSTR raw = nullptr;
    const auto result = SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &raw);
    const std::unique_ptr<wchar_t, decltype(&CoTaskMemFree)> path(raw, CoTaskMemFree);
    if (FAILED(result) || !path) throw std::runtime_error("Could not locate the Windows local application data directory.");
    return std::filesystem::path(path.get()) / "OpenGangstar" / "Saves";
}

ResourceFileSystem::ResourceFileSystem(const std::filesystem::path& root, const std::filesystem::path& saves)
    : root(std::filesystem::absolute(root).lexically_normal()),
      saves(saves.empty() ? saves : std::filesystem::absolute(saves).lexically_normal()),
      resourcePath((this->root / "").generic_u8string()),
      savePath(this->saves.empty() ? "" : (this->saves / "").generic_u8string())
{
    if (!this->saves.empty()) requireSeparateSaveRoot(this->saves, this->root);
}

const char* ResourceFileSystem::getRootPath() const { return resourcePath.c_str(); }
const char* ResourceFileSystem::getResourcesPath() const { return resourcePath.c_str(); }
const char* ResourceFileSystem::getSaveFilesPath() const { return savePath.c_str(); }

void ResourceFileSystem::closeFile(IFileStream*& file)
{
    delete file;
    file = nullptr;
}

IFileStream* ResourceFileSystem::openFile(const char* name) { return openResource(name); }
IFileStream* ResourceFileSystem::openSavefile(const char* name)
{
    const auto relative = saveName(name);
    if (saves.empty() || relative.empty()) return nullptr;
    try {
        // Match the original w+b contract without truncating a linked file.
        replaceSaveFile(name, {});
    } catch (const std::runtime_error&) {
        return nullptr;
    }
    File file(_wfopen((saves / relative).c_str(), L"r+b"), std::fclose);
    return file ? new ResourceStream(std::move(file), 0, true) : nullptr;
}

IFileStream* ResourceFileSystem::openSavedFile(const char* name) const
{
    const auto relative = saveName(name);
    return saves.empty() || relative.empty() ? nullptr : openRead(saves / relative);
}

void ResourceFileSystem::replaceSaveFile(const char* name, const std::vector<unsigned char>& bytes) const
{
    const auto relative = saveName(name);
    if (saves.empty() || relative.empty()) throw std::runtime_error("Invalid save directory or filename.");
    if (bytes.size() > MAXDWORD) throw std::runtime_error("Save file exceeds the supported size.");
    requireSeparateSaveRoot(saves, root);
    std::filesystem::create_directories(saves);
    GUID id{};
    wchar_t idText[40]{};
    if (FAILED(CoCreateGuid(&id)) || !StringFromGUID2(id, idText, static_cast<int>(std::size(idText))))
        throw std::runtime_error("Could not generate a temporary save filename.");

    struct TemporarySave {
        std::filesystem::path path;
        HANDLE handle = INVALID_HANDLE_VALUE;
        bool exists = false;
        ~TemporarySave()
        {
            if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle);
            if (exists) DeleteFileW(path.c_str());
        }
    } temporary{saves / (std::wstring(idText) + L".tmp")};
    temporary.handle = CreateFileW(temporary.path.c_str(), GENERIC_WRITE, 0, nullptr,
                                  CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (temporary.handle == INVALID_HANDLE_VALUE) fileError("Create temporary save");
    temporary.exists = true;
    DWORD written = 0;
    if (!bytes.empty()) {
        if (!WriteFile(temporary.handle, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr))
            fileError("Write temporary save");
        if (written != bytes.size()) fileError("Write temporary save", ERROR_WRITE_FAULT);
    }
    if (!FlushFileBuffers(temporary.handle)) fileError("Flush temporary save");
    if (!CloseHandle(temporary.handle)) fileError("Close temporary save");
    temporary.handle = INVALID_HANDLE_VALUE;
    if (!MoveFileExW(temporary.path.c_str(), (saves / relative).c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        fileError("Replace save");
    temporary.exists = false;
}

IFileStream* ResourceFileSystem::openResource(const char* name)
{
    const auto relative = std::filesystem::u8path(name).lexically_normal();
    if (relative.empty() || relative.has_root_path() ||
        std::find(relative.begin(), relative.end(), "..") != relative.end())
        return nullptr;

    return openRead(root / relative);
}

namespace {
constexpr std::array languageNames{"english", "french", "german", "italian", "japanese", "spanish"};
}

std::optional<RetailLanguage> retailLanguage(std::string_view name)
{
    const auto found = std::find(languageNames.begin(), languageNames.end(), name);
    if (found == languageNames.end()) return {};
    return static_cast<RetailLanguage>(found - languageNames.begin());
}

const char* retailLanguageName(RetailLanguage language)
{
    return languageNames.at(static_cast<size_t>(language));
}

std::vector<std::string> readRetailStrings(FileSystemBase& assets, const std::string& path)
{
    const std::unique_ptr<IFileStream> file(assets.openResource(path.c_str()));
    if (!file) return {};
    if (file->size() < 2 || file->size() > 1024 * 1024)
        throw std::runtime_error("Invalid retail string table size for " + path);
    std::vector<unsigned char> bytes(static_cast<size_t>(file->size()));
    if (file->read(bytes.data(), bytes.size()) != bytes.size())
        throw std::runtime_error("Could not read retail string table " + path);

    size_t offset = 0;
    const auto readLength = [&] {
        if (bytes.size() - offset < 2) throw std::runtime_error("Truncated retail string table " + path);
        const unsigned length = bytes[offset] | (unsigned(bytes[offset + 1]) << 8);
        offset += 2;
        return length;
    };
    const unsigned count = readLength();
    std::vector<std::string> strings;
    strings.reserve(count);
    for (unsigned i = 0; i < count; ++i) {
        const auto length = readLength();
        if (length > bytes.size() - offset)
            throw std::runtime_error("Truncated retail string in " + path);
        const auto* text = reinterpret_cast<const char*>(bytes.data() + offset);
        if (length && (std::find(text, text + length, '\0') != text + length ||
            !MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, length, nullptr, 0)))
            throw std::runtime_error("Invalid UTF-8 in retail string table " + path);
        strings.emplace_back(text, length);
        offset += length;
    }
    if (offset != bytes.size() || strings.empty())
        throw std::runtime_error("Invalid retail string table " + path);
    return strings;
}

std::vector<std::string> readLocalizedStrings(FileSystemBase& assets, std::string_view table,
                                              RetailLanguage language)
{
    const auto prefix = std::string(table) + '.';
    auto strings = readRetailStrings(assets, prefix + retailLanguageName(language));
    if (strings.empty() && language != RetailLanguage::english)
        strings = readRetailStrings(assets, prefix + "english");
    return strings;
}
