#pragma once
#include <Windows.h>
#include <wrl.h> // ComPtr を使用するために必要

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

	static const int KclientWidth = 1280; // クライアント領域の幅
	static const int KclientHeight = 720; // クライアント領域の高さ
private:
	HWND hwnd = nullptr;
	// --- ウィンドウの初期化 ---
	WNDCLASS wc{};
};

