#include "MyGame.h"

// Windowsアプリでのエントリーポイント(main関数)
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {

	// D3Dリソースリークチェック
	D3DResourceLeakCheker leakChecker;
	// COM初期化
	CoInitializeEx(0, COINIT_MULTITHREADED);

	// ダンプ出力設定
	SetUnhandledExceptionFilter(Logger::ExportDump);

	// ゲームの生成
	Framework* game = new MyGame();

	// 実行
	game->Run();

	// 解放
	delete game;

	// COM終了
	CoUninitialize();

}
