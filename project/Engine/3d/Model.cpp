#include "Model.h"
#include "GltfLoader.h"
#include "Logger.h"
#include "Object3dCommon.h"
#include <algorithm>
#include <fstream>

void Model::Initialize(DirectXCommon* dxCommon, const std::string& directoryPath, const std::string& filename) {
	this->dxCommon = dxCommon;
	// 1. ファイルを読み込む
	modelData = LoadObjFile(directoryPath, filename);
	if (modelData.material.textureFilePath.size() > 0) {
		TextureManager::GetInstance()->LoadTexture(modelData.material.textureFilePath);
	} else {
		modelData.material.textureFilePath = "white"; // mtl に画像が無ければ白（色は Object3d::SetColor で付ける）
	}
	SetSingleSubMesh(modelData.material.textureFilePath);
	// .mtl に書かれた法線マップ・発光・金属/粗さ
	ApplyMaterialData(modelData.material);

	CreateVertexBuffer(dxCommon);
}

void Model::InitializeFromVertices(DirectXCommon* dxCommon, const std::vector<VertexData>& vertices, const std::string& textureFilePath) {
	this->dxCommon = dxCommon;
	modelData.vertices = vertices;
	modelData.material.textureFilePath = textureFilePath.empty() ? "white" : textureFilePath;
	// 画像は読み込まない（読み込み済みの物を使う）
	SetSingleSubMesh(modelData.material.textureFilePath);

	CreateVertexBuffer(dxCommon);
}

void Model::InitializeFromGltf(DirectXCommon* dxCommon, const GltfModelData& data, bool keepSkinning) {
	this->dxCommon = dxCommon;
	modelData.vertices.clear();
	subMeshes.clear();
	morphParts.clear();
	for (const GltfPrimitive& primitive : data.primitives) {
		SubMesh subMesh;
		subMesh.startVertex = static_cast<uint32_t>(modelData.vertices.size());
		subMesh.vertexCount = static_cast<uint32_t>(primitive.vertices.size());
		// マテリアルの画像（無ければ色の画像か白）を読み込んで使う
		subMesh.texture = TextureManager::GetInstance()->Acquire(GltfLoader::LoadMaterialTexture(data, primitive.materialIndex));
		InitializeSurface(subMesh);
		ApplyGltfMaterial(subMesh, data, primitive.materialIndex);

		std::vector<VertexData> vertices = primitive.vertices;
		if (!primitive.isSkinned && keepSkinning && primitive.rigidSlot >= 0) {
			// 骨で動かないメッシュは、置かれているノードのパレット（骨の後ろ）だけで動かす（SkinCluster の並びと同じ）
			const uint32_t paletteIndex = static_cast<uint32_t>(data.skin.jointNodeIndices.size()) + static_cast<uint32_t>(primitive.rigidSlot);
			for (VertexData& vertex : vertices) {
				vertex.boneWeights = { 1.0f, 0.0f, 0.0f, 0.0f };
				vertex.boneIndices[0] = paletteIndex;
			}
		}
		if (!primitive.morphTargets.empty()) {
			// 最初の重み（ノードの "weights"）でモーフした形にする。動かす用なら元の形も覚えておく
			std::vector<VertexData> baseVertices = vertices;
			if (primitive.nodeIndex >= 0 && static_cast<size_t>(primitive.nodeIndex) < data.nodes.size()) {
				GltfLoader::ApplyMorphTargets(baseVertices, primitive.morphTargets, data.nodes[primitive.nodeIndex].morphWeights, vertices.data());
			}
			if (keepSkinning) {
				morphParts.push_back({ subMesh.startVertex, primitive.nodeIndex, std::move(baseVertices), primitive.morphTargets });
			}
		}
		if (primitive.isSkinned && !keepSkinning) {
			vertices = GltfLoader::BakeRestPose(data, vertices);
		}
		modelData.vertices.insert(modelData.vertices.end(), vertices.begin(), vertices.end());
		subMeshes.push_back(std::move(subMesh));
	}
	if (modelData.vertices.empty()) {
		Logger::Fatal("Model: glTF に描ける三角形がありません");
	}

	CreateVertexBuffer(dxCommon);
}

void Model::SetSingleSubMesh(const std::string& textureFilePath) {
	subMeshes.clear();
	SubMesh subMesh;
	subMesh.vertexCount = static_cast<uint32_t>(modelData.vertices.size());
	subMesh.texture = TextureManager::GetInstance()->Acquire(textureFilePath);
	InitializeSurface(subMesh);
	subMeshes.push_back(std::move(subMesh));
}

void Model::CreateVertexBuffer(DirectXCommon* dxCommon) {
	// 頂点リソースの作成
	vertexResource = dxCommon->CreatBufferResource(sizeof(VertexData) * modelData.vertices.size());

	// 頂点バッファビューの設定
	vertexBufferView.BufferLocation = vertexResource->GetGPUVirtualAddress();
	vertexBufferView.SizeInBytes = UINT(sizeof(VertexData) * modelData.vertices.size());
	vertexBufferView.StrideInBytes = sizeof(VertexData);

	// データを転送する
	VertexData* vertexData = nullptr;
	vertexResource->Map(0, nullptr, reinterpret_cast<void**>(&vertexData));
	std::memcpy(vertexData, modelData.vertices.data(), sizeof(VertexData) * modelData.vertices.size());
	if (morphParts.empty()) {
		vertexResource->Unmap(0, nullptr);
	} else {
		// モーフする部分を毎フレーム書き換えるので Map したままにする（アップロードヒープなので問題ない）
		mappedVertices = vertexData;
	}
}

void Model::ApplyMorphWeights(int32_t nodeIndex, const std::vector<float>& weights) {
	if (!mappedVertices) {
		return;
	}
	for (const MorphPart& part : morphParts) {
		if (part.nodeIndex == nodeIndex) {
			GltfLoader::ApplyMorphTargets(part.baseVertices, part.targets, weights, mappedVertices + part.startVertex);
		}
	}
}

void Model::Draw(ID3D12GraphicsCommandList* commandList, UINT instanceCount) {
	// 描画コマンドの発行
	commandList->IASetVertexBuffers(0, 1, &vertexBufferView);
	// 部分ごとに画像・材質を切り替えて描く
	TextureManager* textureManager = TextureManager::GetInstance();
	for (const SubMesh& subMesh : subMeshes) {
		commandList->SetGraphicsRootDescriptorTable(2, textureManager->GetSrvHandleGPU(subMesh.texture.GetIndex()));
		commandList->SetGraphicsRootConstantBufferView(Object3dCommon::kRootIndexSurfaceMaterial, subMesh.surfaceResource->GetGPUVirtualAddress());
		for (UINT i = 0; i < Object3dCommon::kMaterialTextureCount; ++i) {
			commandList->SetGraphicsRootDescriptorTable(Object3dCommon::kRootIndexMaterialTextures + i,
			                                            textureManager->GetSrvHandleGPU(subMesh.materialTextures[i].GetIndex()));
		}
		commandList->DrawInstanced(subMesh.vertexCount, instanceCount, subMesh.startVertex, 0);
	}

}

MaterialData Model::LoadMaterialTemplateFile(const std::string& directoryPath, const std::string& filename) {
	MaterialData materialData;
	std::string line;
	std::fstream file(directoryPath + "/" + filename);
	if (!file.is_open()) {
		Logger::Fatal("Model: cannot open " + directoryPath + "/" + filename);
	}
	while (std::getline(file, line)) {
		std::string identifier;
		std::istringstream s(line);
		s >> identifier;

		if (identifier == "map_Kd") {
			std::string textureFilename;
			s >> textureFilename;

			materialData.textureFilePath = directoryPath + "/" + textureFilename;
		} else if (identifier == "map_Bump" || identifier == "map_bump" || identifier == "bump" || identifier == "norm") {
			// 例: map_Bump -bm 1.000000 normal.png（-bm の後は強さ。ほかの -xx は読み飛ばす）
			std::string token;
			while (s >> token) {
				if (token == "-bm") {
					s >> materialData.normalStrength;
				} else if (!token.empty() && token[0] == '-') {
					std::string skipped;
					s >> skipped;
				} else {
					materialData.normalMapFilePath = directoryPath + "/" + token;
				}
			}
		} else if (identifier == "map_Ke") {
			std::string textureFilename;
			s >> textureFilename;
			materialData.emissiveMapFilePath = directoryPath + "/" + textureFilename;
		} else if (identifier == "Ke") {
			s >> materialData.emissive.x >> materialData.emissive.y >> materialData.emissive.z;
		} else if (identifier == "Pm") {
			s >> materialData.metallic;
		} else if (identifier == "Pr") {
			s >> materialData.roughness;
		}
	}
	return materialData;
}

ModelData Model::LoadObjFile(const std::string& directoryPath, const std::string& filename) {
	ModelData modelData;
	std::vector<Vector4> positions;
	std::vector<Vector3> normals;
	std::vector<Vector2> texcoords;
	std::string line;
	std::fstream file(directoryPath + "/" + filename);
	if (!file.is_open()) {
		Logger::Fatal("Model: cannot open " + directoryPath + "/" + filename);
	}
	while (std::getline(file, line)) {
		std::string identifier;
		std::istringstream s(line);
		s >> identifier;
		if (identifier == "v") {
			Vector4 position;
			s >> position.x >> position.y >> position.z;
			// OBJ（右手系）→ エンジン（左手系）。Z を反転する（GltfLoader と同じ。Blender で見た向きのまま表示される）
			position.z = -position.z;
			position.w = 1.0f;
			positions.push_back(position);
		}
		else if (identifier == "vt") {
			Vector2 texcoord;
			s >> texcoord.x >> texcoord.y;
			texcoord.y = 1.0f - texcoord.y;
			texcoords.push_back(texcoord);
		}
		else if (identifier == "vn") {
			Vector3 normal;
			s >> normal.x >> normal.y >> normal.z;
			normal.z = -normal.z;
			normals.push_back(normal);
		}
		else if (identifier == "f") {
			VertexData triangle[3];
			for (int32_t facevertex = 0; facevertex < 3; ++facevertex) {
				std::string vertexDefinition;
				s >> vertexDefinition;
				std::istringstream v(vertexDefinition);
				uint32_t elementIndice[3];
				for (uint32_t element = 0; element < 3; ++element) {
					std::string index;
					std::getline(v, index, '/');
					elementIndice[element] = std::stoi(index);
				}
				Vector4 position = positions[elementIndice[0] - 1];
				Vector2 texcoord = texcoords[elementIndice[1] - 1];
				Vector3 normal = normals[elementIndice[2] - 1];
				triangle[facevertex] = { position, texcoord, normal };
			}
			// Z を反転すると表と裏が入れ替わるので、2番目と3番目の頂点を入れ替える
			modelData.vertices.push_back(triangle[0]);
			modelData.vertices.push_back(triangle[2]);
			modelData.vertices.push_back(triangle[1]);
		}
		else if (identifier == "mtllib") {
			std::string materialFilename;
			s >> materialFilename;

			// マテリアルファイルを読み込む
			modelData.material = LoadMaterialTemplateFile(directoryPath, materialFilename);
		}
	}
	return modelData;
}

// ===== 材質 =====

void Model::InitializeSurface(SubMesh& subMesh) {
	subMesh.surfaceResource = dxCommon->CreatBufferResource(sizeof(SurfaceMaterial));
	Logger::CheckHResult(subMesh.surfaceResource->Map(0, nullptr, reinterpret_cast<void**>(&subMesh.surface)), "Model: Map surfaceResource");
	*subMesh.surface = SurfaceMaterial{};
	// 使わない画像の枠にも何か入れておく必要があるので白（flags で使わないことにしてある）
	for (TextureHandle& texture : subMesh.materialTextures) {
		texture = TextureManager::GetInstance()->Acquire("white");
	}
}

template <typename Function>
void Model::ForEachPart(int part, Function function) {
	if (part >= 0) {
		if (static_cast<size_t>(part) < subMeshes.size()) {
			function(subMeshes[static_cast<size_t>(part)]);
		} else {
			Logger::Log("Model: 部分の番号が範囲外です\n");
		}
		return;
	}
	for (SubMesh& subMesh : subMeshes) {
		function(subMesh);
	}
}

void Model::SetMaterialTexture(int part, MaterialTextureSlot slot, const std::string& textureName, uint32_t flag) {
	ForEachPart(part, [&](SubMesh& subMesh) {
		subMesh.materialTextures[slot] = TextureManager::GetInstance()->Acquire(textureName);
		subMesh.surface->flags |= flag;
	});
}

void Model::SetNormalMap(const std::string& filePath, float strength, int part) {
	SetMaterialTexture(part, kNormalSlot, TextureManager::GetInstance()->LoadDataTexture(filePath), SurfaceMaterialFlags::kNormalMap);
	ForEachPart(part, [&](SubMesh& subMesh) { subMesh.surface->normalStrength = strength; });
}

void Model::SetMetallicRoughnessMap(const std::string& filePath, int part) {
	SetMaterialTexture(part, kMetallicRoughnessSlot, TextureManager::GetInstance()->LoadDataTexture(filePath),
	                   SurfaceMaterialFlags::kMetallicRoughnessMap | SurfaceMaterialFlags::kPhysicalLighting);
}

void Model::SetEmissiveMap(const std::string& filePath, int part) {
	// 発光の画像は色の画像なので、普通に読む
	TextureManager::GetInstance()->LoadTexture(filePath);
	SetMaterialTexture(part, kEmissiveSlot, filePath, SurfaceMaterialFlags::kEmissiveMap);
}

void Model::SetOcclusionMap(const std::string& filePath, float strength, int part) {
	SetMaterialTexture(part, kOcclusionSlot, TextureManager::GetInstance()->LoadDataTexture(filePath), SurfaceMaterialFlags::kOcclusionMap);
	ForEachPart(part, [&](SubMesh& subMesh) { subMesh.surface->occlusionStrength = strength; });
}

void Model::SetMetallic(float metallic, int part) {
	ForEachPart(part, [&](SubMesh& subMesh) {
		subMesh.surface->metallic = std::clamp(metallic, 0.0f, 1.0f);
		subMesh.surface->flags |= SurfaceMaterialFlags::kPhysicalLighting;
	});
}

void Model::SetRoughness(float roughness, int part) {
	ForEachPart(part, [&](SubMesh& subMesh) {
		subMesh.surface->roughness = std::clamp(roughness, 0.0f, 1.0f);
		subMesh.surface->flags |= SurfaceMaterialFlags::kPhysicalLighting;
	});
}

void Model::SetEmissive(const Vector3& color, float intensity, int part) {
	ForEachPart(part, [&](SubMesh& subMesh) { subMesh.surface->emissive = { color.x * intensity, color.y * intensity, color.z * intensity, 0.0f }; });
}

void Model::SetPhysicalLighting(bool enable, int part) {
	ForEachPart(part, [&](SubMesh& subMesh) {
		if (enable) {
			subMesh.surface->flags |= SurfaceMaterialFlags::kPhysicalLighting;
		} else {
			subMesh.surface->flags &= ~SurfaceMaterialFlags::kPhysicalLighting;
		}
	});
}

SurfaceMaterial& Model::GetSurfaceMaterial(size_t part) {
	return *subMeshes[part < subMeshes.size() ? part : 0].surface;
}

void Model::ApplyMaterialData(const MaterialData& material) {
	if (!material.normalMapFilePath.empty()) {
		SetNormalMap(material.normalMapFilePath, material.normalStrength);
	}
	if (!material.emissiveMapFilePath.empty()) {
		SetEmissiveMap(material.emissiveMapFilePath);
		// 画像だけで色が書かれていなければ、画像の色そのままで光る
		if (material.emissive.x == 0.0f && material.emissive.y == 0.0f && material.emissive.z == 0.0f) {
			SetEmissive({ 1.0f, 1.0f, 1.0f });
		}
	}
	if (material.emissive.x != 0.0f || material.emissive.y != 0.0f || material.emissive.z != 0.0f) {
		SetEmissive(material.emissive);
	}
	if (material.metallic >= 0.0f) {
		SetMetallic(material.metallic);
	}
	if (material.roughness >= 0.0f) {
		SetRoughness(material.roughness);
	}
}

void Model::ApplyGltfMaterial(SubMesh& subMesh, const GltfModelData& data, int32_t materialIndex) {
	SurfaceMaterial& surface = *subMesh.surface;
	// glTF は金属・粗さで作られているので、その当たり方にする
	surface.flags |= SurfaceMaterialFlags::kPhysicalLighting;
	if (materialIndex < 0 || static_cast<size_t>(materialIndex) >= data.materials.size()) {
		surface.metallic = 0.0f;
		surface.roughness = 1.0f; // マテリアルの無い glTF の既定（白・金属でない・ざらざら）
		return;
	}
	const GltfMaterial& material = data.materials[static_cast<size_t>(materialIndex)];
	surface.metallic = material.metallicFactor;
	surface.roughness = material.roughnessFactor;
	surface.normalStrength = material.normalScale;
	surface.occlusionStrength = material.occlusionStrength;
	surface.emissive = { material.emissiveFactor.x, material.emissiveFactor.y, material.emissiveFactor.z, 0.0f };
	TextureManager* textureManager = TextureManager::GetInstance();
	auto useImage = [&](int32_t imageIndex, bool isData, MaterialTextureSlot slot, uint32_t flag) {
		const std::string name = GltfLoader::LoadImageTexture(data, imageIndex, isData);
		if (!name.empty()) {
			subMesh.materialTextures[slot] = textureManager->Acquire(name);
			surface.flags |= flag;
		}
	};
	useImage(material.normalImageIndex, true, kNormalSlot, SurfaceMaterialFlags::kNormalMap);
	useImage(material.metallicRoughnessImageIndex, true, kMetallicRoughnessSlot, SurfaceMaterialFlags::kMetallicRoughnessMap);
	useImage(material.emissiveImageIndex, false, kEmissiveSlot, SurfaceMaterialFlags::kEmissiveMap);
	useImage(material.occlusionImageIndex, true, kOcclusionSlot, SurfaceMaterialFlags::kOcclusionMap);
}