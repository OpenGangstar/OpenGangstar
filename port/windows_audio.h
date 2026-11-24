#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <mmsystem.h>

#include <stdexcept>
#include <string>

inline void checkWaveOutput(MMRESULT result, const char* action)
{
    if (result != MMSYSERR_NOERROR)
        throw std::runtime_error(std::string(action) + " failed with MMRESULT " + std::to_string(result));
}
