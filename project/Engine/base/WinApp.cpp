#include "WinApp.h"
#include "StringUtility.h"
#include <cassert>
#include <imm.h>
#include"externals/imgui/imgui.h"

#pragma comment(lib, "imm32.lib")
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
		if (instance) {
			instance->OnKeyDownMessage(wParam);
		}
		break;
	case WM_CHAR:
		if (instance) {
			instance->OnCharMessage(wParam);
		}
		break;
	case WM_SYSCHAR:
		// Alt+Enter の「ポン」というビープ音を鳴らさない
		if (wParam == VK_RETURN) {
			return 0;
		}
		break;
	case WM_ENTERSIZEMOVE:
		// 枠のドラッグ開始。ここから先はWindowsのモーダルループになりメインループが止まるので、
		// タイマーで1フレームずつ進める（USER_TIMER_MINIMUMでもPresentの垂直同期待ちでフレームレートは保たれる）
		if (instance) {
			instance->isInSizeMove = true;
			SetTimer(hwnd, kModalTickTimerId, USER_TIMER_MINIMUM, nullptr);
		}
		break;
	case WM_EXITSIZEMOVE:
		if (instance) {
			KillTimer(hwnd, kModalTickTimerId);
			instance->isInSizeMove = false;
		}
		break;
	case WM_TIMER:
		if (wParam == kModalTickTimerId && instance && instance->modalTickCallback) {
			instance->modalTickCallback();
			return 0;
		}
		break;
	case WM_SIZE:
		// ここではサイズを覚えるだけ。バックバッファの作り直しはFramework::Update()の先頭で行う
		// （ドラッグ中は大量に来るので、1フレームに1回にまとめる）。最小化中は0x0になるので無視する
		if (instance && wParam != SIZE_MINIMIZED && LOWORD(lParam) > 0 && HIWORD(lParam) > 0) {
			instance->pendingWidth = LOWORD(lParam);
			instance->pendingHeight = HIWORD(lParam);
			instance->hasPendingResize = true;
			// ウィンドウモードでのサイズを覚えておく（終了時はウィンドウが破棄済みで問い合わせできないため）
			if (!instance->isFullscreen && wParam != SIZE_MAXIMIZED) {
				instance->windowedClientWidth = LOWORD(lParam);
				instance->windowedClientHeight = HIWORD(lParam);
			}
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

bool WinApp::ConsumeResize(uint32_t& width, uint32_t& height) {
	if (!hasPendingResize) {
		return false;
	}
	width = pendingWidth;
	height = pendingHeight;
	hasPendingResize = false;
	return true;
}

void WinApp::GetClientSize(uint32_t& width, uint32_t& height) const {
	RECT rect{};
	GetClientRect(hwnd, &rect);
	width = static_cast<uint32_t>(rect.right - rect.left);
	height = static_cast<uint32_t>(rect.bottom - rect.top);
}

void WinApp::SetClientSize(uint32_t width, uint32_t height) {
	if (width == 0 || height == 0) {
		return;
	}
	// クライアント領域がこのサイズになるよう、枠込みのウィンドウサイズを求める
	RECT rect = { 0, 0, static_cast<LONG>(width), static_cast<LONG>(height) };
	AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, false);
	LONG outerWidth = rect.right - rect.left;
	LONG outerHeight = rect.bottom - rect.top;

	if (isFullscreen) {
		// 今は画面いっぱいなので、ウィンドウモードに戻したときのサイズだけ変えておく
		windowedRect.right = windowedRect.left + outerWidth;
		windowedRect.bottom = windowedRect.top + outerHeight;
		windowedClientWidth = width;
		windowedClientHeight = height;
		return;
	}
	// サイズ変更はWM_SIZE経由でバックバッファに反映される
	SetWindowPos(hwnd, nullptr, 0, 0, outerWidth, outerHeight, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}

void WinApp::GetWindowedClientSize(uint32_t& width, uint32_t& height) const {
	width = windowedClientWidth;
	height = windowedClientHeight;
}

void WinApp::OnCharMessage(WPARAM wParam) {
	const wchar_t code = static_cast<wchar_t>(wParam);
	using Type = TextInputEvent::Type;
	// 文字ではないキー（Ctrl+V などは制御文字として届く）
	switch (code) {
	case 0x08: pendingTextInput.push_back({ Type::kBackspace }); return;
	case 0x0D: pendingTextInput.push_back({ Type::kEnter }); return;
	case 0x1B: pendingTextInput.push_back({ Type::kEscape }); return;
	case 0x16: pendingTextInput.push_back({ Type::kPaste }); return;
	}
	if (code < 0x20 || code == 0x7F) {
		return; // ほかの制御文字（Tab・Ctrl+Backspace など）は使わない
	}
	// 2つで1文字になる文字は、2つ目が来てからまとめる
	if (code >= 0xD800 && code <= 0xDBFF) {
		pendingHighSurrogate = code;
		return;
	}
	char32_t character = code;
	if (code >= 0xDC00 && code <= 0xDFFF) {
		if (pendingHighSurrogate == 0) {
			return;
		}
		character = 0x10000 + ((static_cast<char32_t>(pendingHighSurrogate) - 0xD800) << 10) + (code - 0xDC00);
	}
	pendingHighSurrogate = 0;
	pendingTextInput.push_back({ Type::kCharacter, character });
}

void WinApp::OnKeyDownMessage(WPARAM wParam) {
	// 文字にならないキーは WM_CHAR が来ないのでここで拾う（押しっぱなしの繰り返しも来る）
	using Type = TextInputEvent::Type;
	switch (wParam) {
	case VK_DELETE: pendingTextInput.push_back({ Type::kDelete }); break;
	case VK_LEFT: pendingTextInput.push_back({ Type::kLeft }); break;
	case VK_RIGHT: pendingTextInput.push_back({ Type::kRight }); break;
	case VK_HOME: pendingTextInput.push_back({ Type::kHome }); break;
	case VK_END: pendingTextInput.push_back({ Type::kEnd }); break;
	}
}

std::vector<TextInputEvent> WinApp::TakeTextInputEvents() {
	std::vector<TextInputEvent> events;
	events.swap(pendingTextInput);
	return events;
}

void WinApp::SetImeEnabled(bool enabled) {
	if (enabled == isImeEnabled) {
		return;
	}
	isImeEnabled = enabled;
	// nullptr の入力コンテキストを結び付けると IME が使えなくなる。IACE_DEFAULT で元に戻る
	ImmAssociateContextEx(hwnd, nullptr, enabled ? IACE_DEFAULT : 0);
}

std::string WinApp::GetClipboardText() const {
	std::string result;
	if (!IsClipboardFormatAvailable(CF_UNICODETEXT) || !OpenClipboard(hwnd)) {
		return result;
	}
	if (HANDLE data = GetClipboardData(CF_UNICODETEXT)) {
		if (const wchar_t* text = static_cast<const wchar_t*>(GlobalLock(data))) {
			result = StringUtility::ConvertString(std::wstring(text));
			GlobalUnlock(data);
		}
	}
	CloseClipboard();
	return result;
}

void WinApp::SetTitle(const std::wstring& title) {
	SetWindowTextW(hwnd, title.c_str());
}

void WinApp::SetFullscreen(bool fullscreen) {
	if (fullscreen == isFullscreen) {
		return;
	}

	// SetWindowPos中に届くWM_SIZEで、フルスクリーンのサイズをウィンドウモードのサイズとして覚えないよう先に切り替える
	isFullscreen = fullscreen;

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
}
