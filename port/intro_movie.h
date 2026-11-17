#pragma once

#include "texture.h"

#include <filesystem>
#include <memory>
#include <optional>

struct MovieFrame {
    Image image;
    double timestampMs;
};

class IntroMovie {
public:
    explicit IntroMovie(const std::filesystem::path& path);
    ~IntroMovie();
    uint32_t width() const;
    uint32_t height() const;
    double durationMs() const;
    std::optional<MovieFrame> next();
    void startAudio();
    std::optional<double> audioPositionMs();
    void stopAudio();

private:
    struct Decoder;
    std::unique_ptr<Decoder> decoder;
};
