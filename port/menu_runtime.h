#pragma once

#include "texture.h"
#include "retail_text.h"

#include <memory>
#include <map>
#include <cstdint>
#include <string>

class ResourceFileSystem;
struct MenuNavigation;

class MenuRuntime {
public:
    MenuRuntime(ResourceFileSystem& assets, const std::map<uint16_t, std::string>& exports,
                RetailLanguage language = RetailLanguage::english, bool startInOptions = false);
    ~MenuRuntime();
    void advance(double milliseconds);
    void pointer(int x, int y, bool down);
    void navigate(const MenuNavigation& navigation, bool persistPreferences = true);
    bool takeNewGameRequest();
    bool takeExitRequest();
    std::optional<RetailLanguage> takeLanguageRequest();
    Image draw(const Image& background);

private:
    struct Runtime;
    std::unique_ptr<Runtime> runtime;
};
