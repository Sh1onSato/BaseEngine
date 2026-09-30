#include "Model.h"
#include <fstream>

void Model::Initialize(DirectXCommon* dxCommon, const std::string& directoryPath, const std::string& filename) {
	// 1. ファイルを読み込む
	modelData = LoadObjFile(directoryPath, filename);
	if (modelData.material.textureFilePath.size() > 0) {
		TextureManager::GetInstance()->LoadTexture(modelData.material.textureFilePath);
	}

	CreateVertexBuffer(dxCommon);
}

void Model::InitializeFromVertices(DirectXCommon* dxCommon, const std::vector<VertexData>& vertices, const std::string& textureFilePath) {
	modelData.vertices = vertices;
	modelData.material.textureFilePath = textureFilePath;

	CreateVertexBuffer(dxCommon);
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
	vertexResource->Unmap(0, nullptr);
}

void Model::Draw(ID3D12GraphicsCommandList* commandList) {
	// 描画コマンドの発行
	commandList->IASetVertexBuffers(0, 1, &vertexBufferView);
	uint32_t textureIndex = TextureManager::GetInstance()->GetTextureIndexByFilePath(modelData.material.textureFilePath);
	commandList->SetGraphicsRootDescriptorTable(2, TextureManager::GetInstance()->GetSrvHandleGPU(textureIndex));
	commandList->DrawInstanced(UINT(modelData.vertices.size()), 1, 0, 0);

}

MaterialData Model::LoadMaterialTemplateFile(const std::string& directoryPath, const std::string& filename) {
	MaterialData materialData;
	std::string line;
	std::fstream file(directoryPath + "/" + filename);
	assert(file.is_open());
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
	assert(file.is_open());
	while (std::getline(file, line)) {
		std::string identifier;
		std::istringstream s(line);
		s >> identifier;
		if (identifier == "v") {
			Vector4 position;
			s >> position.x >> position.y >> position.z;
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
			modelData.vertices.push_back(triangle[0]);
			modelData.vertices.push_back(triangle[1]);
			modelData.vertices.push_back(triangle[2]);
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