#include "Logger.h"
#include <strsafe.h>

#pragma comment(lib, "dbghelp.lib")

namespace Logger{
	void Log(const std::string& message) {
		OutputDebugStringA(message.c_str());
	}

	void Log(std::ostream& os, const std::string& message) {
		os << message << std::endl;
		OutputDebugStringA(message.c_str());
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
