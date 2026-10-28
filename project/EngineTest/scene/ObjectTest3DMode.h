#pragma once
#include "ObjectTest3DObjects.h"
#include "Structs.h"
#include <memory>
#include <string>

class Camera;
class GameObjectManager;
class Object3d;

namespace ObjectTest {
    /// <summary>
    /// ObjectTestScene の「3D当たり判定テスト」モードの中身（カメラ・物の配置・レイキャスト・描画）。
    /// シーンのメンバーとして1つ持ち、物を入れる GameObjectManager はシーンのものを借りる。
    /// 共有メッシュを持っているので、シーンでは GameObjectManager より前に宣言すること（物より長生きさせるため）。
    ///
    /// 使い方（シーン側）:
    ///   Initialize で collision3D.Initialize();
    ///   モード切替で objects.Clear(); collision3D.Spawn(objects);
    ///   Update で objects.Update(); collision3D.Update(objects);（レイキャストは押し戻し後の位置で行う）
    ///   Draw で collision3D.Draw(objects);（影 → 通常描画。objects.Draw() もこの中で呼ぶ）
    ///   Finalize で CameraManager::GetInstance()->Initialize();（登録したカメラを消す）
    /// </summary>
    class Collision3DTest {
    public:
        Collision3DTest();
        ~Collision3DTest();

        // カメラ（CameraManager に登録してアクティブにする）・メッシュ・印を作る。シーンの Initialize で1回呼ぶ
        void Initialize();
        // 物を並べる（呼ぶ前に objects.Clear() しておくこと）
        void Spawn(GameObjectManager& objects);
        // プレイヤーの形（カプセル / 球）を切り替える（作り直す）
        void TogglePlayerShape(GameObjectManager& objects);
        // objects.Update() の後で呼ぶ（レイキャスト）
        void Update(GameObjectManager& objects);
        // シャドウパス → 通常パス（objects.Draw() もここで呼ぶ）
        void Draw(GameObjectManager& objects);

        std::string BuildStatusText(const GameObjectManager& objects) const;
        static const char* GetHelpText();

    private:
        void SetupCamera();
        void UpdateRaycast(GameObjectManager& objects);
        void DrawShadowPass(GameObjectManager& objects);
        // シャドウパスで外されたメインの描画先（RTV / DSV）を張り直す
        void RebindMainRenderTarget();

        Arena3DMeshes meshes;
        // レイが当たった場所の印（物ではないので GameObjectManager に入れず、ここで持つ）
        std::unique_ptr<Object3d> hitMarker;
        // CameraManager が持ち主なので借りるだけ
        Camera* camera = nullptr;

        bool isSphereMode = false;
        int triggerEntryCount = 0; // TriggerZone が書き込む（物より長生きするのでポインタを渡して安全）

        // 最後のレイキャストの結果
        bool hasRayHit = false;
        std::string rayHitName;
        float rayHitDistance = 0.0f;
        Vector3 rayHitPoint = { 0.0f, 0.0f, 0.0f };
    };
}
