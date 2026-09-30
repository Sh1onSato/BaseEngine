#pragma once
#include "Model.h"
#include "Calculation.h"
#include "Object3dCommon.h"
#include "Structs.h"
#include "Camera.h"
#include <wrl.h>

class Object3d {
public:
    virtual void Initialize(Object3dCommon* common);
    virtual void Update();
    virtual void Draw();
    // シャドウマップ生成パス用の描画（深度のみ、ライト視点WVPで描画）
    void DrawShadow(const Matrix4x4& lightViewProjection);
    void ImGui(); // 追加

    // Setter
    void SetModel(Model* model) { this->model = model; }
    // スキニング用ボーン行列パレットのセット（非所有ポインタ。SetModel()と同じ方針）
    // 未設定の場合はObject3dCommonのデフォルト単位行列パレットが使われる
    void SetBonePalette(ID3D12Resource* paletteResource) { this->bonePaletteResource = paletteResource; }
    void SetScale(const Vector3& scale) { transform.scale = scale; }
    void SetRotate(const Vector3& rotate);
    void SetRotateQuaternion(const Quaternion& rotate) { quaternionRotation = rotate; }
    void SetTranslate(const Vector3& translate) { transform.translate = translate; }
    void SetCamera(Camera* camera) { this->camera = camera; }
    void SetColor(const Vector4& color) { materialData->color = color; }
    const Vector4& GetColor() const { return materialData->color; }
    void SetEnableLighting(bool enable) { materialData->enableLighting = enable ? 1 : 0; }

    // Getter
    const Transform& GetTransform() const { return transform; }

private:
    Object3dCommon* common = nullptr;
    Model* model = nullptr;
    // スキニング用ボーン行列パレット（非所有）。nullptrならcommonのデフォルト単位行列パレットを使う
    ID3D12Resource* bonePaletteResource = nullptr;

    // 行列計算用のリソース
    Microsoft::WRL::ComPtr<ID3D12Resource> wvpResource;
    TransformationMatrix* wvpData = nullptr;

    // マテリアル用のリソース作成
    Microsoft::WRL::ComPtr<ID3D12Resource> materialResource;
    Material* materialData = nullptr;

    // シャドウマップ生成パス用のWVP（ライト視点）リソース
    Microsoft::WRL::ComPtr<ID3D12Resource> shadowWvpResource;
    Matrix4x4* shadowWvpData = nullptr;

    // 座標情報
    Transform transform;
    Quaternion quaternionRotation = { 0.0f, 0.0f, 0.0f, 1.0f };
    // Update()で計算したワールド行列（DrawShadowでも使い回すためメンバ化）
    Matrix4x4 worldMatrix = Calculation::MakeIdentity4x4();

    Camera* camera = nullptr;
};

