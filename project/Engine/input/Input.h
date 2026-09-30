#pragma once
#include <Windows.h>
#include <wrl.h>
#include <vector>
#include "WinApp.h"

#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>
#include <Xinput.h>

#pragma comment(lib, "dinput8.lib")
#pragma comment(lib, "dxguid.lib")
#pragma comment(lib, "xinput.lib")

using namespace Microsoft::WRL;

class Input {
public:
    struct MouseMove {
        long lX;
        long lY;
        long lZ;
    };

    struct JoystickState {
        XINPUT_STATE state;
        XINPUT_STATE preState;
    };

    // 初期化処理
    void Initialize(WinApp* winApp);
    // 更新処理
    void Update();

    /// <summary>
    /// キーの押下をチェック
    /// </summary>
    bool PushKey(BYTE keyNumber);

    /// <summary>
    /// キーのトリガーをチェック
    /// </summary>
    bool TriggerKey(BYTE keyNumber);

    /// <summary>
    /// マウスの移動量を取得
    /// </summary>
    MouseMove GetMouseMove() const;

    /// <summary>
    /// マウスのボタン押下をチェック (0:左, 1:右, 2:中)
    /// </summary>
    bool PushMouseButton(int buttonNumber) const;

    /// <summary>
    /// マウスのボタントリガーをチェック
    /// </summary>
    bool TriggerMouseButton(int buttonNumber) const;

    /// <summary>
    /// コントローラーの接続状態
    /// </summary>
    bool GetJoystickState(int stickNo, JoystickState& out) const;

private:
    HRESULT hr;

    ComPtr<IDirectInput8> directInput_;
    WinApp* winApp = nullptr;

    // キーボード
    ComPtr<IDirectInputDevice8> keyboard;
    BYTE key[256] = {};
    BYTE keyPre[256] = {};

    // マウス
    ComPtr<IDirectInputDevice8> mouse;
    DIMOUSESTATE2 mouseState = {};
    DIMOUSESTATE2 mouseStatePre = {};

    // コントローラー (XInput)
    XINPUT_STATE xInputState = {};
    XINPUT_STATE xInputStatePre = {};
    bool isControllerConnected = false;
};
