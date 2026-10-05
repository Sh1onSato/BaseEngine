#include "WinApp.h"
#include <cassert>
#include"externals/imgui/imgui.h"
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

WinApp* WinApp::instance = nullptr;

void WinApp::Initialize() {
	HRESULT hr = CoInitializeEx(nullptr, COINITBASE_MULTITHREADED);
	instance = this;

	// ウィンドウプロシージャ
	wc.lpfnWndProc = WindowProc;
	// ウィンドウクラス名
	wc.lpszClassName = L"EngineWindowClass";
	// インスタンスハンドル
	wc.hInstance = GetModuleHandle(nullptr);
	//　カーソル
	wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
	// ウィンドウクラスを登録する
	RegisterClass(&wc);
	//ウィンドウサイズを表す構造体に、クライアント領域
	RECT wrc = { 0,0,KclientWidth,KclientHeight };

	// ウィンドウサイズを補正
	AdjustWindowRect(&wrc, WS_OVERLAPPEDWINDOW, false);

	//ウィンドウサイズの生成
	hwnd = CreateWindow(
		wc.lpszClassName, // ウィンドウクラス名
		L"DirectX12 Engine", // ウィンドウ名（ゲーム側で変更する場合はSetTitle()を使う）
		WS_OVERLAPPEDWINDOW, // ウィンドウスタイル
		CW_USEDEFAULT, // x座標
		CW_USEDEFAULT, // y座標
		wrc.right - wrc.left, // 幅
		wrc.bottom - wrc.top, // 高さ
		nullptr, // 親ウィンドウハンドル
		nullptr, // メニューハンドル
		wc.hInstance, // インスタンスハンドル
		nullptr); // ユーザーデータ

	//ウィンドウを表示する
	ShowWindow(hwnd, SW_SHOW);
}

void WinApp::Update() {
}

void WinApp::Finalize(){
	CloseWindow(hwnd);
	CoUninitialize();
	instance = nullptr;
}

// ウィンドウプロシージャ
LRESULT CALLBACK WinApp::WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	if (ImGui::GetCurrentContext()) {
		if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam)) {
			return true;
		}
	}
	switch (msg)
	{
		//ウィンドウが破壊された
	case WM_DESTROY:
	{
		// OSに対して、アプリの終了を伝える
		PostQuitMessage(0);

		return 0;
	}
	case WM_ACTIVATEAPP:
		if (instance) {
			instance->isActive = wParam != FALSE;
		}
		break;
	case WM_SYSKEYDOWN:
		// Alt+Enter でフルスクリーン切り替え（lParamの29bit目がAltの押下状態）
		if (wParam == VK_RETURN && (lParam & (1 << 29)) && instance) {
			instance->ToggleFullscreen();
			return 0;
		}
		break;
	case WM_KEYDOWN:
		// F11 でフルスクリーン切り替え（キーリピートは無視）
		if (wParam == VK_F11 && !(lParam & (1 << 30)) && instance) {
			instance->ToggleFullscreen();
			return 0;
		}
		break;
	case WM_SYSCHAR:
		// Alt+Enter の「ポン」というビープ音を鳴らさない
		if (wParam == VK_RETURN) {
			return 0;
		}
		break;
	}
	// 標準のメッセージ処理を行う
	return DefWindowProc(hwnd, msg, wParam, lParam);
}

bool WinApp::ProcessMassage() {
	// 1フレームに溜まったメッセージを全て処理する（1件ずつだとマウス移動等が溜まって操作が遅れるため）
	MSG msg{};
	while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
		if (msg.message == WM_QUIT) {
			return true;
		}
		//メッセージがあったら処理する
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}

	return false;
}

void WinApp::ShowCursor(bool isShow) {
	// ::ShowCursorは内部カウンタ式なので、呼んだ回数だけ逆を呼ばないと戻らない。目的の状態になるまで呼ぶ
	if (isShow) {
		while (::ShowCursor(TRUE) < 0) {}
	} else {
		while (::ShowCursor(FALSE) >= 0) {}
	}
}

void WinApp::SetClipCursor(bool isClip) {
	if (isClip) {
		RECT rect;
		GetClientRect(hwnd, &rect);
		MapWindowPoints(hwnd, nullptr, reinterpret_cast<LPPOINT>(&rect), 2);
		::ClipCursor(&rect);
	}
	else {
		::ClipCursor(nullptr);
	}
}

void WinApp::SetTitle(const std::wstring& title) {
	SetWindowTextW(hwnd, title.c_str());
}

void WinApp::SetFullscreen(bool fullscreen) {
	if (fullscreen == isFullscreen) {
		return;
	}

	if (fullscreen) {
		// 戻すときのために今のウィンドウ位置を覚えておく
		GetWindowRect(hwnd, &windowedRect);

		// ウィンドウがあるモニターの全体に枠なしで広げる
		MONITORINFO monitorInfo{ sizeof(MONITORINFO) };
		GetMonitorInfo(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &monitorInfo);
		const RECT& monitor = monitorInfo.rcMonitor;

		SetWindowLongPtr(hwnd, GWL_STYLE, WS_POPUP | WS_VISIBLE);
		SetWindowPos(hwnd, HWND_TOP, monitor.left, monitor.top, monitor.right - monitor.left, monitor.bottom - monitor.top,
			SWP_FRAMECHANGED | SWP_NOOWNERZORDER);
	} else {
		SetWindowLongPtr(hwnd, GWL_STYLE, WS_OVERLAPPEDWINDOW | WS_VISIBLE);
		SetWindowPos(hwnd, HWND_NOTOPMOST, windowedRect.left, windowedRect.top,
			windowedRect.right - windowedRect.left, windowedRect.bottom - windowedRect.top,
			SWP_FRAMECHANGED | SWP_NOOWNERZORDER);
	}
	isFullscreen = fullscreen;
}
