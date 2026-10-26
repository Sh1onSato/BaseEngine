#pragma once
#include <Windows.h>
#include <wrl.h> // ComPtr を使用するために必要
#include <string>
#include <cstdint>
#include <functional>
#include <vector>

// 文字入力欄用のキーボード入力（Windows の文字入力をそのまま受け取るので、Shift・CapsLock・キー配列・
// 押しっぱなしの繰り返しが OS の設定どおりになる）。Input::GetTextInputEvents で受け取る
struct TextInputEvent {
	enum class Type {
		kCharacter, // 文字（character に入る）
		kBackspace,
		kDelete,
		kLeft,
		kRight,
		kHome,
		kEnd,
		kEnter,
		kEscape,
		kPaste, // Ctrl+V
	};
	Type type = Type::kCharacter;
	char32_t character = 0;
};

class WinApp{
public:
	// 初期化
	void Initialize();
	// 更新
	void Update();
	// 終了
	void Finalize();
	// ウィンドウプロシージャ
	static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

	bool ProcessMassage();
	// getter
	HWND GetHwnd()const { return hwnd; }
	HINSTANCE GetHinstance()const { return wc.hInstance; }

	// マウスカーソルの表示・非表示
	void ShowCursor(bool isShow);

	// マウスをウィンドウ内に閉じ込める
	void SetClipCursor(bool isClip);

	// ウィンドウタイトルの変更（ゲームごとに名前を変える用）
	void SetTitle(const std::wstring& title);

	// ボーダーレスフルスクリーンの切り替え（Alt+Enter / F11 でも切り替わる）。
	// バックバッファはウィンドウのクライアントサイズに追従し、ゲームの絵はScreenのレイアウト（レターボックス等）で表示する
	void SetFullscreen(bool fullscreen);
	void ToggleFullscreen() { SetFullscreen(!isFullscreen); }
	bool IsFullscreen() const { return isFullscreen; }

	// ウィンドウがアクティブ（フォーカスを持っている）か。非アクティブ時にポーズする等に使う
	bool IsActive() const { return isActive; }

	// ウィンドウサイズが変わっていたら新しいクライアントサイズを返す（1回の変更につき1度だけtrue）
	bool ConsumeResize(uint32_t& width, uint32_t& height);
	// 現在のクライアント領域のサイズ
	void GetClientSize(uint32_t& width, uint32_t& height) const;
	// ウィンドウモード時のクライアント領域のサイズを変える（フルスクリーン中なら、戻したときのサイズとして記録する）
	void SetClientSize(uint32_t width, uint32_t height);
	// ウィンドウモード時のクライアント領域のサイズ（フルスクリーン中は戻したときのサイズ）。設定の保存用
	void GetWindowedClientSize(uint32_t& width, uint32_t& height) const;

	// ウィンドウの枠をドラッグ（サイズ変更・移動）している最中か。
	// この間もゲームは進むので、止めたいゲームはこれを見てポーズする
	bool IsInSizeMove() const { return isInSizeMove; }
	// ドラッグ中はWindowsがメッセージ処理を独占してメインループが回らないので、
	// その間にタイマーから呼んでもらう1フレーム分の処理（Frameworkが登録する。nullptrで解除）
	void SetModalTickCallback(std::function<void()> callback) { modalTickCallback = std::move(callback); }

	// 前回呼んでから届いた文字入力を受け取る（Input::Update が毎フレーム呼ぶ。ゲーム側は Input::GetTextInputEvents を使う）
	std::vector<TextInputEvent> TakeTextInputEvents();
	// 日本語入力（IME）の ON/OFF。英数字の入力欄に打っている間は OFF にして、変換の窓が出ないようにする
	void SetImeEnabled(bool enabled);
	// クリップボードの文字列（UTF-8。文字列が無ければ空）
	std::string GetClipboardText() const;

	// ベース解像度（ゲーム座標の基準。起動時のクライアントサイズもこれ）。
	// 実際のバックバッファはウィンドウサイズに追従するので、描画サイズとしては使わずScreenを参照すること
	static const int KclientWidth = 1280; // ベース解像度の幅
	static const int KclientHeight = 720; // ベース解像度の高さ
private:
	HWND hwnd = nullptr;
	// --- ウィンドウの初期化 ---
	WNDCLASS wc{};

	bool isFullscreen = false;
	bool isActive = true;
	// フルスクリーンから戻すときのウィンドウ位置
	RECT windowedRect{};
	// ウィンドウモードでのクライアントサイズ（WM_SIZEで更新。設定の保存用）
	uint32_t windowedClientWidth = KclientWidth;
	uint32_t windowedClientHeight = KclientHeight;

	// WM_SIZEで受け取った未処理のリサイズ
	bool hasPendingResize = false;
	uint32_t pendingWidth = 0;
	uint32_t pendingHeight = 0;

	// ドラッグ中の描画継続用
	static constexpr UINT_PTR kModalTickTimerId = 1;
	bool isInSizeMove = false;
	std::function<void()> modalTickCallback;

	// 文字入力（WM_CHAR / WM_KEYDOWN で溜め、TakeTextInputEvents で渡す）
	void OnCharMessage(WPARAM wParam);
	void OnKeyDownMessage(WPARAM wParam);
	std::vector<TextInputEvent> pendingTextInput;
	// UTF-16 の2つで1文字になる文字（絵文字など）の1つ目
	wchar_t pendingHighSurrogate = 0;
	bool isImeEnabled = true;

	// WindowProc(static)から自分を参照するため
	static WinApp* instance;
};

