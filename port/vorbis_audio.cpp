#include "vorbis_audio.h"
#include "IFileStream.h"

#define OV_EXCLUDE_STATIC_CALLBACKS
#include <vorbis/vorbisfile.h>

#include <algorithm>
#include <climits>
#include <cstdio>
#include <stdexcept>
#include <string>

namespace {
size_t readFile(void* destination, size_t size, size_t count, void* context)
{
    auto& file = *static_cast<IFileStream*>(context);
    if (!size) return 0;
    const auto remaining = (file.size() - file.tell()) / size;
    const auto items = std::min<unsigned long long>(remaining, count);
    return static_cast<size_t>(file.read(static_cast<unsigned char*>(destination), size * items) / size);
}

int seekFile(void* context, ogg_int64_t offset, int whence)
{
    auto& file = *static_cast<IFileStream*>(context);
    const auto length = static_cast<ogg_int64_t>(file.size());
    ogg_int64_t origin = 0;
    if (whence == SEEK_CUR) origin = static_cast<ogg_int64_t>(file.tell());
    else if (whence == SEEK_END) origin = length;
    else if (whence != SEEK_SET) return -1;
    if (offset < -origin || offset > length - origin) return -1;
    const auto position = static_cast<unsigned long long>(origin + offset);
    file.seek(position);
    return file.tell() == position ? 0 : -1;
}

long tellFile(void* context)
{
    return static_cast<long>(static_cast<IFileStream*>(context)->tell());
}
}

struct VorbisStream::Decoder {
    OggVorbis_File state{};
    bool opened = false;
    ~Decoder() { if (opened) ov_clear(&state); }
};

VorbisStream::VorbisStream(IFileStream& file)
{
    if (!file.size()) return;
    if (file.size() > LONG_MAX) throw std::runtime_error("Ogg file exceeds the decoder's seek range.");
    file.seek(0);
    if (file.tell() != 0) throw std::runtime_error("Could not rewind the Ogg file.");
    constexpr ov_callbacks callbacks{readFile, seekFile, nullptr, tellFile};
    decoder = std::make_unique<Decoder>();
    auto& state = decoder->state;
    const auto result = ov_open_callbacks(&file, &state, nullptr, 0, callbacks);
    if (result != 0) throw std::runtime_error("Could not open Ogg Vorbis stream (" + std::to_string(result) + ").");
    decoder->opened = true;

    const auto count = ov_streams(&state);
    const auto* info = ov_info(&state, 0);
    if (!info || count < 1 || info->rate <= 0 || info->channels <= 0 || info->channels > 8)
        throw std::runtime_error("Invalid Vorbis sample format.");
    rate = static_cast<unsigned>(info->rate);
    channelCount = static_cast<unsigned>(info->channels);
    for (long i = 1; i < count; ++i) {
        const auto* link = ov_info(&state, static_cast<int>(i));
        if (!link || link->rate != info->rate || link->channels != info->channels)
            throw std::runtime_error("Vorbis links change sample format.");
    }
    const auto total = ov_pcm_total(&state, -1);
    if (total < 0) throw std::runtime_error("Could not read the Vorbis frame count.");
    frames = static_cast<uint64_t>(total);
}

VorbisStream::~VorbisStream() = default;

size_t VorbisStream::readFrames(int16_t* destination, size_t capacity)
{
    if (!decoder || !capacity) return 0;
    const size_t frameBytes = channelCount * sizeof(int16_t);
    const int bytes = static_cast<int>(std::min(capacity, INT_MAX / frameBytes) * frameBytes);
    int link = 0;
    const auto decoded = ov_read(&decoder->state, reinterpret_cast<char*>(destination), bytes, 0, 2, 1, &link);
    if (decoded < 0 || decoded % frameBytes != 0)
        throw std::runtime_error("Invalid Vorbis audio packet (" + std::to_string(decoded) + ").");
    return static_cast<size_t>(decoded) / frameBytes;
}
