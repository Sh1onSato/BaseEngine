#pragma once
#include <d3d12.h>
#include <dxcapi.h>
#include <wrl.h>
#include <string>
#include <format>
#include <cassert>
#include <iostream>
#include"Logger.h"
#include "StringUtility.h"

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
		std::ostream& os
	);

};

