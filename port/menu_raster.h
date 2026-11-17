#pragma once

#include "texture.h"

#include <memory>

namespace gameswf {
struct bitmap_info;
struct root;
}

gameswf::bitmap_info* makeMenuBitmap(Image image);

class MenuRaster {
public:
    MenuRaster();
    ~MenuRaster();
    Image draw(gameswf::root& movie, const Image& background);

private:
    struct Renderer;
    std::unique_ptr<Renderer> renderer;
};
