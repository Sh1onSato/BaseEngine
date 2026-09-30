#pragma once
#include<string>
#include<Windows.h>
#include <iostream>
#include <dbghelp.h>

namespace Logger{
	void Log(const std::string& message);

	void Log(std::ostream& os, const std::string& message);

	// クラッシュ時のダンプ出力用関数
	LONG WINAPI ExportDump(EXCEPTION_POINTERS* exception);
};

