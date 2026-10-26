#pragma once
#include "Structs.h"
#include <memory>
#include <unordered_map>
#include <vector>

class Model;
class ModelInstancer;

/// <summary>
/// ModelObject の自動まとめ描き。同じモデルの ModelObject を、ModelInstancer で1回の描画命令にまとめて描く。
/// GameObjectManager が持っていて、Draw / DrawShadow の間だけ有効になる（その間に ModelObject が Add する）。
/// 普段は意識しなくてよい（GameObjectManager::SetAutoBatching(false) で止められる）。
/// </summary>
class ModelBatcher {
public:
    ModelBatcher();
    ~ModelBatcher();

    // 集め始める（isShadowPass: true なら影のパス）。Begin 〜 Flush の間だけ IsActive() が true
    void Begin(bool isShadowPass);
    bool IsActive() const { return isActive; }
    // 1つ足す（Object3d と同じ回転順の transform と、色）
    void Add(Model* model, const Transform& transform, const Vector4& color);
    // 集めた物をモデルごとにまとめて描き、集めるのをやめる
    void Flush();

    // 最後の通常パスの Flush で描いた物の数・描画命令の数（デバッグ表示用）
    size_t GetLastObjectCount() const { return lastObjectCount; }
    size_t GetLastDrawCallCount() const { return lastDrawCallCount; }

private:
    struct Group {
        std::unique_ptr<ModelInstancer> instancer;
        // 今のパスで使われたか（使われなかったモデルの分は Flush で捨てる）
        bool isUsed = false;
    };
    using GroupMap = std::unordered_map<Model*, Group>;

    // 影のパスと通常のパスで別の ModelInstancer を使う（同じフレームに同じバッファを2回書き換えないように）
    GroupMap mainGroups;
    GroupMap shadowGroups;
    // 今のパスで使われたモデル（最初に Add された順。描く順を毎フレーム同じにする）
    std::vector<Model*> usedModels;
    bool isActive = false;
    bool isShadowPass = false;
    size_t objectCount = 0;
    size_t lastObjectCount = 0;
    size_t lastDrawCallCount = 0;
};
