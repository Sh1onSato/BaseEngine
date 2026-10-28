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
	// シェーダーのファイルの場所を決める。今のフォルダ（ゲームのフォルダ）に無ければ、エンジンのフォルダ（ENGINE_ROOT_DIR）の物を使う。
	// エンジンのシェーダーを、ゲームのフォルダにコピーしなくても使えるようにするため（ゲームで同じ名前のファイルを置けば、そちらが優先）
	std::wstring ResolveShaderPath(const std::wstring& filePath);

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

