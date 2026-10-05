#pragma once
#include<string>
#include<Windows.h>
#include <iostream>
#include <dbghelp.h>

struct ID3D12Device;

namespace Logger{
	void Log(const std::string& message);

	void Log(std::ostream& os, const std::string& message);

	// 続行できないエラー。ログとメッセージボックスを出してプロセスを終了する（assertと違いReleaseでも有効）
	[[noreturn]] void Fatal(const std::string& message);

	// HRESULTが失敗ならログとメッセージボックスを出してプロセスを終了する（assertと違いReleaseでも有効）
	// what には「何をしようとして失敗したか」を書く
	void CheckHResult(HRESULT hr, const std::string& what);

	// デバイスロスト時に GetDeviceRemovedReason をログへ出すため、生成したデバイスを登録しておく
	void SetDiagnosticsDevice(ID3D12Device* device);

	// クラッシュ時のダンプ出力用関数
	LONG WINAPI ExportDump(EXCEPTION_POINTERS* exception);
};

