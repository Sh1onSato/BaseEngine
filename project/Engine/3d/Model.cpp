#include "Model.h"
#include "GltfLoader.h"
#include "Logger.h"
#include <fstream>

void Model::Initialize(DirectXCommon* dxCommon, const std::string& directoryPath, const std::string& filename) {
	// 1. ファイルを読み込む
	modelData = LoadObjFile(directoryPath, filename);
	if (modelData.material.textureFilePath.size() > 0) {
		TextureManager::GetInstance()->LoadTexture(modelData.material.textureFilePath);
	} else {
		modelData.material.textureFilePath = "white"; // mtl に画像が無ければ白（色は Object3d::SetColor で付ける）
	}
	SetSingleSubMesh(modelData.material.textureFilePath);

	CreateVertexBuffer(dxCommon);
}

void Model::InitializeFromVertices(DirectXCommon* dxCommon, const std::vector<VertexData>& vertices, const std::string& textureFilePath) {
	modelData.vertices = vertices;
	modelData.material.textureFilePath = textureFilePath.empty() ? "white" : textureFilePath;
	// 画像は読み込まない（読み込み済みの物を使う）
	SetSingleSubMesh(modelData.material.textureFilePath);

	CreateVertexBuffer(dxCommon);
}

void Model::InitializeFromGltf(DirectXCommon* dxCommon, const GltfModelData& data, bool keepSkinning) {
	modelData.vertices.clear();
	subMeshes.clear();
	morphParts.clear();
	for (const GltfPrimitive& primitive : data.primitives) {
		SubMesh subMesh;
		subMesh.startVertex = static_cast<uint32_t>(modelData.vertices.size());
		subMesh.vertexCount = static_cast<uint32_t>(primitive.vertices.size());
		// マテリアルの画像（無ければ色の画像か白）を読み込んで使う
		subMesh.texture = TextureManager::GetInstance()->Acquire(GltfLoader::LoadMaterialTexture(data, primitive.materialIndex));

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
	// 部分ごとに画像を切り替えて描く
	for (const SubMesh& subMesh : subMeshes) {
		commandList->SetGraphicsRootDescriptorTable(2, TextureManager::GetInstance()->GetSrvHandleGPU(subMesh.texture.GetIndex()));
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