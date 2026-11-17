#pragma once

#include "TouchScreenBase.h"

#include <windows.h>
#include <xinput.h>

#include <array>
#include <vector>

struct MenuPointerEvent {
    short x;
    short y;
    bool down;
    bool motion;
};

struct MenuNavigation {
    int step = 0;
    bool accept = false;
    bool back = false;
    int horizontal = 0;
};

struct TouchIDList {
    std::array<long, 8> ids{};
    unsigned count = 0;
};

class Win32TouchInput final : public TouchScreenBase {
public:
    Win32TouchInput(short width, short height);

    short getLeftBound() const override { return 0; }
    short getTopBound() const override { return 0; }
    short getRightBound() const override { return width - 1; }
    short getBottomBound() const override { return height - 1; }
    bool message(HWND window, UINT type, WPARAM key, LPARAM coordinates);
    bool takePress();
    bool takeCutsceneSkip();
    bool takeVehiclePress();
    bool takeTargetLockPress();
    bool takePunchPress();
    int takeRadioStep();
    std::array<float, 2> takeCameraDrag();
    bool takeCameraRecenter();
    std::vector<ULONGLONG> takeCameraTaps();
    std::array<float, 2> movement(bool driving = false) const;
    void pollGamepad(HWND window);
    void updateGamepad(const XINPUT_GAMEPAD& state);
    void startMenuPointer();
    void stopMenuPointer() { menuPointer = false; pointerEvents.clear(); navigation = {}; }
    MenuNavigation takeMenuNavigation();
    const std::vector<MenuPointerEvent>& menuPointerEvents() const { return pointerEvents; }
    void clearMenuPointerEvents() { pointerEvents.clear(); }

private:
    void recordPointer(MenuPointerEvent event);
    short width;
    short height;
    long mouseSlot = -1;
    bool pressed = false;
    bool cutsceneSkip = false;
    bool vehiclePressed = false;
    bool targetLockPressed = false;
    bool punchPressed = false;
    int radioStep = 0;
    bool menuPointer = false;
    MenuNavigation navigation;
    bool cameraDragging = false;
    bool cameraRecenter = false;
    POINT cameraDragPosition{};
    ULONGLONG cameraDownMs = 0;
    std::array<float, 2> cameraTravel{};
    std::vector<ULONGLONG> cameraTaps;
    std::array<float, 2> cameraDrag{};
    bool trackingLeave = false;
    std::vector<MenuPointerEvent> pointerEvents;
    std::array<bool, 256> keys{};
    XINPUT_GAMEPAD gamepad{};
};
