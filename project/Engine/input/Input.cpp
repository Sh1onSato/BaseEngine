#include "Input.h"
#include "GameTime.h"
#include "Logger.h"
#include "Screen.h"
#include <algorithm>
#include <cassert>
#include <cmath>

Input::~Input() {
    // 振動したまま終了するとコントローラーが震え続けるので止めておく
    for (int i = 0; i < kMaxPads; ++i) {
        if (pads[i].isConnected) {
            XINPUT_VIBRATION vibration{};
            XInputSetState(i, &vibration);
        }
    }
}

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
    Logger::CheckHResult(hr, "Input: DirectInput8Create");

    // キーボードデバイスの生成
    hr = directInput_->CreateDevice(GUID_SysKeyboard, keyboard.GetAddressOf(), NULL);
    Logger::CheckHResult(hr, "Input: CreateDevice(Keyboard)");
    hr = keyboard->SetDataFormat(&c_dfDIKeyboard);
    Logger::CheckHResult(hr, "Input: Keyboard SetDataFormat");
    hr = keyboard->SetCooperativeLevel(winApp->GetHwnd(), DISCL_FOREGROUND | DISCL_NONEXCLUSIVE | DISCL_NOWINKEY);
    Logger::CheckHResult(hr, "Input: Keyboard SetCooperativeLevel");

    // マウスデバイスの生成
    hr = directInput_->CreateDevice(GUID_SysMouse, mouse.GetAddressOf(), NULL);
    Logger::CheckHResult(hr, "Input: CreateDevice(Mouse)");
    hr = mouse->SetDataFormat(&c_dfDIMouse2);
    Logger::CheckHResult(hr, "Input: Mouse SetDataFormat");
    hr = mouse->SetCooperativeLevel(winApp->GetHwnd(), DISCL_FOREGROUND | DISCL_NONEXCLUSIVE);
    Logger::CheckHResult(hr, "Input: Mouse SetCooperativeLevel");

    // 起動直後に全パッドを確認させる
    disconnectedPollCounter = 0;
}

void Input::Update() {
    // --- キーボード ---
    memcpy(keyPre, key, sizeof(key));
    if (FAILED(keyboard->GetDeviceState(sizeof(key), key))) {
        // フォーカスを失っていた等で取得できない場合は再取得し、それでもダメなら全キー離した扱いにする
        keyboard->Acquire();
        if (FAILED(keyboard->GetDeviceState(sizeof(key), key))) {
            memset(key, 0, sizeof(key));
        }
    }

    // --- マウス ---
    mouseStatePre = mouseState;
    if (FAILED(mouse->GetDeviceState(sizeof(DIMOUSESTATE2), &mouseState))) {
        mouse->Acquire();
        if (FAILED(mouse->GetDeviceState(sizeof(DIMOUSESTATE2), &mouseState))) {
            mouseState = {};
        }
    }

    POINT cursor{};
    if (GetCursorPos(&cursor) && ScreenToClient(winApp->GetHwnd(), &cursor)) {
        // ウィンドウサイズやレターボックスの黒帯に関係なく、ゲーム座標(Screen::GetGameSize())で返す。
        // 黒帯の上では範囲外（負の値やゲームサイズ超え）になる
        mousePosition = Screen::ClientToGame({ static_cast<float>(cursor.x), static_cast<float>(cursor.y) });
    }

    // --- 文字入力 ---
    textInputEvents = winApp->TakeTextInputEvents();

    // --- コントローラー (XInput) ---
    bool pollDisconnected = disconnectedPollCounter <= 0;
    disconnectedPollCounter = pollDisconnected ? kDisconnectedPollInterval : disconnectedPollCounter - 1;

    float deltaTime = Time::GetUnscaledDeltaTime();
    for (int i = 0; i < kMaxPads; ++i) {
        PadState& pad = pads[i];
        pad.preState = pad.state;
        pad.wasConnected = pad.isConnected;

        if (pad.isConnected || pollDisconnected) {
            pad.isConnected = XInputGetState(i, &pad.state) == ERROR_SUCCESS;
        }
        if (!pad.isConnected) {
            pad.state = {};
        }

        // 時間指定の振動を止める
        if (pad.vibrationTimer > 0.0f) {
            pad.vibrationTimer -= deltaTime;
            if (pad.vibrationTimer <= 0.0f) {
                pad.vibrationTimer = 0.0f;
                XINPUT_VIBRATION vibration{};
                XInputSetState(i, &vibration);
            }
        }
    }
}

// ===== キーボード =====

bool Input::PushKey(BYTE keyNumber) const {
    return key[keyNumber] != 0;
}

bool Input::TriggerKey(BYTE keyNumber) const {
    return !keyPre[keyNumber] && key[keyNumber];
}

bool Input::ReleaseKey(BYTE keyNumber) const {
    return keyPre[keyNumber] && !key[keyNumber];
}

// ===== マウス =====

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

bool Input::ReleaseMouseButton(int buttonNumber) const {
    bool current = (mouseState.rgbButtons[buttonNumber] & 0x80) != 0;
    bool pre = (mouseStatePre.rgbButtons[buttonNumber] & 0x80) != 0;
    return pre && !current;
}

// ===== ゲームパッド =====

const Input::PadState* Input::GetPad(int padNo) const {
    if (padNo < 0 || padNo >= kMaxPads || !pads[padNo].isConnected) {
        return nullptr;
    }
    return &pads[padNo];
}

bool Input::GetJoystickState(int stickNo, JoystickState& out) const {
    const PadState* pad = GetPad(stickNo);
    if (!pad) {
        return false;
    }
    out.state = pad->state;
    out.preState = pad->preState;
    return true;
}

bool Input::IsPadConnected(int padNo) const {
    return GetPad(padNo) != nullptr;
}

bool Input::PushPadButton(WORD button, int padNo) const {
    const PadState* pad = GetPad(padNo);
    return pad && (pad->state.Gamepad.wButtons & button) != 0;
}

bool Input::TriggerPadButton(WORD button, int padNo) const {
    const PadState* pad = GetPad(padNo);
    return pad && (pad->state.Gamepad.wButtons & button) != 0 && (pad->preState.Gamepad.wButtons & button) == 0;
}

bool Input::ReleasePadButton(WORD button, int padNo) const {
    const PadState* pad = GetPad(padNo);
    return pad && (pad->state.Gamepad.wButtons & button) == 0 && (pad->preState.Gamepad.wButtons & button) != 0;
}

float Input::ApplyStickDeadZone(SHORT raw) const {
    float value = (std::max)(-1.0f, static_cast<float>(raw) / 32767.0f);
    float magnitude = std::fabs(value);
    if (magnitude < stickDeadZone) {
        return 0.0f;
    }
    // デッドゾーンの外側を0〜1に割り当て直す（デッドゾーン境界で値が飛ばないように）
    float rescaled = (magnitude - stickDeadZone) / (1.0f - stickDeadZone);
    return value < 0.0f ? -rescaled : rescaled;
}

Vector2 Input::ApplyStickDeadZone(SHORT rawX, SHORT rawY) const {
    // 円形デッドゾーン（軸ごとに判定すると斜め入力が十字方向に吸い付くため、長さで判定する）
    Vector2 value = {
        (std::max)(-1.0f, static_cast<float>(rawX) / 32767.0f),
        (std::max)(-1.0f, static_cast<float>(rawY) / 32767.0f),
    };
    float length = std::sqrt(value.x * value.x + value.y * value.y);
    if (length < stickDeadZone) {
        return { 0.0f, 0.0f };
    }
    float rescaled = (std::min)(1.0f, (length - stickDeadZone) / (1.0f - stickDeadZone));
    return value * (rescaled / length);
}

Vector2 Input::GetLeftStick(int padNo) const {
    const PadState* pad = GetPad(padNo);
    return pad ? ApplyStickDeadZone(pad->state.Gamepad.sThumbLX, pad->state.Gamepad.sThumbLY) : Vector2{ 0.0f, 0.0f };
}

Vector2 Input::GetRightStick(int padNo) const {
    const PadState* pad = GetPad(padNo);
    return pad ? ApplyStickDeadZone(pad->state.Gamepad.sThumbRX, pad->state.Gamepad.sThumbRY) : Vector2{ 0.0f, 0.0f };
}

float Input::GetLeftTrigger(int padNo) const {
    const PadState* pad = GetPad(padNo);
    return pad ? static_cast<float>(pad->state.Gamepad.bLeftTrigger) / 255.0f : 0.0f;
}

float Input::GetRightTrigger(int padNo) const {
    const PadState* pad = GetPad(padNo);
    return pad ? static_cast<float>(pad->state.Gamepad.bRightTrigger) / 255.0f : 0.0f;
}

void Input::SetVibration(float left, float right, float durationSeconds, int padNo) {
    if (!GetPad(padNo)) {
        return;
    }
    XINPUT_VIBRATION vibration{};
    vibration.wLeftMotorSpeed = static_cast<WORD>((std::clamp)(left, 0.0f, 1.0f) * 65535.0f);
    vibration.wRightMotorSpeed = static_cast<WORD>((std::clamp)(right, 0.0f, 1.0f) * 65535.0f);
    XInputSetState(padNo, &vibration);
    pads[padNo].vibrationTimer = durationSeconds;
}

float Input::GetPadAxisValue(PadAxis axis, int padNo) const {
    const PadState* pad = GetPad(padNo);
    if (!pad) {
        return 0.0f;
    }
    const XINPUT_GAMEPAD& gamepad = pad->state.Gamepad;
    switch (axis) {
    case PadAxis::kLeftX:  return ApplyStickDeadZone(gamepad.sThumbLX);
    case PadAxis::kLeftY:  return ApplyStickDeadZone(gamepad.sThumbLY);
    case PadAxis::kRightX: return ApplyStickDeadZone(gamepad.sThumbRX);
    case PadAxis::kRightY: return ApplyStickDeadZone(gamepad.sThumbRY);
    case PadAxis::kLeftTrigger:  return GetLeftTrigger(padNo);
    case PadAxis::kRightTrigger: return GetRightTrigger(padNo);
    default: return 0.0f;
    }
}

// ===== アクションマップ =====

const Input::ActionBinding* Input::GetActionBinding(const std::string& name) const {
    auto it = actions.find(name);
    return it != actions.end() ? &it->second : nullptr;
}

bool Input::IsActionDown(const ActionBinding& binding, int padNo, bool previous) const {
    const BYTE* keys = previous ? keyPre : key;
    for (BYTE k : binding.keys) {
        if (keys[k]) return true;
    }

    const DIMOUSESTATE2& mouseSnapshot = previous ? mouseStatePre : mouseState;
    for (int button : binding.mouseButtons) {
        if (button >= 0 && button < 8 && (mouseSnapshot.rgbButtons[button] & 0x80)) return true;
    }

    if (const PadState* pad = GetPad(padNo)) {
        WORD buttons = previous ? pad->preState.Gamepad.wButtons : pad->state.Gamepad.wButtons;
        for (WORD button : binding.padButtons) {
            if (buttons & button) return true;
        }
    }
    return false;
}

bool Input::PushAction(const std::string& name, int padNo) const {
    const ActionBinding* binding = GetActionBinding(name);
    return binding && IsActionDown(*binding, padNo, false);
}

bool Input::TriggerAction(const std::string& name, int padNo) const {
    const ActionBinding* binding = GetActionBinding(name);
    return binding && IsActionDown(*binding, padNo, false) && !IsActionDown(*binding, padNo, true);
}

bool Input::ReleaseAction(const std::string& name, int padNo) const {
    const ActionBinding* binding = GetActionBinding(name);
    return binding && !IsActionDown(*binding, padNo, false) && IsActionDown(*binding, padNo, true);
}

float Input::GetAxis(const std::string& name, int padNo) const {
    auto it = axes.find(name);
    if (it == axes.end()) {
        return 0.0f;
    }
    const AxisBinding& binding = it->second;

    float keyValue = 0.0f;
    for (BYTE k : binding.negativeKeys) {
        if (key[k]) { keyValue -= 1.0f; break; }
    }
    for (BYTE k : binding.positiveKeys) {
        if (key[k]) { keyValue += 1.0f; break; }
    }

    float padValue = GetPadAxisValue(binding.padAxis, padNo);
    if (binding.invertPadAxis) {
        padValue = -padValue;
    }

    // 絶対値が大きい方を採用（キーとパッドを同時に触っても値が2倍にならないように）
    return std::fabs(padValue) > std::fabs(keyValue) ? padValue : keyValue;
}
