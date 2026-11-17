#include "menu_font.h"
#include "menu_raster.h"

#include "FileSystemBase.h"
#include "IFileStream.h"
#include "gameswf/gameswf_font.h"

#include <windows.h>
#include <algorithm>

namespace {
constexpr char fontName[] = "GROBOLDpro";

HANDLE loadFont(FileSystemBase& assets, const char* path, unsigned limit)
{
    const std::unique_ptr<IFileStream> file(assets.openResource(path));
    if (!file || !file->size() || file->size() > limit) return nullptr;
    std::vector<unsigned char> bytes(static_cast<size_t>(file->size()));
    if (file->read(bytes.data(), bytes.size()) != bytes.size()) return nullptr;
    DWORD faces = 0;
    return AddFontMemResourceEx(bytes.data(), static_cast<DWORD>(bytes.size()), nullptr, &faces);
}

class MenuFont final : public gameswf::glyph_provider {
public:
    explicit MenuFont(FileSystemBase& assets) : assets(assets)
    {
        resource = loadFont(assets, "font02.ttf", 1024 * 1024);
        if (resource) dc = CreateCompatibleDC(nullptr);
    }

    ~MenuFont() override
    {
        if (dc) DeleteDC(dc);
        if (fallback) RemoveFontMemResourceEx(fallback);
        if (resource) RemoveFontMemResourceEx(resource);
    }

    bool ready() const { return dc != nullptr; }

    gameswf::bitmap_info* get_char_image(gameswf::character_def*, Uint16 code, const tu_string& name,
                                       bool bold, bool italic, int size, gameswf::rect* bounds,
                                       float* advance) override
    {
        const bool grobold = name == fontName || name == "GROBOLDpro Regular";
        const bool arial = name == "Arial" || name == "Arial Regular";
        if ((!grobold && !arial) || size <= 0 || size > 96)
            return nullptr;
        auto* glyph = rasterGlyph(code, bold, italic, size, bounds, advance, arial ? L"Arial" : L"GROBOLDpro");
        if (glyph) return glyph;
        if (!triedFallback) {
            triedFallback = true;
            fallback = loadFont(assets, "fontall.ttf", 32 * 1024 * 1024);
        }
        return fallback ? rasterGlyph(code, bold, italic, size, bounds, advance, L"unifont") : nullptr;
    }

private:
    gameswf::bitmap_info* rasterGlyph(Uint16 code, bool bold, bool italic, int size,
                                      gameswf::rect* bounds, float* advance, const wchar_t* family)
    {
        const auto face = CreateFontW(-size, 0, 0, 0, bold ? FW_BOLD : FW_NORMAL, italic, FALSE, FALSE,
                                     DEFAULT_CHARSET, OUT_TT_ONLY_PRECIS, CLIP_DEFAULT_PRECIS,
                                     ANTIALIASED_QUALITY, DEFAULT_PITCH, family);
        if (!face) return nullptr;
        const auto previous = SelectObject(dc, face);
        if (!previous || previous == HGDI_ERROR) {
            DeleteObject(face);
            return nullptr;
        }

        GLYPHMETRICS metrics{};
        const MAT2 identity{{0, 1}, {}, {}, {0, 1}};
        const wchar_t text = code;
        WORD index = 0xffff;
        const auto mapped = GetGlyphIndicesW(dc, &text, 1, &index, GGI_MARK_NONEXISTING_GLYPHS);
        const auto format = GGO_GRAY8_BITMAP | GGO_GLYPH_INDEX;
        const DWORD bytes = mapped != GDI_ERROR && index != 0xffff
            ? GetGlyphOutlineW(dc, index, format, &metrics, 0, nullptr, &identity) : GDI_ERROR;
        std::vector<unsigned char> gray;
        bool valid = bytes != GDI_ERROR;
        if (valid && bytes != 0) {
            gray.resize(bytes);
            valid = GetGlyphOutlineW(dc, index, format, &metrics, bytes, gray.data(),
                                     &identity) == bytes;
        }
        SelectObject(dc, previous);
        DeleteObject(face);
        if (!valid) return nullptr;

        const auto width = std::max(1u, metrics.gmBlackBoxX);
        const auto height = std::max(1u, metrics.gmBlackBoxY);
        Image image{width, height, std::vector<uint32_t>(size_t(width) * height)};
        const size_t pitch = (size_t(metrics.gmBlackBoxX) + 3) & ~size_t(3);
        if (!gray.empty()) {
            for (size_t y = 0; y < height; ++y) {
                for (size_t x = 0; x < width; ++x) {
                    const uint32_t alpha = gray[y * pitch + x] * 255u / 64u;
                    image.bgra[y * width + x] = alpha << 24 | 0xffffffu;
                }
            }
        }
        // GameSWF stores the baseline offset and sampled extent as texture coordinates.
        if (bounds) {
            bounds->m_x_min = -float(metrics.gmptGlyphOrigin.x) / width;
            bounds->m_y_min = float(metrics.gmptGlyphOrigin.y) / height;
            bounds->m_x_max = bounds->m_y_max = 1;
        }
        if (advance) *advance = float(metrics.gmCellIncX) * 1024 / size;
        return makeMenuBitmap(std::move(image));
    }

    FileSystemBase& assets;
    HANDLE resource = nullptr;
    HANDLE fallback = nullptr;
    HDC dc = nullptr;
    bool triedFallback = false;
};
}

void installMenuFont(FileSystemBase& assets)
{
    auto provider = std::make_unique<MenuFont>(assets);
    if (provider->ready()) gameswf::set_glyph_provider(provider.release());
}

RetailCaptionFont::RetailCaptionFont(FileSystemBase& assets, RetailLanguage language)
    : face(language == RetailLanguage::japanese ? L"unifont" : L"GROBOLDpro"),
      resource(language == RetailLanguage::japanese
          ? loadFont(assets, "fontall.ttf", 32 * 1024 * 1024)
          : loadFont(assets, "font02.ttf", 1024 * 1024))
{
}

RetailCaptionFont::~RetailCaptionFont()
{
    if (resource) RemoveFontMemResourceEx(resource);
}

gameswf::font* load_opengangstar_font(gameswf::player* owner)
{
    if (!gameswf::get_glyph_provider()) return nullptr;
    auto* face = new gameswf::font(owner);
    face->set_name(fontName);
    return face;
}
