#pragma once
#include "GameObject.h"
#include "Structs.h"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

class Model;
class Object3d;

/// <summary>
/// ObjectTestScene の「3D当たり判定テスト」で使う物たち（検証用。ゲーム本編では使わない）。
/// 見た目は Object3d（立方体・球のメッシュを共有）で、transform.scale は「見た目の大きさ」にだけ使う
/// （コライダーは scale を掛けないので、判定の大きさは MakeBox などに渡した値のまま）。
/// </summary>
namespace ObjectTest {
    // 3Dテストの衝突レイヤー（2はゴースト用に2Dテストが使っているので避ける）
    constexpr uint32_t kWorld3DLayer = 0;   // 床・壁・押せる箱
    constexpr uint32_t kPlayer3DLayer = 1;  // プレイヤー（2Dのプレイヤーと同じ）
    constexpr uint32_t kTrigger3DLayer = 3; // トリガー

    /// <summary>
    /// 3Dテストで共有するメッシュ（Model）の持ち主。
    /// 物（MeshObject）はここの Model を借りるだけなので、物より長生きさせること。
    /// </summary>
    class Arena3DMeshes {
    public:
        Arena3DMeshes();
        ~Arena3DMeshes();
        void Initialize();
        Model* GetCube() const { return cube.get(); }
        Model* GetSphere() const { return sphere.get(); }

    private:
        std::unique_ptr<Model> cube;
        std::unique_ptr<Model> sphere;
    };

    /// <summary>
    /// Object3d を1つ持つ物の共通部分。描画のたびに transform を Object3d へ反映する
    /// （当たり判定の押し戻しは Update の後なので、描画時に反映すれば押し戻し後の位置が見える）。
    /// 影を落とすため、シーンは通常の Draw() の前に DrawShadow() を呼ぶ。
    /// </summary>
    class MeshObject : public GameObject {
    public:
        // model は Arena3DMeshes が持つ（借りるだけ）。size は見た目の大きさ（transform.scale に入れる）
        MeshObject(Model* model, const Vector3& position, const Vector3& size, const Vector4& color);
        ~MeshObject() override;
        void Draw() override;
        // シャドウパス用（Object3dCommon::PreDrawShadow と PostDrawShadow の間で呼ぶ）
        void DrawShadow(const Matrix4x4& lightViewProjection);

    protected:
        // Initialize の中で呼ぶ（Object3d を作る）
        void CreateMesh();
        void SetColor(const Vector4& newColor) { color = newColor; }

    private:
        // transform と色を Object3d に反映して行列を計算する
        void SyncMesh();

        std::unique_ptr<Object3d> object3d;
        Model* model = nullptr;
        Vector4 color = { 1.0f, 1.0f, 1.0f, 1.0f };
    };

    // 動かない障害物（床・AABB・OBB・球など。コライダーは外から渡して Static にする）
    class Obstacle : public MeshObject {
    public:
        Obstacle(Model* model, const std::string& name, const Vector3& position, const Vector3& size,
                 float rotateY, const Vector4& color, std::unique_ptr<Collider> collider);
        void Initialize() override;

    private:
        std::string obstacleName;
        std::unique_ptr<Collider> pendingCollider; // Initialize で AddCollider に渡す
    };

    /// <summary>
    /// 矢印キーで XZ 平面を動くプレイヤー（Dynamic）。何かに触れている間は色が明るくなる。
    /// 形はカプセル（見た目は 1x2x1 の箱）か球（半径0.75）。形を変えるときは作り直す。
    /// </summary>
    class Player3D : public MeshObject {
    public:
        Player3D(const Arena3DMeshes& meshes, const Vector2& positionXZ, bool useSphere);
        void Initialize() override;
        void Update() override;
        void OnCollisionEnter(const CollisionEvent& event) override;
        void OnCollisionExit(const CollisionEvent& event) override;

        int GetContactCount() const { return contactCount; }
        bool IsSphere() const { return useSphere; }
        // 最後に動いた向き（XZ 平面・長さ1）。レイキャストの向きに使う
        const Vector3& GetFacing() const { return facing; }

    private:
        void Move();
        // 形ごとの中心の高さ（底がちょうど床の上に来る高さ）
        static float GetCenterHeight(bool useSphere);

        bool useSphere = false;
        int contactCount = 0;
        Vector3 facing = { 0.0f, 0.0f, 1.0f };
    };

    // 押せる箱（Dynamic の AABB）
    class Crate3D : public MeshObject {
    public:
        Crate3D(Model* model, const Vector2& positionXZ);
        void Initialize() override;
        void Update() override;
    };

    // 入った回数を数えるトリガー（見た目は床に敷いた薄い板、判定はその上の箱）
    class TriggerZone : public MeshObject {
    public:
        // entryCount はプレイヤーが入った回数を書き込む先（物より長生きする変数を渡すこと）
        TriggerZone(Model* model, const Vector2& positionXZ, int* entryCount);
        void Initialize() override;
        void Update() override;
        void OnCollisionEnter(const CollisionEvent& event) override;
        void OnCollisionExit(const CollisionEvent& event) override;

    private:
        int* entryCount = nullptr;
        int insideCount = 0; // 今中にいる物の数
    };
}
