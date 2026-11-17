#pragma once

#include "FileSystemBase.h"
#include "IFileStream.h"

#include <filesystem>
#include <string>
#include <vector>

class ResourceFileSystem final : public FileSystemBase {
public:
    explicit ResourceFileSystem(const std::filesystem::path& root,
                                const std::filesystem::path& saves = {});

    const char* getRootPath() const override;
    const char* getResourcesPath() const override;
    const char* getSaveFilesPath() const override;
    void closeFile(IFileStream*& file) override;
    IFileStream* openFile(const char* name) override;
    IFileStream* openResource(const char* name) override;
    IFileStream* openSavefile(const char* name) override;
    IFileStream* openSavedFile(const char* name) const;
    void replaceSaveFile(const char* name, const std::vector<unsigned char>& bytes) const;

private:
    std::filesystem::path root;
    std::filesystem::path saves;
    std::string resourcePath;
    std::string savePath;
};

std::filesystem::path defaultSaveDirectory();
