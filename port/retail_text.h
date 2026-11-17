#pragma once

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

class FileSystemBase;

enum class RetailLanguage { english, french, german, italian, japanese, spanish };

inline constexpr std::array retailLanguagePacks{
    RetailLanguage::english, RetailLanguage::french, RetailLanguage::german,
    RetailLanguage::spanish, RetailLanguage::italian, RetailLanguage::japanese
};

std::optional<RetailLanguage> retailLanguage(std::string_view name);
const char* retailLanguageName(RetailLanguage language);
std::vector<std::string> readRetailStrings(FileSystemBase& assets, const std::string& path);
std::vector<std::string> readLocalizedStrings(FileSystemBase& assets, std::string_view table,
                                              RetailLanguage language);
