#pragma once
#include <Windows.h>
#include <wrl.h>
#include <string>
#include <unordered_map>
#include <vector>
#include "WinApp.h"
#include "Structs.h"

#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>
#include <Xinput.h>

#pragma comment(lib, "dinput8.lib")
#pragma comment(lib, "dxguid.lib")
#pragma comment(lib, "xinput.lib")

using namespace Microsoft::WRL;

// ゲームパッドのアナログ軸（アクションマップのBindAxisで使う）
enum class PadAxis {
    kNone,
    kLeftX,
    kLeftY,
    kRightX,
    kRightY,
    kLeftTrigger,
    kRightTrigger,
};

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

    // 1つの「操作」に割り当てる入力の組（どれか1つでも押されていれば押下扱い）
    struct ActionBinding {
        std::vector<BYTE> keys;          // DIK_xxx
        std::vector<WORD> padButtons;    // XINPUT_GAMEPAD_xxx
        std::vector<int> mouseButtons;   // 0:左, 1:右, 2:中
    };

    // 1つの「軸」に割り当てる入力（キーの正負ペアとパッドのアナログ軸。大きい方の値を採用）
    struct AxisBinding {
        std::vector<BYTE> negativeKeys;
        std::vector<BYTE> positiveKeys;
        PadAxis padAxis = PadAxis::kNone;
        bool invertPadAxis = false;
    };

    static constexpr int kMaxPads = XUSER_MAX_COUNT;
    // スティックのデッドゾーン（0〜1の比率。これ未満の傾きは0扱いにする）
    static constexpr float kDefaultStickDeadZone = 0.24f;
    static constexpr float kDefaultTriggerThreshold = 0.12f;

    ~Input();

    // 初期化処理
    void Initialize(WinApp* winApp);
    // 更新処理
    void Update();

    // ===== キーボード =====
    /// <summary>
    /// キーの押下をチェック
    /// </summary>
    bool PushKey(BYTE keyNumber) const;

    /// <summary>
    /// キーのトリガーをチェック
    /// </summary>
    bool TriggerKey(BYTE keyNumber) const;

    /// <summary>
    /// キーを離した瞬間をチェック
    /// </summary>
    bool ReleaseKey(BYTE keyNumber) const;

    // ===== マウス =====
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
    /// マウスのボタンを離した瞬間をチェック
    /// </summary>
    bool ReleaseMouseButton(int buttonNumber) const;

    /// <summary>
    /// マウスカーソルのクライアント座標（ウィンドウ左上が原点、ピクセル単位）
    /// </summary>
    const Vector2& GetMousePosition() const { return mousePosition; }

    /// <summary>
    /// ホイールの回転量（奥に回すと正。1ノッチ=120）
    /// </summary>
    long GetMouseWheel() const { return mouseState.lZ; }

    // ===== ゲームパッド (XInput、最大4台) =====
    /// <summary>
    /// コントローラーの接続状態
    /// </summary>
    bool GetJoystickState(int stickNo, JoystickState& out) const;

    bool IsPadConnected(int padNo = 0) const;
    // button は XINPUT_GAMEPAD_A などのビットフラグ
    bool PushPadButton(WORD button, int padNo = 0) const;
    bool TriggerPadButton(WORD button, int padNo = 0) const;
    bool ReleasePadButton(WORD button, int padNo = 0) const;
    // スティックの傾き（各軸-1〜1。デッドゾーン適用済み、上が+Y）
    Vector2 GetLeftStick(int padNo = 0) const;
    Vector2 GetRightStick(int padNo = 0) const;
    // トリガーの押し込み量（0〜1）
    float GetLeftTrigger(int padNo = 0) const;
    float GetRightTrigger(int padNo = 0) const;
    // 振動（0〜1。leftは低周波の大きなモーター、rightは高周波の小さなモーター）
    // durationSeconds > 0 なら指定秒数後に自動で止まる。0なら次にSetVibrationを呼ぶまで続く
    void SetVibration(float left, float right, float durationSeconds = 0.0f, int padNo = 0);
    void StopVibration(int padNo = 0) { SetVibration(0.0f, 0.0f, 0.0f, padNo); }

    void SetStickDeadZone(float deadZone) { stickDeadZone = deadZone; }

    // ===== アクションマップ =====
    // 例: BindAction("Jump", { {DIK_SPACE}, {XINPUT_GAMEPAD_A}, {} });
    //     if (input->TriggerAction("Jump")) { ... }
    // キーコンフィグは同じ名前でBindActionし直せばよい
    void BindAction(const std::string& name, const ActionBinding& binding) { actions[name] = binding; }
    // 例: BindAxis("MoveX", { {DIK_A, DIK_LEFT}, {DIK_D, DIK_RIGHT}, PadAxis::kLeftX });
    void BindAxis(const std::string& name, const AxisBinding& binding) { axes[name] = binding; }
    const ActionBinding* GetActionBinding(const std::string& name) const;

    bool PushAction(const std::string& name, int padNo = 0) const;
    bool TriggerAction(const std::string& name, int padNo = 0) const;
    bool ReleaseAction(const std::string& name, int padNo = 0) const;
    // -1〜1
    float GetAxis(const std::string& name, int padNo = 0) const;

private:
    struct PadState {
        XINPUT_STATE state = {};
        XINPUT_STATE preState = {};
        bool isConnected = false;
        bool wasConnected = false;
        float vibrationTimer = 0.0f; // 残り振動時間（秒）。0以下なら時間指定なし
    };

    bool IsActionDown(const ActionBinding& binding, int padNo, bool previous) const;
    const PadState* GetPad(int padNo) const;
    float GetPadAxisValue(PadAxis axis, int padNo) const;
    float ApplyStickDeadZone(SHORT raw) const;
    Vector2 ApplyStickDeadZone(SHORT rawX, SHORT rawY) const;

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
    Vector2 mousePosition = { 0.0f, 0.0f };

    // コントローラー (XInput)
    PadState pads[kMaxPads] = {};
    // 未接続パッドへのXInputGetStateは重いので、一定フレームごとにしか再確認しない
    static constexpr int kDisconnectedPollInterval = 60;
    int disconnectedPollCounter = 0;
    float stickDeadZone = kDefaultStickDeadZone;

    std::unordered_map<std::string, ActionBinding> actions;
    std::unordered_map<std::string, AxisBinding> axes;
};
