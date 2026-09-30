#include "Sprite.h"
#include "SpriteCommon.h"
#include"Calculation.h"
#include"externals/imgui/imgui.h"
#include"externals/imgui/imgui_impl_dx12.h"
#include"externals/imgui/imgui_impl_win32.h"
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
#include"TextureManager.h"


void Sprite::Initialize(SpriteCommon* spriteCommon, std::string textureFilePath){
    this->spriteCommon = spriteCommon;
    this->dxCommon = spriteCommon->GetDxCommon();
    // 単位行列を書き込んでおく
    textureIndex = TextureManager::GetInstance()->GetTextureIndexByFilePath(textureFilePath);
    // Commonに「住所を教えて！」と頼んで、自分の中にメモ（変数）しておく
    this->textureSrvHandleGPU = spriteCommon->GetTextureHandle(textureIndex);
    this->name = textureFilePath; // メンバ変数にファイル名を保存しておく
    // 1. 頂点リソースの設定 (4頂点分でOK)
    vertexResourceSprite = dxCommon->CreatBufferResource(sizeof(VertexData) * 4);

    float left = 0.0f - anchorPoint.x;
    float right = 1.0f - anchorPoint.x;
    float top = 0.0f - anchorPoint.y;
    float bottom = 1.0f - anchorPoint.y;

    // 左右反転・上下反転の処理
    if (isFlipX) {
        left = -left;
        right = -right;
    }
    if (isFlipY) {
        top = -top;
        bottom = -bottom;
    }

    const DirectX::TexMetadata& metadate = TextureManager::GetInstance()->GetMetadata(textureIndex);
    float tex_left = texLeftTop.x / static_cast<float>(metadate.width);
    float tex_right = (texLeftTop.x + texSize.x) / static_cast<float>(metadate.width);
    float tex_top = texLeftTop.y / static_cast<float>(metadate.height);
    float tex_bottom = (texLeftTop.y + texSize.y) / static_cast<float>(metadate.height);

    // 頂点データの書き込み
    // ※Map()で取得するのはGPUバッファの生メモリで、C++のデフォルトメンバ初期化子は適用されない
    //   （VertexDataのboneWeights/boneIndicesがゴミ値のままだとObject3d.VS.hlslのスキニング計算で
    //   意図しない変形になるため、Spriteでは常に「ボーン0を全重み」で明示的に初期化する）
    vertexResourceSprite->Map(0, nullptr, reinterpret_cast<void**>(&VertexDataSprite));
    for (int i = 0; i < 4; ++i) {
        VertexDataSprite[i].boneWeights = { 1.0f, 0.0f, 0.0f, 0.0f };
        VertexDataSprite[i].boneIndices[0] = VertexDataSprite[i].boneIndices[1] = VertexDataSprite[i].boneIndices[2] = VertexDataSprite[i].boneIndices[3] = 0;
    }
    VertexDataSprite[0].position = { left, bottom, 0.0f, 1.0f }; VertexDataSprite[0].texcoord = { tex_left, tex_bottom };
    VertexDataSprite[1].position = { left, top, 0.0f, 1.0f };   VertexDataSprite[1].texcoord = { tex_left, tex_top };
    VertexDataSprite[2].position = { right, bottom, 0.0f, 1.0f }; VertexDataSprite[2].texcoord = { tex_right, tex_bottom };
    VertexDataSprite[3].position = { right, top, 0.0f, 1.0f };   VertexDataSprite[3].texcoord = { tex_right, tex_top };

    // .hのメンバ変数 vertexBufferViewSprite に代入
    vertexBufferViewSprite.BufferLocation = vertexResourceSprite->GetGPUVirtualAddress();
    vertexBufferViewSprite.SizeInBytes = sizeof(VertexData) * 4;
    vertexBufferViewSprite.StrideInBytes = sizeof(VertexData);

    
    // インデックスリソースの設定 (6要素)
    indexResourceSprite = dxCommon->CreatBufferResource(sizeof(uint32_t) * 6);
    indexBuffViewSprite.BufferLocation = indexResourceSprite->GetGPUVirtualAddress();
    indexBuffViewSprite.SizeInBytes = sizeof(uint32_t) * 6;
    indexBuffViewSprite.Format = DXGI_FORMAT_R32_UINT;

    indexResourceSprite->Map(0, nullptr, reinterpret_cast<void**>(&indexDataSprite));
    indexDataSprite[0] = 0; indexDataSprite[1] = 1; indexDataSprite[2] = 2;
    indexDataSprite[3] = 1; indexDataSprite[4] = 3; indexDataSprite[5] = 2;

    // マテリアルリソースの設定
    materialResourceSprite = dxCommon->CreatBufferResource(sizeof(Material));
    materialResourceSprite->Map(0, nullptr, reinterpret_cast<void**>(&materialData));
    materialData->color = { 1.0f, 1.0f, 1.0f, 1.0f };
    materialData->enableLighting = false;
    materialData->uvTransform = Calculation::MakeIdentity4x4();

    // 行列リソースの設定
    transformationMatrixResourceSprite = dxCommon->CreatBufferResource(sizeof(TransformationMatrix));
    transformationMatrixResourceSprite->Map(0, nullptr, reinterpret_cast<void**>(&transformetionMatrixDataSprite));
    transformetionMatrixDataSprite->World = Calculation::MakeAffineMatrix(transform.scale, transform.rotate, transform.translate);
    transformetionMatrixDataSprite->WVP = Calculation::MakeIdentity4x4();

    AdjustTextureSize();
}

void Sprite::Update() {
    // 1. 頂点リソースの設定 (4頂点分でOK)

    float left = 0.0f - anchorPoint.x;
    float right = 1.0f - anchorPoint.x;
    float top = 0.0f - anchorPoint.y;
    float bottom = 1.0f - anchorPoint.y;

    // 左右反転・上下反転の処理
    if (isFlipX) {
        left = -left;
        right = -right;
    }
    if (isFlipY) {
        top = -top;
        bottom = -bottom;
    }
    //
    const DirectX::TexMetadata& metadate = TextureManager::GetInstance()->GetMetadata(textureIndex);
    float tex_left = texLeftTop.x / static_cast<float>(metadate.width);
    float tex_right = (texLeftTop.x + texSize.x) / static_cast<float>(metadate.width);
    float tex_top = texLeftTop.y / static_cast<float>(metadate.height);
    float tex_bottom = (texLeftTop.y + texSize.y) / static_cast<float>(metadate.height);

    // 頂点データの書き込み（boneWeights/boneIndicesの明示初期化についてはInitialize()側のコメント参照）
    vertexResourceSprite->Map(0, nullptr, reinterpret_cast<void**>(&VertexDataSprite));
    for (int i = 0; i < 4; ++i) {
        VertexDataSprite[i].boneWeights = { 1.0f, 0.0f, 0.0f, 0.0f };
        VertexDataSprite[i].boneIndices[0] = VertexDataSprite[i].boneIndices[1] = VertexDataSprite[i].boneIndices[2] = VertexDataSprite[i].boneIndices[3] = 0;
    }
    VertexDataSprite[0].position = { left, bottom, 0.0f, 1.0f }; VertexDataSprite[0].texcoord = { tex_left, tex_bottom };
    VertexDataSprite[1].position = { left, top, 0.0f, 1.0f };   VertexDataSprite[1].texcoord = { tex_left, tex_top };
    VertexDataSprite[2].position = { right, bottom, 0.0f, 1.0f }; VertexDataSprite[2].texcoord = { tex_right, tex_bottom };
    VertexDataSprite[3].position = { right, top, 0.0f, 1.0f };   VertexDataSprite[3].texcoord = { tex_right, tex_top };

    // 外部から設定された size と transform.scale を掛け合わせる
    Vector3 actualScale = {
        size.x * transform.scale.x,
        size.y * transform.scale.y,
        1.0f
    };
    // 外部から設定されたサイズを反映させる
    Vector3 scale = { size.x * transform.scale.x, size.y * transform.scale.y, 1.0f };
    // スプライトのワールド行列を計算
    Matrix4x4 worldMatrix = Calculation::MakeAffineMatrix(scale, transform.rotate, transform.translate);

    // ビュー行列（スプライトは通常単位行列）
    Matrix4x4 viewMatrix = Calculation::MakeIdentity4x4();

    // プロジェクション行列（平行投影 / 正射影行列）
    Matrix4x4 projectionMatrix = Calculation::MakeOrthographicMatrix(
        0.0f, 0.0f, screenResolution.x, screenResolution.y, 0.0f, 100.0f);

    // 4. WVP行列の合成
    Matrix4x4 wvpMatrix = worldMatrix * viewMatrix * projectionMatrix;

    // 5. GPU上のリソース（定数バッファ）に書き込む
    // Initialize で Map したポインタを保持している場合は、そこに直接代入するだけでOK
    transformetionMatrixDataSprite->WVP = wvpMatrix;
    transformetionMatrixDataSprite->World = worldMatrix;


    Matrix4x4 uvTransformMatrix = Calculation::MakeScaleMatrix(uvTransformSprite.scale) * 
                                  Calculation::MakeRotationZMatrix(uvTransformSprite.rotate.z) * 
                                  Calculation::MakeTranslationMatrix(uvTransformSprite.translate);
    materialData->uvTransform = uvTransformMatrix; // UV変換行列を更新

}

void Sprite::Draw() {
    Draw(this->textureIndex);
}

void Sprite::Draw(uint32_t textureIndex) {
    DrawSRV(TextureManager::GetInstance()->GetSrvIndex(textureIndex));
}

void Sprite::DrawSRV(uint32_t srvIndex) {
    ID3D12GraphicsCommandList* commandList = dxCommon->GetCommandList();

    // 1. 頂点とインデックスをセット
    commandList->IASetVertexBuffers(0, 1, &vertexBufferViewSprite);
    commandList->IASetIndexBuffer(&indexBuffViewSprite);

    // 2. 定数バッファをセット
    commandList->SetGraphicsRootConstantBufferView(0, materialResourceSprite->GetGPUVirtualAddress());
    commandList->SetGraphicsRootConstantBufferView(1, transformationMatrixResourceSprite->GetGPUVirtualAddress());

    // 3. テクスチャをセット（SRVインデックスから直接ハンドルを取得）
    commandList->SetGraphicsRootDescriptorTable(2, spriteCommon->GetTextureHandle(srvIndex));

    // 4. 描画！
    commandList->DrawIndexedInstanced(6, 1, 0, 0, 0);
}

void Sprite::ImGui(){
    // タブバーの開始
    if (ImGui::BeginTabBar("SpriteTabBar")) {

        // --- 座標変換 ---
        if (ImGui::BeginTabItem("Transform")) {
            ImGui::DragFloat2("Position", &transform.translate.x, 1.0f); // 座標
            ImGui::DragFloat2("Size", &size.x, 1.0f);                   // サイズ
            ImGui::SliderAngle("Rotate", &transform.rotate.z);         // 回転
            ImGui::EndTabItem();
        }
        // --- マテリアル色変更 --- 
        if (ImGui::BeginTabItem("Material")) {
            // materialData->color を直接編集する
            // ImGui::ColorEdit4 は 0.0f ~ 1.0f の float4 を自動で扱えます
            ImGui::ColorEdit4("Color", &materialData->color.x);

            // ついでにライティングの有効/無効も切り替えられるように
            ImGui::Checkbox("Enable Lighting", reinterpret_cast<bool*>(&materialData->enableLighting));
            ImGui::EndTabItem();
        }
        // --- UV変換 ---
        if (ImGui::BeginTabItem("UV")) {
            ImGui::DragFloat2("UVTranslate", &uvTransformSprite.translate.x, 0.01f, -10.0f, 10.0f);
            ImGui::DragFloat2("UVScale", &uvTransformSprite.scale.x, 0.01f, -10.0f, 10.0f);
            ImGui::SliderAngle("UVRotate", &uvTransformSprite.rotate.z);
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }
}

void Sprite::AdjustTextureSize(){
	const DirectX::TexMetadata& metadate = TextureManager::GetInstance()->GetMetadata(textureIndex);
    texSize.x = static_cast<float>(metadate.width);
	texSize.y = static_cast<float>(metadate.height);
    // 画像サイズをテクスチャサイズに合わせる
	size = texSize;
}
