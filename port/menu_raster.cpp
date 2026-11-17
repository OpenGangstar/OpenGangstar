#include "menu_raster.h"

#include "base/image.h"
#include "gameswf/gameswf_root.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

namespace {
using namespace gameswf;

class Bitmap final : public bitmap_info {
public:
    explicit Bitmap(Image pixels) : image(std::move(pixels))
    {
        for (auto& pixel : image.bgra)
            pixel = (pixel & 0xff00ff00u) | ((pixel & 0xffu) << 16) | ((pixel >> 16) & 0xffu);
    }
    int get_width() const override { return static_cast<int>(image.width); }
    int get_height() const override { return static_cast<int>(image.height); }
    int get_bpp() const override { return 4; }
    unsigned char* get_data() const override { return reinterpret_cast<unsigned char*>(image.bgra.data()); }

private:
    mutable Image image;
};

Bitmap* copyBitmap(int width, int height, const unsigned char* bytes, int pitch, unsigned channels)
{
    if (width <= 0 || height <= 0 || width > 4096 || height > 4096) return nullptr;
    Image image{uint32_t(width), uint32_t(height), std::vector<uint32_t>(size_t(width) * height)};
    for (int y = 0; y < height; ++y) {
        const auto* row = bytes + size_t(y) * pitch;
        auto* dst = image.bgra.data() + size_t(y) * width;
        for (int x = 0; x < width; ++x) {
            const auto* pixel = row + size_t(x) * channels;
            if (channels == 1) dst[x] = (uint32_t(pixel[0]) << 24) | 0xffffffu;
            else dst[x] = (uint32_t(channels == 4 ? pixel[3] : 255) << 24) |
                          (uint32_t(pixel[0]) << 16) | (uint32_t(pixel[1]) << 8) | pixel[2];
        }
    }
    return new Bitmap(std::move(image));
}

struct Vertex {
    point screen;
    point uv;
};

float edge(point a, point b, point p)
{
    return (b.m_x - a.m_x) * (p.m_y - a.m_y) - (b.m_y - a.m_y) * (p.m_x - a.m_x);
}

bool topLeft(point a, point b)
{
    return a.m_y > b.m_y || (a.m_y == b.m_y && a.m_x < b.m_x);
}
}

gameswf::bitmap_info* makeMenuBitmap(Image image) { return new Bitmap(std::move(image)); }

struct MenuRaster::Renderer final : gameswf::render_handler {
    struct Fill {
        gameswf::rgba color{255, 255, 255, 255};
        gameswf::bitmap_info* bitmap = nullptr;
        gameswf::matrix uv;
        bitmap_wrap_mode wrap = WRAP_CLAMP;
    };

    Image frame{};
    gameswf::matrix matrix;
    gameswf::cxform transform;
    Fill fill;
    Fill line;
    float lineWidth = 1;
    float left = 0, top = 0, sx = 1, sy = 1;
    std::vector<std::vector<unsigned char>> masks;
    bool writingMask = false;

    gameswf::bitmap_info* create_bitmap_info_empty() override { return new Bitmap({0, 0, {}}); }
    gameswf::bitmap_info* create_bitmap_info_alpha(int w, int h, unsigned char* data) override
    { return copyBitmap(w, h, data, w, 1); }
    gameswf::bitmap_info* create_bitmap_info_rgb(image::rgb* im) override
    { return copyBitmap(im->m_width, im->m_height, im->m_data, im->m_pitch, 3); }
    gameswf::bitmap_info* create_bitmap_info_rgba(image::rgba* im) override
    { return copyBitmap(im->m_width, im->m_height, im->m_data, im->m_pitch, 4); }
    gameswf::video_handler* create_video_handler() override { return nullptr; }

    void begin_display(gameswf::rgba, int, int, int, int, float x0, float x1, float y0, float y1) override
    {
        left = x0;
        top = y0;
        sx = x1 > x0 ? frame.width / (x1 - x0) : 0;
        sy = y1 > y0 ? frame.height / (y1 - y0) : 0;
        masks.clear();
        writingMask = false;
    }
    void end_display() override {}
    void open() override {}
    void set_matrix(const gameswf::matrix& m) override { matrix = m; }
    void set_cxform(const gameswf::cxform& value) override { transform = value; }
    void set_antialiased(bool) override {}
    bool is_visible(const gameswf::rect&) override { return true; }
    bool test_stencil_buffer(const gameswf::rect&, Uint8) override { return true; }
    void begin_submit_mask() override
    {
        masks.emplace_back(frame.bgra.size());
        writingMask = true;
    }
    void end_submit_mask() override { writingMask = false; }
    void disable_mask() override { if (!masks.empty()) masks.pop_back(); }

    void fill_style_disable(int side) override { if (side == 0) fill = {}; }
    void fill_style_color(int side, const gameswf::rgba& color) override
    { if (side == 0) fill = {transform.transform(color)}; }
    void fill_style_bitmap(int side, gameswf::bitmap_info* bitmap, const gameswf::matrix& uv,
                           bitmap_wrap_mode wrap, bitmap_blend_mode) override
    { if (side == 0) fill = {gameswf::rgba(255, 255, 255, 255), bitmap, uv, wrap}; }
    void line_style_disable() override { line.color = gameswf::rgba(0, 0, 0, 0); }
    void line_style_color(gameswf::rgba color) override { line.color = transform.transform(color); }
    void line_style_width(float width) override { lineWidth = width; }

    point screen(const gameswf::matrix& m, point local) const
    {
        point stage;
        m.transform(&stage, local);
        return {(stage.m_x - left) * sx, (stage.m_y - top) * sy};
    }

    void triangles(const void* coords, int count, bool strip)
    {
        const auto* xy = static_cast<const int16_t*>(coords);
        const auto vertex = [&](int i) {
            point local{float(xy[2*i]), float(xy[2*i+1])};
            point uv;
            if (fill.bitmap) fill.uv.transform(&uv, local);
            return Vertex{screen(matrix, local), uv};
        };
        for (int i = 2; i < count; i += strip ? 1 : 3)
            triangle(vertex(i-2), vertex(i-1), vertex(i), fill, true);
    }
    void draw_mesh_strip(const void* coords, int count) override { triangles(coords, count, true); }
    void draw_triangle_list(const void* coords, int count) override { triangles(coords, count, false); }
    void draw_line_strip(const void* coords, int count) override
    {
        const auto* xy = static_cast<const int16_t*>(coords);
        const float half = std::max(.5f, lineWidth * (sx + sy) / 4);
        for (int i = 1; i < count; ++i) {
            const auto a = screen(matrix, {float(xy[2*i-2]), float(xy[2*i-1])});
            const auto b = screen(matrix, {float(xy[2*i]), float(xy[2*i+1])});
            const float dx = b.m_x - a.m_x, dy = b.m_y - a.m_y;
            const float length = std::hypot(dx, dy);
            if (!(length > 0)) continue;
            const float x = -dy * half / length, y = dx * half / length;
            const Vertex p{{a.m_x+x,a.m_y+y},{}}, q{{a.m_x-x,a.m_y-y},{}};
            const Vertex r{{b.m_x+x,b.m_y+y},{}}, s{{b.m_x-x,b.m_y-y},{}};
            triangle(p, q, r, line, false);
            triangle(q, s, r, line, false);
        }
    }

    void draw_bitmap(const gameswf::matrix& m, gameswf::bitmap_info* bitmap, const gameswf::rect& p,
                     const gameswf::rect& tex, gameswf::rgba tint) override
    {
        if (!bitmap) return;
        const float w = float(bitmap->get_width()), h = float(bitmap->get_height());
        const Vertex a{screen(m, {p.m_x_min,p.m_y_min}),{tex.m_x_min*w,tex.m_y_min*h}};
        const Vertex b{screen(m, {p.m_x_max,p.m_y_min}),{tex.m_x_max*w,tex.m_y_min*h}};
        const Vertex c{screen(m, {p.m_x_min,p.m_y_max}),{tex.m_x_min*w,tex.m_y_max*h}};
        const Vertex d{screen(m, {p.m_x_max,p.m_y_max}),{tex.m_x_max*w,tex.m_y_max*h}};
        const Fill glyph{tint, bitmap};
        triangle(a, b, c, glyph, false);
        triangle(b, d, c, glyph, false);
    }

    void triangle(Vertex a, Vertex b, Vertex c, const Fill& style, bool colorTransform)
    {
        float area = edge(a.screen, b.screen, c.screen);
        if (!(std::isfinite)(area) || area == 0) return;
        if (area < 0) {
            std::swap(b, c);
            area = -area;
        }
        const auto bounds = [](float p, float q, float r, uint32_t limit) {
            return std::array<int, 2>{
                int(std::clamp(std::floor(std::min({p,q,r})),0.f,float(limit))),
                int(std::clamp(std::ceil(std::max({p,q,r})),0.f,float(limit)))};
        };
        const auto x = bounds(a.screen.m_x,b.screen.m_x,c.screen.m_x,frame.width);
        const auto y = bounds(a.screen.m_y,b.screen.m_y,c.screen.m_y,frame.height);
        const bool ea = topLeft(b.screen,c.screen), eb = topLeft(c.screen,a.screen), ec = topLeft(a.screen,b.screen);
        for (int py = y[0]; py < y[1]; ++py) {
            for (int px = x[0]; px < x[1]; ++px) {
                const point p(px + .5f, py + .5f);
                const float wa = edge(b.screen,c.screen,p), wb = edge(c.screen,a.screen,p), wc = edge(a.screen,b.screen,p);
                if (wa < 0 || wb < 0 || wc < 0 || (!wa && !ea) || (!wb && !eb) || (!wc && !ec)) continue;
                const size_t index = size_t(py) * frame.width + px;
                if (!masks.empty()) {
                    if (writingMask) { masks.back()[index] = 1; continue; }
                    if (std::any_of(masks.begin(), masks.end(), [index](const auto& mask) { return !mask[index]; }))
                        continue;
                }

                auto color = style.color;
                if (style.bitmap) {
                    const int w = style.bitmap->get_width(), h = style.bitmap->get_height();
                    const auto* data = style.bitmap->get_data();
                    if (!data || w <= 0 || h <= 0 || style.bitmap->get_bpp() != 4) continue;
                    const auto sample = [&](float v, int extent) {
                        if (!(std::isfinite)(v)) return 0;
                        if (style.wrap == WRAP_REPEAT) {
                            v = std::fmod(v, float(extent));
                            if (v < 0) v += extent;
                        }
                        return int(std::clamp(v, 0.f, float(extent-1)));
                    };
                    const auto u = sample((a.uv.m_x*wa+b.uv.m_x*wb+c.uv.m_x*wc)/area,w);
                    const auto v = sample((a.uv.m_y*wa+b.uv.m_y*wb+c.uv.m_y*wc)/area,h);
                    const auto* src = data + (size_t(v)*w+u)*4;
                    color = gameswf::rgba(src[0]*color.m_r/255,src[1]*color.m_g/255,
                                          src[2]*color.m_b/255,src[3]*color.m_a/255);
                    if (colorTransform) color = transform.transform(color);
                }
                auto& target = frame.bgra[index];
                const auto blend = [&](uint32_t channel, unsigned shift) {
                    return ((channel*color.m_a + ((target>>shift)&255)*(255-color.m_a) + 127)/255)<<shift;
                };
                target = 0xff000000u | blend(color.m_r,16) | blend(color.m_g,8) | blend(color.m_b,0);
            }
        }
    }
};

MenuRaster::MenuRaster() : renderer(std::make_unique<Renderer>()) { gameswf::set_render_handler(renderer.get()); }
MenuRaster::~MenuRaster() { gameswf::set_render_handler(nullptr); }

Image MenuRaster::draw(gameswf::root& movie, const Image& background)
{
    renderer->frame = background;
    movie.display();
    return std::move(renderer->frame);
}
