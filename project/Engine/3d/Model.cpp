#include "Model.h"
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
	texture = TextureManager::GetInstance()->Acquire(modelData.material.textureFilePath);

	CreateVertexBuffer(dxCommon);
}

void Model::InitializeFromVertices(DirectXCommon* dxCommon, const std::vector<VertexData>& vertices, const std::string& textureFilePath) {
	modelData.vertices = vertices;
	modelData.material.textureFilePath = textureFilePath.empty() ? "white" : textureFilePath;
	// 画像は読み込まない（読み込み済みの物を使う）
	texture = TextureManager::GetInstance()->Acquire(modelData.material.textureFilePath);

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

void Model::Draw(ID3D12GraphicsCommandList* commandList, UINT instanceCount) {
	// 描画コマンドの発行
	commandList->IASetVertexBuffers(0, 1, &vertexBufferView);
	commandList->SetGraphicsRootDescriptorTable(2, TextureManager::GetInstance()->GetSrvHandleGPU(texture.GetIndex()));
	commandList->DrawInstanced(UINT(modelData.vertices.size()), instanceCount, 0, 0);

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