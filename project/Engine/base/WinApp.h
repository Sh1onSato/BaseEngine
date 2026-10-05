#pragma once
#include <Windows.h>
#include <wrl.h> // ComPtr を使用するために必要
#include <string>

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
	// 描画解像度はKclientWidth x KclientHeightのまま、画面サイズに引き伸ばして表示する
	void SetFullscreen(bool fullscreen);
	void ToggleFullscreen() { SetFullscreen(!isFullscreen); }
	bool IsFullscreen() const { return isFullscreen; }

	// ウィンドウがアクティブ（フォーカスを持っている）か。非アクティブ時にポーズする等に使う
	bool IsActive() const { return isActive; }

	static const int KclientWidth = 1280; // クライアント領域の幅
	static const int KclientHeight = 720; // クライアント領域の高さ
private:
	HWND hwnd = nullptr;
	// --- ウィンドウの初期化 ---
	WNDCLASS wc{};

	bool isFullscreen = false;
	bool isActive = true;
	// フルスクリーンから戻すときのウィンドウ位置
	RECT windowedRect{};

	// WindowProc(static)から自分を参照するため
	static WinApp* instance;
};

