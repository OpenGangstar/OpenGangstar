#include "city_assets.h"
#include "filesystem.h"
#include "FileSystemBase.h"
#include "IFileStream.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>

namespace {
using Bytes = std::vector<unsigned char>;
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
template<class F> void rejects(F operation) {
    try { operation(); } catch (const std::runtime_error&) { return; }
    throw std::runtime_error("Expected checked metatype rejection");
}
class MemoryStream final : public IFileStream {
public:
    const Bytes& bytes;
    unsigned long long reportedSize;
    size_t cursor = 0;
    explicit MemoryStream(const Bytes& value, unsigned long long size) : bytes(value), reportedSize(size) {}
    unsigned long long size() const override { return reportedSize; }
    bool canRead() const override { return true; }
    bool canWrite() const override { return false; }
    unsigned long long tell() const override { return cursor; }
    void seek(unsigned long long offset) override { cursor = static_cast<size_t>(std::min<unsigned long long>(offset, bytes.size())); }
    unsigned long long read(unsigned char* out, unsigned long long count) override {
        const auto n = static_cast<size_t>(std::min<unsigned long long>(count, bytes.size() - cursor));
        std::copy_n(bytes.data() + cursor, n, out); cursor += n; return n;
    }
    unsigned long long write(const unsigned char*, unsigned long long) override { return 0; }
};
class MemoryFiles final : public FileSystemBase {
public:
    Bytes bytes;
    bool missing = false;
    unsigned long long declaredSize = 0;
    const char* getRootPath() const override { return ""; }
    const char* getResourcesPath() const override { return ""; }
    const char* getSaveFilesPath() const override { return ""; }
    void closeFile(IFileStream*& file) override { delete file; file = nullptr; }
    IFileStream* openFile(const char* name) override { return openResource(name); }
    IFileStream* openResource(const char* name) override {
        return missing || std::string_view(name) != "metatypes.gmap" ? nullptr :
            new MemoryStream(bytes, declaredSize ? declaredSize : bytes.size());
    }
    IFileStream* openSavefile(const char*) override { return nullptr; }
};
void word(Bytes& bytes, uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) bytes.push_back(static_cast<unsigned char>(value >> (24 - i * 8)));
}
Bytes record(int32_t type, const std::string& name, Bytes tail = {}) {
    Bytes result; uint32_t encoded; std::memcpy(&encoded, &type, 4); word(result, encoded);
    if (type != -1) { word(result, static_cast<uint32_t>(name.size())); result.insert(result.end(), name.begin(), name.end()); }
    result.insert(result.end(), tail.begin(), tail.end()); return result;
}
Bytes frame(const std::vector<Bytes>& records) {
    Bytes result; word(result, static_cast<uint32_t>(records.size()));
    for (const auto& record : records) { word(result, static_cast<uint32_t>(record.size())); result.insert(result.end(), record.begin(), record.end()); }
    return result;
}
void synthetic() {
    const std::vector<Bytes> records{
        record(-1, "", {0xaa, 0xbb, 0xcc}), record(7, "act", {1, 2, 3}), record(3, "Actor", {4}),
        record(5, "ACTOR", {5}), record(99, std::string("Actor\0", 6), {6}), record(42, ""),
        record(-2, std::string("\xc1") + "B", {7}), record(17, std::string(1, '\0'))
    };
    MemoryFiles files; files.bytes = frame(records);
    files.bytes.insert(files.bytes.end(), {'T', 'A', 'I', 'L', 0, 255});
    const auto catalogue = loadCityMetatypeCatalog(files);
    require(catalogue.records.size() == records.size(), "Every indexed slot survives independent of type");
    for (size_t i = 0; i < records.size(); ++i)
        require(catalogue.records[i].encoded == records[i], "Exact payload including type/name/unknown fields");
    require(catalogue.records[0].type == -1 && catalogue.records[0].name.empty() &&
                catalogue.records[0].sourceNameLength == 0 && catalogue.records[4].name == "Actor" &&
                catalogue.records[4].sourceNameLength == 6 && catalogue.records[7].name.empty() &&
                catalogue.records[7].sourceNameLength == 1, "Original signed type and serialized optional NUL length");
    require(catalogue.find("ACTOR-extra") == 2 && catalogue.find("aCt") == 1 && catalogue.find("Actor") == 4 &&
                catalogue.find("") == 7 && catalogue.find("ac") == -1,
            "Longest ASCII byte prefix, first equal-length tie, terminator participates and zero length cannot win");
    require(catalogue.find(std::string("\xc1") + "b-tail") == 6 && catalogue.find(std::string("\xe1") + "b") == -1,
            "ASCII folding leaves non-ASCII bytes unchanged");
    rejects([&] { (void)catalogue.find(std::string("Actor\0suffix", 12)); });
    auto bad = catalogue; bad.records[1].sourceNameLength = 999;
    rejects([&] { (void)bad.find("act"); });
    bad = catalogue; bad.records[1].name = std::string("a\0b", 3);
    rejects([&] { (void)bad.find("a"); });
    bad = catalogue; bad.records.resize(257);
    rejects([&] { (void)bad.find("act"); });
    std::vector<Bytes> maximum(256, record(-1, "")); maximum.back() = record(9, "last");
    files.bytes = frame(maximum);
    require(loadCityMetatypeCatalog(files).find("LAST-instance") == 255, "Highest valid 8-bit slot");
    files.bytes = frame({});
    require(loadCityMetatypeCatalog(files).records.empty(), "Empty standalone catalogue is valid");
    files.bytes = frame(std::vector<Bytes>(257, record(-1, "")));
    rejects([&] { (void)loadCityMetatypeCatalog(files); });
    files.bytes = {0, 0, 0}; rejects([&] { (void)loadCityMetatypeCatalog(files); });
    files.bytes = frame({record(7, "name")}); files.bytes.pop_back();
    rejects([&] { (void)loadCityMetatypeCatalog(files); });
    files.bytes = frame({Bytes{0, 0, 0}}); rejects([&] { (void)loadCityMetatypeCatalog(files); });
    files.bytes = frame({Bytes{0, 0, 0, 9, 0, 0, 0, 20, 'x'}});
    rejects([&] { (void)loadCityMetatypeCatalog(files); });
    files.bytes = frame({record(1, std::string("a\0b", 3))});
    rejects([&] { (void)loadCityMetatypeCatalog(files); });
    files.bytes = frame({record(1, std::string("a\0\0", 3))});
    rejects([&] { (void)loadCityMetatypeCatalog(files); });
    files.bytes = frame({}); files.declaredSize = 64ull * 1024 * 1024 + 1;
    rejects([&] { (void)loadCityMetatypeCatalog(files); });
    files.declaredSize = 0; files.missing = true;
    rejects([&] { (void)loadCityMetatypeCatalog(files); });
}
uint32_t rawWord(const Bytes& bytes, size_t offset) {
    require(offset <= bytes.size() && bytes.size() - offset >= 4, "Raw metatype framing bounds");
    uint32_t value = 0;
    for (size_t i = 0; i < 4; ++i) value = (value << 8) | bytes[offset + i];
    return value;
}
void retail(const std::filesystem::path& directory, const char* queries) {
    ResourceFileSystem files(directory);
    const auto catalogue = loadCityMetatypeCatalog(files);
    std::ifstream file(directory / "metatypes.gmap", std::ios::binary);
    const Bytes raw((std::istreambuf_iterator<char>(file)), {});
    require(raw.size() == 37788 && rawWord(raw, 0) == 196 && catalogue.records.size() == 196,
            "Supplied full framing and all196 original indexed slots");
    size_t cursor = 4, payload = 0, invalid = 0, names = 0;
    for (size_t i = 0; i < catalogue.records.size(); ++i) {
        const auto size = rawWord(raw, cursor); cursor += 4;
        require(size <= raw.size() - cursor, "Raw payload bounds");
        const auto& record = catalogue.records[i];
        require(record.encoded.size() == size && std::equal(record.encoded.begin(), record.encoded.end(), raw.begin() + cursor),
                "All encoded object bytes exactly match supplied record framing");
        uint32_t type; std::memcpy(&type, &record.type, 4);
        require(type == rawWord(raw, cursor), "Signed decoded type bits match raw source");
        if (record.type == -1) ++invalid;
        else {
            const auto length = rawWord(raw, cursor + 4);
            require(record.sourceNameLength == length && record.name.size() == length &&
                        std::equal(record.name.begin(), record.name.end(), raw.begin() + cursor + 8) &&
                        std::all_of(record.name.begin(), record.name.end(), [](unsigned char byte) { return byte > 0 && byte < 128; }),
                    "Every real source name/serialized length is ASCII and nonterminated");
            ++names;
        }
        cursor += size; payload += size;
    }
    require(cursor == raw.size() && payload == 37000 && invalid == 3 && names == 193,
            "All37000 object bytes,193 names and three invalid slots accounted");
    if (queries && std::string_view(queries) != "--full-retail") {
        std::ifstream input(queries); require(bool(input), "Original query fixture input");
        std::string line; size_t count = 0;
        while (std::getline(input, line)) {
            const auto split = line.find('\t'); require(split != std::string::npos, "Original query framing");
            const auto expected = std::stoi(line.substr(split + 1));
            require(catalogue.find(std::string_view(line).substr(0, split)) == expected, "Exact original740 query index comparison");
            ++count;
        }
        require(count == 740, "All original case/prefix/tie/missing query results consumed");
        std::cout << count << " original query indices match native\n";
    }
    if (queries && std::string_view(queries) == "--full-retail") {
        const auto city = loadCityAssets(files);
        require(city.metatypes.records.size() == catalogue.records.size() && city.spawnGroups.size() == 19 &&
                    city.spawnMetatypes.size() == 45 && city.weaponMetatypes.size() == 11,
                "Real spawn/weapon binding consumes complete catalogue without behavior loss");
        for (size_t i = 0; i < catalogue.records.size(); ++i)
            require(city.metatypes.records[i].encoded == catalogue.records[i].encoded, "City retains identical canonical raw records");
        for (const auto& district : city.districts)
            for (const auto& point : district.spawnPoints)
                require(city.metatypes.find(point.name) == point.group, "Every original spawn prefix keeps its existing group ID");
        std::cout << "19 spawn groups,45 spawn metatypes and11 weapons preserved\n";
    }
    std::cout << "196 records/37000 payload bytes/193 names match raw retail metadata\n";
}
}
int main(int argc, char** argv) {
    try {
        synthetic();
        if (argc > 1) retail(argv[1], argc > 2 ? argv[2] : nullptr);
        std::cout << "Original metatype catalogue and prefix matching checks passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
