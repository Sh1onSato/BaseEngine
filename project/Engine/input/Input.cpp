#include "Input.h"
#include <cassert>

void Input::Initialize(WinApp* winApp) {
    this->winApp = winApp;
    // DirectInputの初期化
    hr = DirectInput8Create(
        winApp->GetHinstance(),
        DIRECTINPUT_VERSION,
        IID_IDirectInput8,
        reinterpret_cast<void**>(directInput_.GetAddressOf()),
        nullptr
    );
    assert(SUCCEEDED(hr));

    // キーボードデバイスの生成
    hr = directInput_->CreateDevice(GUID_SysKeyboard, keyboard.GetAddressOf(), NULL);
    assert(SUCCEEDED(hr));
    hr = keyboard->SetDataFormat(&c_dfDIKeyboard);
    assert(SUCCEEDED(hr));
    hr = keyboard->SetCooperativeLevel(winApp->GetHwnd(), DISCL_FOREGROUND | DISCL_NONEXCLUSIVE | DISCL_NOWINKEY);
    assert(SUCCEEDED(hr));

    // マウスデバイスの生成
    hr = directInput_->CreateDevice(GUID_SysMouse, mouse.GetAddressOf(), NULL);
    assert(SUCCEEDED(hr));
    hr = mouse->SetDataFormat(&c_dfDIMouse2);
    assert(SUCCEEDED(hr));
    hr = mouse->SetCooperativeLevel(winApp->GetHwnd(), DISCL_FOREGROUND | DISCL_NONEXCLUSIVE);
    assert(SUCCEEDED(hr));
}

void Input::Update() {
    // --- キーボード ---
    memcpy(keyPre, key, sizeof(key));
    keyboard->Acquire();
    keyboard->GetDeviceState(sizeof(key), key);

    // --- マウス ---
    mouseStatePre = mouseState;
    mouse->Acquire();
    mouse->GetDeviceState(sizeof(DIMOUSESTATE2), &mouseState);

    // --- コントローラー (XInput) ---
    xInputStatePre = xInputState;
    DWORD dwResult = XInputGetState(0, &xInputState);
    if (dwResult == ERROR_SUCCESS) {
        isControllerConnected = true;
    } else {
        isControllerConnected = false;
    }
}

bool Input::PushKey(BYTE keyNumber) {
    return key[keyNumber] != 0;
}

bool Input::TriggerKey(BYTE keyNumber) {
    return !keyPre[keyNumber] && key[keyNumber];
}

Input::MouseMove Input::GetMouseMove() const {
    MouseMove result;
    result.lX = mouseState.lX;
    result.lY = mouseState.lY;
    result.lZ = mouseState.lZ;
    return result;
}

bool Input::PushMouseButton(int buttonNumber) const {
    return (mouseState.rgbButtons[buttonNumber] & 0x80) != 0;
}

bool Input::TriggerMouseButton(int buttonNumber) const {
    bool current = (mouseState.rgbButtons[buttonNumber] & 0x80) != 0;
    bool pre = (mouseStatePre.rgbButtons[buttonNumber] & 0x80) != 0;
    return !pre && current;
}

bool Input::GetJoystickState(int stickNo, JoystickState& out) const {
    if (stickNo == 0 && isControllerConnected) {
        out.state = xInputState;
        out.preState = xInputStatePre;
        return true;
    }
    return false;
}
