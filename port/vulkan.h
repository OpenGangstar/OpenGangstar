#pragma once

#include "texture.h"
#include "retail_text.h"

#include <windows.h>

struct StartupLogo;
struct SplashFrames;
class IntroMovie;
class Win32TouchInput;
class ResourceFileSystem;

// Keep the swapchain alive until the window closes.
void presentImage(HWND window, const Image& image, bool smokeTest);
void presentStartup(HWND window, const StartupLogo& logo, IntroMovie& movie, const SplashFrames& splash,
                    Win32TouchInput& touch, ResourceFileSystem& assets, bool smokeTest,
                    RetailLanguage language = RetailLanguage::english, bool openingSmokeTest = false);
