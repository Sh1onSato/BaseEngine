#include "Logger.h"
#include <strsafe.h>
#include <d3d12.h>
#include <cstdint>
#include <format>

#pragma comment(lib, "dbghelp.lib")

namespace {
	ID3D12Device* diagnosticsDevice = nullptr;
}

namespace Logger{
	void Log(const std::string& message) {
		OutputDebugStringA(message.c_str());
	}

	void Log(std::ostream& os, const std::string& message) {
		os << message << std::endl;
		OutputDebugStringA(message.c_str());
	}

	void Fatal(const std::string& message) {
		std::string text = "[Fatal] " + message + "\n";
		Log(text);
		// デバッガ接続中はその場で止めて呼び出し履歴を見られるようにする
		if (IsDebuggerPresent()) {
			__debugbreak();
		}
		MessageBoxA(nullptr, text.c_str(), "Fatal Error", MB_OK | MB_ICONERROR);
		ExitProcess(1);
	}

	void CheckHResult(HRESULT hr, const std::string& what) {
		if (SUCCEEDED(hr)) {
			return;
		}
		std::string message = std::format("{} failed. HRESULT=0x{:08X}", what, static_cast<uint32_t>(hr));
		// デバイスが失われていれば、その理由も出す
		if (diagnosticsDevice) {
			HRESULT reason = diagnosticsDevice->GetDeviceRemovedReason();
			if (FAILED(reason)) {
				message += std::format("\nDeviceRemovedReason=0x{:08X}", static_cast<uint32_t>(reason));
			}
		}
		Fatal(message);
	}

	void SetDiagnosticsDevice(ID3D12Device* device) {
		diagnosticsDevice = device;
	}

	LONG WINAPI ExportDump(EXCEPTION_POINTERS* exception) {
		SYSTEMTIME time;
		GetLocalTime(&time);
		wchar_t filePath[MAX_PATH] = { 0 };
		CreateDirectory(L"./Dumps", nullptr);
		StringCchPrintfW(filePath, MAX_PATH, L"./Dumps/%04d-%02d%02d-%02d%02d.dmp", time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute);
		HANDLE dmupFileHandle = CreateFile(filePath, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, 0, CREATE_ALWAYS, 0, 0);
		// processId(このexeのId)とクラッシュ(例外)の発生したtheradIdを取得
		DWORD processId = GetCurrentProcessId();
		DWORD threadId = GetCurrentThreadId();
		// 設定情報を入力
		MINIDUMP_EXCEPTION_INFORMATION minidumpInfomation{ 0 };
		minidumpInfomation.ThreadId = threadId;
		minidumpInfomation.ExceptionPointers = exception;
		minidumpInfomation.ClientPointers = TRUE;
		// Dumpを出力。MiniDumpNormalは最低限の情報を出力するフラグ
		MiniDumpWriteDump(GetCurrentProcess(), processId, dmupFileHandle, MiniDumpNormal, &minidumpInfomation, nullptr, nullptr);
		// 他に関連付けられているSEH例外ハンドラがあれば実行。通常プロセスを終了する
		return EXCEPTION_EXECUTE_HANDLER;
	}
}
