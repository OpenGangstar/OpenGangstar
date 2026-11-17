#include "retail_constants.h"
#include "FileSystemBase.h"
#include "IFileStream.h"
#include "StreamReader.h"

#include <cstring>
#include <memory>
#include <stdexcept>

namespace {
class ConstantsReader {
public:
    ConstantsReader(FileSystemBase& assets, const char* name) : file(assets.openResource(name)), name(name)
    {
        if (!file || file->size() > 1024 * 1024) fail();
    }

    size_t remaining() const { return static_cast<size_t>(file->size() - file->tell()); }

    uint32_t word()
    {
        unsigned char bytes[4];
        if (remaining() < sizeof bytes ||
            !StreamReader::readStringEx(file.get(), reinterpret_cast<char*>(bytes), sizeof bytes)) fail();
        uint32_t value = 0;
        for (unsigned i = 0; i < sizeof bytes; ++i) value |= uint32_t(bytes[i]) << (8 * i);
        return value;
    }

    size_t count(size_t minSize)
    {
        const auto result = word();
        if (result > remaining() / minSize) fail();
        return result;
    }

    std::string string()
    {
        const auto size = word();
        if (size > remaining()) fail();
        std::string text(size, '\0');
        if (size && !StreamReader::readStringEx(file.get(), text.data(), size)) fail();
        return text;
    }

    int32_t integer()
    {
        const auto bits = word();
        int32_t value;
        std::memcpy(&value, &bits, sizeof value);
        return value;
    }

    [[noreturn]] void fail() const { throw std::runtime_error("Invalid retail constant table " + name); }

private:
    std::unique_ptr<IFileStream> file;
    std::string name;
};
}

RetailConstants readRetailConstants(FileSystemBase& assets, const char* name)
{
    ConstantsReader reader(assets, name);
    RetailConstants table;
    const auto groupCount = reader.count(9);
    for (size_t i = 0; i < groupCount; ++i) {
        const auto group = reader.string();
        if (group.empty() || table.count(group)) reader.fail();
        auto& entries = table[group];
        const auto entryCount = reader.count(9);
        for (size_t j = 0; j < entryCount; ++j) {
            const auto key = reader.string();
            if (key.empty() || entries.count(key)) reader.fail();
            // These two script aliases are length-prefixed strings. The file has no per-value tags.
            if (group == "ScriptSpecial" && (key == "ME" || key == "Player")) {
                entries.emplace(key, reader.string());
            } else {
                entries.emplace(key, reader.integer());
            }
        }
    }
    if (reader.remaining()) reader.fail();
    return table;
}

int32_t retailInteger(const RetailConstants& table, const char* group, const char* key)
{
    const auto section = table.find(group);
    if (section != table.end()) {
        const auto entry = section->second.find(key);
        if (entry != section->second.end() && std::holds_alternative<int32_t>(entry->second))
            return std::get<int32_t>(entry->second);
    }
    throw std::runtime_error(std::string("Missing integer retail constant ") + group + "." + key);
}
