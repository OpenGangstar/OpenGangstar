#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

class IFileStream;

class VorbisStream {
public:
    // The file must outlive the decoder. Empty retail slots have zero frames.
    explicit VorbisStream(IFileStream& file);
    ~VorbisStream();

    unsigned sampleRate() const { return rate; }
    unsigned channels() const { return channelCount; }
    uint64_t frameCount() const { return frames; }
    size_t readFrames(int16_t* destination, size_t capacity);

private:
    struct Decoder;
    std::unique_ptr<Decoder> decoder;
    unsigned rate = 0;
    unsigned channelCount = 0;
    uint64_t frames = 0;
};
