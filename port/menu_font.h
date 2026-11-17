#pragma once

#include "retail_text.h"

class FileSystemBase;
void installMenuFont(FileSystemBase& assets);

class RetailCaptionFont {
public:
    RetailCaptionFont(FileSystemBase& assets, RetailLanguage language);
    ~RetailCaptionFont();
    RetailCaptionFont(const RetailCaptionFont&) = delete;
    RetailCaptionFont& operator=(const RetailCaptionFont&) = delete;

    const wchar_t* family() const { return resource ? face : nullptr; }

private:
    const wchar_t* face;
    void* resource;
};
