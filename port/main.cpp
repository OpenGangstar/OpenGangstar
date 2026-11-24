#include "retail_save.h"
#include "retail_constants.h"
#include "filesystem.h"
#include "intro_movie.h"
#include "menu_swf.h"
#include "startup_logo.h"
#include "vulkan.h"
#include "win32_touch.h"

#include <algorithm>
#include <cwchar>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace {
Image readSplashBitmap(FileSystemBase& assets, const SwfBitmap& layer, uint32_t width, uint32_t height, bool overlay)
{
    const std::unique_ptr<IFileStream> stream(assets.openResource(layer.texture.c_str()));
    if (!stream) throw std::runtime_error("Could not open the SWF splash texture: " + layer.texture);
    const auto texture = readTexture(*stream);
    Image frame{width, height, std::vector<uint32_t>(size_t(width) * height, overlay ? 0u : 0xff000000u)};
    const int left = std::max(0, layer.x);
    const int right = std::min(int(width), layer.right);
    const int top = std::max(0, layer.y);
    const int bottom = std::min(int(height), layer.bottom);
    if (left >= right || top >= bottom) throw std::runtime_error("The SWF splash lies outside the stage.");
    if (left < layer.textureX || right - layer.textureX > int(texture.width) ||
        top < layer.textureY || bottom - layer.textureY > int(texture.height))
        throw std::runtime_error("The SWF splash shape reads outside its texture.");
    for (int row = top; row < bottom; ++row) {
        const auto* source = texture.bgra.data() + size_t(row - layer.textureY) * texture.width + (left - layer.textureX);
        auto* destination = frame.bgra.data() + size_t(row) * frame.width + left;
        std::copy_n(source, right - left, destination);
    }
    return frame;
}

SplashFrames readSplash(FileSystemBase& assets)
{
    const std::unique_ptr<IFileStream> menu(assets.openResource("newmenu.swf"));
    if (!menu) throw std::runtime_error("Could not open newmenu.swf in the game data.");
    auto splash = readSwfSplash(*menu);
    std::cout << "Loaded widgetSplash show and hide frames from newmenu.swf.\n";
    return {readSplashBitmap(assets, splash.show, splash.width, splash.height, false),
            readSplashBitmap(assets, splash.background, splash.width, splash.height, false),
            readSplashBitmap(assets, splash.outgoing, splash.width, splash.height, true),
            splash.frameMs, std::move(splash.opacities), std::move(splash.exports)};
}

Image readPreview(FileSystemBase& assets, const std::filesystem::path& path)
{
    const auto resource = path.generic_u8string();
    const std::unique_ptr<IFileStream> stream(assets.openResource(resource.c_str()));
    if (!stream) throw std::runtime_error("Could not open the requested texture in the game data: " + resource);
    return readTexture(*stream);
}

void compositePreview(Image& image)
{
    for (uint32_t y = 0; y < image.height; ++y) {
        for (uint32_t x = 0; x < image.width; ++x) {
            auto& pixel = image.bgra[size_t(y) * image.width + x];
            const uint32_t alpha = pixel >> 24;
            const uint32_t background = ((x / 8 + y / 8) % 2) ? 160 : 96;
            uint32_t composite = 0xff000000;
            for (unsigned shift = 0; shift < 24; shift += 8) {
                const uint32_t source = (pixel >> shift) & 255;
                composite |= ((source * alpha + background * (255 - alpha) + 127) / 255) << shift;
            }
            pixel = composite;
        }
    }
}

LRESULT CALLBACK windowMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (message == WM_NCCREATE) {
        const auto* creation = reinterpret_cast<const CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(creation->lpCreateParams));
    }
    auto* touch = reinterpret_cast<Win32TouchInput*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (touch && touch->message(window, message, wParam, lParam))
        return 0;
    if (message == WM_CLOSE) {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

HWND createWindow(const Image& image, Win32TouchInput& touch)
{
    const HINSTANCE instance = GetModuleHandleW(nullptr);
    WNDCLASSW windowClass{};
    windowClass.lpfnWndProc = windowMessage;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    windowClass.lpszClassName = L"OpenGangstar";
    if (!RegisterClassW(&windowClass))
        throw std::runtime_error("Could not register the OpenGangstar window.");
    constexpr DWORD style = WS_CAPTION | WS_SYSMENU;
    const double scale = std::min(double(std::clamp(image.width, 256u, 1024u)) / image.width,
                                  700.0 / image.height);
    const auto width = std::max(1L, static_cast<LONG>(image.width * scale));
    const auto height = std::max(1L, static_cast<LONG>(image.height * scale));
    RECT area{0, 0, width, height};
    AdjustWindowRect(&area, style, FALSE);
    HWND window = CreateWindowExW(0, windowClass.lpszClassName, L"OpenGangstar - West Coast Hustle",
                                 style, CW_USEDEFAULT, CW_USEDEFAULT, area.right - area.left,
                                 area.bottom - area.top, nullptr, nullptr, instance, &touch);
    if (!window)
        throw std::runtime_error("Could not create the OpenGangstar window.");
    return window;
}
}

int wmain(int argc, wchar_t** argv)
{
    bool smokeTest = false;
    bool openingSmokeTest = false;
    std::filesystem::path texture;
    std::filesystem::path saves;
    std::optional<RetailLanguage> language;
    if (argc < 2) {
        std::cerr << "Usage: gangstar-wch.exe <extracted game data folder> [--language <name>] [--save-dir <folder>] "
                     "[--texture <file>] [--smoke-test | --opening-smoke-test]\n";
        return 2;
    }

    try {
        static_assert(sizeof(void*) == 8, "The game target must be x86-64.");
        for (int i = 2; i < argc; ++i) {
            if (std::wcscmp(argv[i], L"--smoke-test") == 0) {
                smokeTest = true;
            } else if (std::wcscmp(argv[i], L"--opening-smoke-test") == 0) {
                smokeTest = true;
                openingSmokeTest = true;
            } else if (std::wcscmp(argv[i], L"--texture") == 0 && i + 1 < argc && texture.empty()) {
                texture = argv[++i];
            } else if (std::wcscmp(argv[i], L"--language") == 0 && i + 1 < argc) {
                const auto selected = retailLanguage(std::filesystem::path(argv[++i]).u8string());
                if (!selected) throw std::runtime_error("Unknown retail language.");
                language = *selected;
            } else if (std::wcscmp(argv[i], L"--save-dir") == 0 && i + 1 < argc && saves.empty()) {
                saves = argv[++i];
            } else {
                throw std::runtime_error("Expected --language <name>, --save-dir <folder>, --smoke-test, "
                                         "--opening-smoke-test, or --texture <file>.");
            }
        }
        if (openingSmokeTest && !texture.empty())
            throw std::runtime_error("--opening-smoke-test requires the game startup path.");
        if (saves.empty() && !smokeTest && texture.empty()) saves = defaultSaveDirectory();
        ResourceFileSystem assets(argv[1], saves);
        if (!language) {
            try {
                const std::unique_ptr<IFileStream> file(assets.openSavedFile(retailPreferencesFilename));
                if (file) language = readRetailLanguagePreference(*file);
            } catch (const std::runtime_error& error) {
                std::cerr << "Could not load language preference: " << error.what() << '\n';
            }
        }
        const auto version = readRetailConstants(assets, "Version_pycst.bin");
        const auto major = retailInteger(version, "VersionNumber", "Major");
        const auto minor = retailInteger(version, "VersionNumber", "Minor");
        const auto micro = retailInteger(version, "VersionNumber", "Micro");
        std::cout << "West Coast Hustle asset version " << major << '.' << minor << '.' << micro << ".\n";
        std::optional<SplashFrames> splash;
        Image preview{};
        if (texture.empty()) splash = readSplash(assets);
        else preview = readPreview(assets, texture);
        auto& image = splash ? splash->show : preview;
        if (!texture.empty()) {
            std::cout << "Decoded a texture (" << image.width << " x " << image.height << ").\n";
            compositePreview(image);
        }
        Win32TouchInput touch(static_cast<short>(image.width), static_cast<short>(image.height));
        std::unique_ptr<std::remove_pointer_t<HWND>, decltype(&DestroyWindow)> window(createWindow(image, touch),
                                                                                     DestroyWindow);
        if (texture.empty()) {
            const auto logo = readStartupLogo(assets, image.width, image.height);
            const auto resourceRoot = std::filesystem::u8path(assets.getResourcesPath());
            IntroMovie movie(resourceRoot / "intro.mp4");
            presentStartup(window.get(), logo, movie, *splash, touch, assets, smokeTest,
                           language.value_or(RetailLanguage::english), openingSmokeTest);
        } else {
            presentImage(window.get(), image, smokeTest);
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
