#include "WinApp.h"
#include <cassert>
#include"externals/imgui/imgui.h"
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

void WinApp::Initialize() {
	HRESULT hr = CoInitializeEx(nullptr, COINITBASE_MULTITHREADED);
	
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
		L"DirectX12 Engine", // ウィンドウ名（ゲーム側で変更する場合はここを差し替える）
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
	}
	// 標準のメッセージ処理を行う
	return DefWindowProc(hwnd, msg, wParam, lParam);
}

bool WinApp::ProcessMassage() {
	MSG msg{};
	if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
		//メッセージがあったら処理する
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}

	if(msg.message == WM_QUIT){
		return true;
	}

	return false;
}

void WinApp::ShowCursor(bool isShow) {
	::ShowCursor(isShow);
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
