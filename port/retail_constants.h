#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <variant>

class FileSystemBase;

using RetailConstant = std::variant<int32_t, std::string>;
using RetailConstants = std::map<std::string, std::map<std::string, RetailConstant>>;

RetailConstants readRetailConstants(FileSystemBase& assets, const char* name);
int32_t retailInteger(const RetailConstants& table, const char* group, const char* key);
