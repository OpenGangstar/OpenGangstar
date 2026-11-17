#include "win32_touch.h"

#include <windowsx.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace {
std::array<float, 2> normalizedStick(float x, float y, float deadZone)
{
    const auto magnitude = std::hypot(x, y);
    if (magnitude <= deadZone) return {};
    const auto factor = (std::min(magnitude, 32767.f) - deadZone) / ((32767.f - deadZone) * magnitude);
    return {x * factor, y * factor};
}
}

// Native versions of the three touch operations that still use ARM assembly.
void TouchScreenBase::touchBegan(const Point2D<short>& point, long id)
{
    auto& slot = points[id];
    if (!slot.active) {
        slot.first = point;
        slot.swipeDirection = 0;
        slot.numPoints = 1;
        slot.previous = point;
    } else {
        slot.previous = slot.current;
    }
    slot.current = point;
    slot.hasPoints = true;
    slot.active = true;
    slot.lastTouchTime = currentTime;
    slotCount = std::max(slotCount, static_cast<int>(id + 1));
}

Point2D<short> TouchScreenBase::getTouchDisplacement(long id) const
{
    const auto& slot = points[id];
    if (!slot.active || !slot.hasPoints || (slot.current.x == slot.first.x && slot.current.y == slot.first.y))
        return {};
    return {static_cast<short>(slot.current.x - slot.previous.x),
            static_cast<short>(slot.current.y - slot.previous.y)};
}

TouchIDList TouchScreenBase::getTouchIDList() const
{
    TouchIDList list;
    for (long i = 0; i < static_cast<long>(list.ids.size()); ++i) {
        if (points[i].active)
            list.ids[list.count++] = i;
    }
    return list;
}

Win32TouchInput::Win32TouchInput(short width, short height) : width(width), height(height)
{
}

bool Win32TouchInput::takePress()
{
    const bool observed = pressed;
    pressed = false;
    return observed;
}

bool Win32TouchInput::takeCutsceneSkip()
{
    return std::exchange(cutsceneSkip, false);
}

bool Win32TouchInput::takeVehiclePress()
{
    const bool observed = vehiclePressed;
    vehiclePressed = false;
    return observed;
}

bool Win32TouchInput::takeTargetLockPress()
{
    return std::exchange(targetLockPressed, false);
}

bool Win32TouchInput::takePunchPress()
{
    return std::exchange(punchPressed, false);
}

int Win32TouchInput::takeRadioStep()
{
    const auto step = radioStep;
    radioStep = 0;
    return step;
}

std::array<float, 2> Win32TouchInput::takeCameraDrag()
{
    const auto mouse = std::exchange(cameraDrag, {});
    if (cameraDragging || mouse != std::array<float, 2>{}) return mouse;
    const auto stick = normalizedStick(gamepad.sThumbRX, gamepad.sThumbRY, XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE);
    return {stick[0] * width, -stick[1] * height};
}

bool Win32TouchInput::takeCameraRecenter()
{
    return std::exchange(cameraRecenter, false);
}

std::vector<ULONGLONG> Win32TouchInput::takeCameraTaps()
{
    return std::exchange(cameraTaps, {});
}

void Win32TouchInput::startMenuPointer()
{
    pointerEvents.reserve(16);
    menuPointer = true;
}

MenuNavigation Win32TouchInput::takeMenuNavigation()
{
    return std::exchange(navigation, {});
}

void Win32TouchInput::recordPointer(MenuPointerEvent event)
{
    if (!menuPointer) return;
    if (event.motion && !pointerEvents.empty() && pointerEvents.back().motion)
        pointerEvents.back() = event;
    else
        pointerEvents.push_back(event);
}

void Win32TouchInput::pollGamepad(HWND window)
{
    XINPUT_STATE state{};
    if (GetForegroundWindow() == window && XInputGetState(0, &state) == ERROR_SUCCESS)
        updateGamepad(state.Gamepad);
    else
        gamepad = {};
}

void Win32TouchInput::updateGamepad(const XINPUT_GAMEPAD& state)
{
    const auto pressed = state.wButtons & ~gamepad.wButtons;
    if (menuPointer) {
        if (pressed & XINPUT_GAMEPAD_DPAD_UP) --navigation.step;
        if (pressed & XINPUT_GAMEPAD_DPAD_DOWN) ++navigation.step;
        if (pressed & XINPUT_GAMEPAD_DPAD_LEFT) --navigation.horizontal;
        if (pressed & XINPUT_GAMEPAD_DPAD_RIGHT) ++navigation.horizontal;
        if (pressed & XINPUT_GAMEPAD_A) navigation.accept = true;
        if (pressed & XINPUT_GAMEPAD_B) navigation.back = true;
    } else {
        if (pressed & XINPUT_GAMEPAD_A) vehiclePressed = true;
        if (pressed & XINPUT_GAMEPAD_X) targetLockPressed = true;
        if (pressed & XINPUT_GAMEPAD_Y) punchPressed = true;
        if (pressed & XINPUT_GAMEPAD_B) cutsceneSkip = true;
        if (pressed & XINPUT_GAMEPAD_RIGHT_THUMB) cameraRecenter = true;
        if (pressed & XINPUT_GAMEPAD_DPAD_LEFT) --radioStep;
        if (pressed & XINPUT_GAMEPAD_DPAD_RIGHT) ++radioStep;
    }
    gamepad = state;
}

std::array<float, 2> Win32TouchInput::movement(bool driving) const
{
    const auto axis = [&](unsigned positive, unsigned positiveArrow, unsigned negative, unsigned negativeArrow) {
        return float(keys[positive] || keys[positiveArrow]) - float(keys[negative] || keys[negativeArrow]);
    };
    const std::array keyboard{axis('D', VK_RIGHT, 'A', VK_LEFT), axis('W', VK_UP, 'S', VK_DOWN)};
    if (keyboard[0] != 0 || keyboard[1] != 0) return keyboard;

    const auto stick = normalizedStick(gamepad.sThumbLX, gamepad.sThumbLY, XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE);
    const auto trigger = [](BYTE value) {
        constexpr float threshold = XINPUT_GAMEPAD_TRIGGER_THRESHOLD;
        return std::max(0.f, (value - threshold) / (255.f - threshold));
    };
    const auto accelerate = trigger(gamepad.bRightTrigger), brake = trigger(gamepad.bLeftTrigger);
    const auto vertical = driving && (accelerate > 0 || brake > 0) ? accelerate - brake : stick[1];
    return {stick[0], vertical};
}

bool Win32TouchInput::message(HWND window, UINT type, WPARAM key, LPARAM coordinates)
{
    if ((type == WM_KEYDOWN || type == WM_KEYUP) && key < keys.size()) {
        if (type == WM_KEYDOWN && !keys[key]) {
            if (menuPointer) {
                if (key == VK_UP) --navigation.step;
                if (key == VK_DOWN || key == VK_TAB) ++navigation.step;
                if (key == VK_LEFT) --navigation.horizontal;
                if (key == VK_RIGHT) ++navigation.horizontal;
                if (key == VK_RETURN || key == VK_SPACE) navigation.accept = true;
                if (key == VK_ESCAPE) navigation.back = true;
            } else if (key == VK_ESCAPE) cutsceneSkip = true;
            if (key == 'E') vehiclePressed = true;
            if (!menuPointer && key == 'Q') targetLockPressed = true;
            if (!menuPointer && key == 'F') punchPressed = true;
            if (key == VK_OEM_4) radioStep = -1;
            if (key == VK_OEM_6) radioStep = 1;
        }
        keys[key] = type == WM_KEYDOWN;
        return false;
    }
    if (type == WM_KILLFOCUS) {
        keys.fill(false);
        gamepad = {};
        navigation = {};
        cutsceneSkip = false;
        vehiclePressed = false;
        targetLockPressed = false;
        punchPressed = false;
        radioStep = 0;
        cameraTaps.clear();
        cameraRecenter = false;
        cameraDrag = {};
    }
    if (type == WM_MOUSELEAVE) {
        trackingLeave = false;
        if (mouseSlot < 0) recordPointer({-1, -1, false, true});
        return false;
    }
    if (type == WM_CAPTURECHANGED || type == WM_CANCELMODE || type == WM_KILLFOCUS) {
        if (cameraDragging) {
            cameraDragging = false;
            cameraDrag = {};
            if (type != WM_CAPTURECHANGED && GetCapture() == window)
                ReleaseCapture();
        }
        if (mouseSlot >= 0) {
            touchCancelled({}, mouseSlot);
            mouseSlot = -1;
            recordPointer({-1, -1, false, false});
            if (type != WM_CAPTURECHANGED && GetCapture() == window)
                ReleaseCapture();
        } else if (type == WM_KILLFOCUS) {
            recordPointer({-1, -1, false, true});
        }
        return false;
    }
    if (type == WM_RBUTTONDOWN && !menuPointer && mouseSlot < 0 && isEnabled()) {
        cameraDragPosition = {GET_X_LPARAM(coordinates), GET_Y_LPARAM(coordinates)};
        cameraDragging = true;
        cameraDownMs = GetTickCount64();
        cameraTravel = {};
        SetCapture(window);
        return true;
    }
    if (cameraDragging) {
        if (type == WM_MOUSEMOVE || type == WM_RBUTTONUP) {
            const POINT point{GET_X_LPARAM(coordinates), GET_Y_LPARAM(coordinates)};
            RECT client{};
            if (GetClientRect(window, &client) && client.right > 0 && client.bottom > 0) {
                const std::array movement{
                    float(point.x - cameraDragPosition.x) * 480.f / float(client.right),
                    float(point.y - cameraDragPosition.y) * 320.f / float(client.bottom)};
                for (size_t axis = 0; axis < movement.size(); ++axis) {
                    cameraDrag[axis] += movement[axis];
                    cameraTravel[axis] += std::abs(movement[axis]);
                }
            }
            cameraDragPosition = point;
            if (type == WM_RBUTTONUP) {
                const auto releasedMs = GetTickCount64();
                // CameraManager treats short releases with at most nine pixels per axis as taps.
                if (releasedMs - cameraDownMs < 500 && cameraTravel[0] <= 9 && cameraTravel[1] <= 9)
                    cameraTaps.push_back(releasedMs);
                cameraDragging = false;
                if (GetCapture() == window) ReleaseCapture();
            }
            return true;
        }
        if (type == WM_LBUTTONDOWN || type == WM_LBUTTONUP) return true;
    }
    if (type != WM_LBUTTONDOWN && type != WM_MOUSEMOVE && type != WM_LBUTTONUP)
        return false;
    if (type == WM_LBUTTONUP && mouseSlot < 0)
        return false;
    if (type != WM_LBUTTONUP && (!isEnabled() || (type == WM_MOUSEMOVE && mouseSlot < 0 && !menuPointer)))
        return false;

    RECT client{};
    const bool valid = GetClientRect(window, &client) && client.right > 0 && client.bottom > 0;
    if (!valid && type != WM_LBUTTONUP)
        return false;
    const auto toTouch = [](int coordinate, short bound, LONG extent) {
        if (coordinate < 0) return short(-1);
        return static_cast<short>(std::clamp(coordinate * int(bound) / extent,
                                             LONG(std::numeric_limits<short>::min()),
                                             LONG(std::numeric_limits<short>::max())));
    };
    const Point2D<short> point(valid ? toTouch(GET_X_LPARAM(coordinates), width, client.right) : short(-1),
                               valid ? toTouch(GET_Y_LPARAM(coordinates), height, client.bottom) : short(-1));
    if (menuPointer && type == WM_MOUSEMOVE && !trackingLeave) {
        TRACKMOUSEEVENT tracking{sizeof tracking, TME_LEAVE, window, 0};
        trackingLeave = TrackMouseEvent(&tracking) != FALSE;
    }
    update(double(GetTickCount64()) / 1000);
    if (type == WM_LBUTTONDOWN) {
        constexpr long mouseId = 1;
        mouseSlot = getID(mouseId);
        if (mouseSlot < 0) return false;
        SetCapture(window);
        touchBegan(point, mouseSlot);
        pressed = point.x >= 0 && point.x < width && point.y >= 0 && point.y < height;
        recordPointer({point.x, point.y, true, false});
    } else if (type == WM_LBUTTONUP) {
        touchEnded(point, mouseSlot);
        mouseSlot = -1;
        recordPointer({point.x, point.y, false, false});
        if (GetCapture() == window) ReleaseCapture();
    } else if (mouseSlot >= 0) {
        touchMoved(point, mouseSlot);
        recordPointer({point.x, point.y, true, true});
    } else {
        recordPointer({point.x, point.y, false, true});
    }
    return true;
}
