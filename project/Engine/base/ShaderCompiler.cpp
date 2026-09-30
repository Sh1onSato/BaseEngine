#include "ShaderCompiler.h"
using namespace Logger;
using namespace StringUtility;

namespace ShaderCompiler{
	Microsoft::WRL::ComPtr<IDxcBlob> CompileShader(
		// CompileするShaderのファイルへのパス
		const std::wstring& filePath,
		// Compileに使用するProfilae
		const wchar_t* profile,
		// 初期化で生成したものを3つ
		IDxcUtils* dxcUtils,
		IDxcCompiler3* dxcCompiler,
		IDxcIncludeHandler* includeHandler,
		std::ostream& os)
	{
		// ここからシェーダーをコンパイルする旨をログに出す
		Log(os, ConvertString(std::format(L"Begin CompileShader,path:{}\n", filePath, profile)));
		// hlslファイルを読む
		Microsoft::WRL::ComPtr <IDxcBlobEncoding> shaderSource = nullptr;
		HRESULT hr = dxcUtils->LoadFile(filePath.c_str(), nullptr, &shaderSource);
		// 読めなかったら止める
		assert(SUCCEEDED(hr));
		// 読み込んだファイルの内容を設定する
		DxcBuffer shaderSourceBuffer;
		shaderSourceBuffer.Ptr = shaderSource->GetBufferPointer();
		shaderSourceBuffer.Size = shaderSource->GetBufferSize();
		shaderSourceBuffer.Encoding = DXC_CP_ACP;

		LPCWSTR arguments[] = {
			filePath.c_str(),
			L"-E", L"main", // エントリーポイント
			L"-T", profile, // プロファイル
			L"-Zi",L"-Qembed_debug", // デバッグ情報を埋め込む
			L"-Od", // 最適化しない
			L"-Zpr",
		};
		// 実際にShaderをCompileする
		Microsoft::WRL::ComPtr <IDxcResult> shaderResult = nullptr;
		hr = dxcCompiler->Compile(
			&shaderSourceBuffer,
			arguments, _countof(arguments),
			includeHandler,
			IID_PPV_ARGS(&shaderResult)
		);
		// コンパイルエラーではなくdxcが起動できないなど致命的な状況
		assert(SUCCEEDED(hr));

		// 警告・エラーが出たらログに出して止める
		Microsoft::WRL::ComPtr<IDxcBlobUtf8> shaderError = nullptr; // 変更点: ComPtr を使用
		shaderResult->GetOutput(
			DXC_OUT_ERRORS, IID_PPV_ARGS(&shaderError), nullptr);
		if (shaderError != nullptr && shaderError->GetStringLength() != 0) {
			// エラーがあった場合は、エラーメッセージをログに出す
			Log(shaderError->GetStringPointer());
			assert(false);
		}
		// コンパイル結果を取得する
		Microsoft::WRL::ComPtr<IDxcBlob> shaderBlob = nullptr; // 変更点: ComPtr を使用
		hr = shaderResult->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&shaderBlob), nullptr);
		assert(SUCCEEDED(hr));
		// 成功したログを出す
		Log(os, ConvertString(std::format(L"Compile Success,path:{}, profile:{}\n", filePath, profile)));

		// 実行用のバイナリを返却
		return shaderBlob;
	};
}