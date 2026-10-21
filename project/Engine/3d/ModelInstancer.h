#pragma once
#include "Structs.h"
#include <d3d12.h>
#include <vector>
#include <wrl.h>

class Camera;
class Model;
class Object3dCommon;

/// <summary>
/// 同じモデルを大量に（弾・草・岩・群衆など）、1回の描画命令でまとめて描くクラス（インスタンシング描画）。
/// Object3d を1000個作ると描画命令も1000回になるが、これなら何個でも1回で済む。
/// 1個ずつ違ってよいのは「位置・回転・大きさ」と「色」。モデル・テクスチャ・ライティングの有無は全部共通。
///
/// 使い方:
///   ModelInstancer rocks;                                     // シーンのメンバー
///   rocks.Initialize(ModelManager::GetInstance()->GetCubeModel());   // Initialize で
///   // 毎フレーム（Update で）
///   rocks.Clear();
///   for (auto& rock : rockList) { rocks.Add(rock.transform, rock.color); }
///   // Draw で（Object3d と同じ場所。影 → 通常の順）
///   object3dCommon->PreDrawShadow();  rocks.DrawShadow();  object3dCommon->PostDrawShadow();
///   object3dCommon->PreDraw();        rocks.Draw();
///
/// 注意:
///   - Clear しない限り前のフレームの分が残る（動かない物なら1回 Add するだけでよい）
///   - 1フレームに同じ ModelInstancer の Draw を2回呼ぶと、2回とも後から設定したカメラで描かれる（Object3d と同じ制限）
///   - スキニング（ボーンで動くモデル）には対応していない
///   - 透明（加算以外の半透明）の並べ替えはしない。Add した順に描かれる
/// </summary>
class ModelInstancer {
public:
    // GPU に送る1個分のデータ（Object3dInstanced.VS.hlsl / ShadowInstanced.VS.hlsl の InstanceData と同じ並び）
    struct InstanceData {
        Matrix4x4 world;
        Vector4 color;
    };

    ModelInstancer();
    ~ModelInstancer();

    // model は ModelManager などが持つ（借りるだけ）。initialCapacity を超えて Add しても自動で広がる
    void Initialize(Model* model, size_t initialCapacity = 256);
    void SetModel(Model* newModel) { model = newModel; }
    Model* GetModel() const { return model; }

    // 全部消す（毎フレーム並べ直すときは Update の最初に呼ぶ）
    void Clear();
    // 1個追加する（transform は Object3d と同じ回転順）
    void Add(const Transform& transform, const Vector4& color = { 1.0f, 1.0f, 1.0f, 1.0f });
    // ワールド行列で追加する（自分で行列を計算済みのとき）
    void Add(const Matrix4x4& world, const Vector4& color = { 1.0f, 1.0f, 1.0f, 1.0f });
    size_t GetCount() const { return instances.size(); }

    // 通常パスで描く（Object3dCommon::PreDraw の後で呼ぶ）
    void Draw();
    // 影を描く（Object3dCommon::PreDrawShadow と PostDrawShadow の間で呼ぶ）
    void DrawShadow();

    // 使うカメラ（nullptr なら CameraManager のアクティブカメラ。Object3d と同じ）
    void SetCamera(Camera* newCamera) { camera = newCamera; }
    // 全部に共通のマテリアル（色は Add の色と掛け算される）
    void SetMaterialColor(const Vector4& color) { materialData->color = color; }
    void SetEnableLighting(bool enable) { materialData->enableLighting = enable ? 1 : 0; }

private:
    // instances を GPU のバッファに写す（足りなければ広げる）。前回から変わっていなければ何もしない
    void UploadInstances();
    // count 個入るインスタンスバッファを作り直す
    void CreateInstanceBuffer(size_t count);
    // 今のパスに合ったインスタンシング用 PSO で描き、元の PSO に戻す
    void DrawWithInstancedPipeline(UINT viewProjectionRootIndex, D3D12_GPU_VIRTUAL_ADDRESS viewProjectionAddress,
                                   UINT instancesRootIndex);

    Object3dCommon* common = nullptr;
    Model* model = nullptr;
    Camera* camera = nullptr;

    // CPU 側の一覧（Add で増える）
    std::vector<InstanceData> instances;
    bool isDirty = false;

    // GPU 側のインスタンスバッファ（アップロードヒープ。毎フレーム GPU の完了を待つ作りなので1本で足りる）
    Microsoft::WRL::ComPtr<ID3D12Resource> instanceResource;
    InstanceData* instanceData = nullptr;
    size_t capacity = 0;
    // 同じフレームの途中でバッファを広げたとき、すでに描画命令が参照している古いバッファ（次の Clear で解放）
    std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>> retiredResources;

    // カメラ / ライト視点のビュー × プロジェクション
    Microsoft::WRL::ComPtr<ID3D12Resource> viewProjectionResource;
    Matrix4x4* viewProjectionData = nullptr;
    Microsoft::WRL::ComPtr<ID3D12Resource> lightViewProjectionResource;
    Matrix4x4* lightViewProjectionData = nullptr;

    // 全部に共通のマテリアル（b0）
    Microsoft::WRL::ComPtr<ID3D12Resource> materialResource;
    Material* materialData = nullptr;
};
