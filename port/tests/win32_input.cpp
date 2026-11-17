#include "win32_touch.h"

#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}
}

int main() try {
    Win32TouchInput input(480, 320);
    XINPUT_GAMEPAD state{};
    state.sThumbLX = XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE;
    input.updateGamepad(state);
    require(input.movement() == std::array{0.f, 0.f}, "Stick drift passed the dead zone.");
    state.sThumbLX = 32767;
    state.sThumbLY = 32767;
    state.bRightTrigger = 255;
    input.updateGamepad(state);
    const auto walking = input.movement();
    require(std::abs(std::hypot(walking[0], walking[1]) - 1.f) < 0.001f,
            "Diagonal stick input was not bounded.");
    require(input.movement(true)[1] == 1.f, "Right trigger did not accelerate.");
    state.bLeftTrigger = 255;
    input.updateGamepad(state);
    require(input.movement(true)[1] == 0.f, "Opposing triggers did not cancel.");
    state.bRightTrigger = 0;
    state.sThumbRY = XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE;
    input.updateGamepad(state);
    require(input.takeCameraDrag() == std::array{0.f, 0.f}, "Camera stick drift passed the dead zone.");
    state.sThumbRY = 32767;
    state.wButtons = XINPUT_GAMEPAD_A | XINPUT_GAMEPAD_B | XINPUT_GAMEPAD_DPAD_LEFT | XINPUT_GAMEPAD_RIGHT_THUMB;
    input.updateGamepad(state);
    require(input.movement(true)[1] == -1.f && input.takeVehiclePress() && input.takeCutsceneSkip() &&
            input.takeRadioStep() == -1, "Controller buttons or brake were not mapped.");
    require(input.takeCameraDrag() == std::array{0.f, -320.f} && input.takeCameraRecenter(),
            "Camera stick direction or recenter was not mapped.");
    input.updateGamepad(state);
    require(input.takeCameraDrag() == std::array{0.f, -320.f} && !input.takeCameraRecenter(),
            "Camera motion stopped or recenter repeated while held.");
    require(!input.takeVehiclePress() && !input.takeCutsceneSkip() && input.takeRadioStep() == 0,
            "Held buttons repeated their actions.");
    input.message(nullptr, WM_KEYDOWN, 'W', 0);
    input.message(nullptr, WM_KEYDOWN, 'Q', 0);
    require(input.takeTargetLockPress() && !input.takeTargetLockPress(), "Q did not emit one lock press.");
    input.message(nullptr, WM_KEYDOWN, 'Q', 0);
    require(!input.takeTargetLockPress(), "Held Q repeated the lock press.");
    input.message(nullptr, WM_KEYUP, 'Q', 0);
    require(input.movement(true) == std::array{0.f, 1.f}, "Keyboard input did not retain priority.");
    input.message(nullptr, WM_KILLFOCUS, 0, 0);
    require(input.movement(true) == std::array{0.f, 0.f}, "Focus loss retained movement.");
    input.startMenuPointer();
    state.wButtons = XINPUT_GAMEPAD_DPAD_DOWN | XINPUT_GAMEPAD_DPAD_LEFT | XINPUT_GAMEPAD_A;
    input.updateGamepad(state);
    const auto navigation = input.takeMenuNavigation();
    require(navigation.step == 1 && navigation.horizontal == -1 && navigation.accept && !input.takeVehiclePress(),
            "Controller menu navigation leaked into vehicle input.");
    input.message(nullptr, WM_KEYDOWN, VK_ESCAPE, 0);
    require(input.takeMenuNavigation().back && !input.takeCutsceneSkip(),
            "Menu escape leaked into cutscene input.");
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
