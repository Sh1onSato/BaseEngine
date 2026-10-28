#pragma once
#include "GameObject.h"
#include "Structs.h"
#include <cstdint>
#include <memory>

class Sprite;

/// <summary>
/// ObjectTestScene の「当たり判定テスト」で使う物たち（検証用。ゲーム本編では使わない）。
/// どれも 2D で、transform.translate の x, y を画面上の中心位置として使う。
/// </summary>
namespace ObjectTest {
    // 衝突レイヤー（プレイヤーとゴーストのレイヤー同士は当たらない設定にして、すり抜けを確認する）
    constexpr uint32_t kDefaultLayer = 0;
    constexpr uint32_t kPlayerLayer = 1;
    constexpr uint32_t kGhostLayer = 2;
    // 丸い見た目に使う画像（test_sheet.png の 0 コマ目が丸）。シーンの Initialize で LoadTexture しておくこと
    inline constexpr const char* kCircleTexturePath = "Resources/textures/test_sheet.png";

    /// <summary>
    /// スプライトを1枚持つ物の共通部分。Draw のたびに transform の位置をスプライトへ反映する
    /// （当たり判定の押し戻しは Update の後なので、Draw で反映すれば押し戻し後の位置が見える）。
    /// </summary>
    class SpriteObject : public GameObject {
    public:
        SpriteObject(const Vector2& position, const Vector2& size, const Vector4& color, bool isCircle);
        ~SpriteObject() override;
        void Draw() override;

    protected:
        // Initialize の中で呼ぶ（スプライトを作る）
        void CreateSprite();
        void SetColor(const Vector4& newColor) { color = newColor; }
        const Vector2& GetSize() const { return size; }

    private:
        std::unique_ptr<Sprite> sprite;
        Vector2 size = { 0.0f, 0.0f };
        Vector4 color = { 1.0f, 1.0f, 1.0f, 1.0f };
        bool isCircle = false;
    };

    // 矢印キーで動く四角（Dynamic）。何かに触れている間は明るくなり、敵に当たると赤く点滅する
    class Player : public SpriteObject {
    public:
        explicit Player(const Vector2& position);
        void Initialize() override;
        void Update() override;
        void OnCollisionEnter(const CollisionEvent& event) override;
        void OnCollisionExit(const CollisionEvent& event) override;

        // 今触れている相手の数（Enter で +1、Exit で -1）
        int GetContactCount() const { return contactCount; }

    private:
        void Move();
        void UpdateColor();

        int contactCount = 0;
        float flashTimer = 0.0f;
    };

    // 動かない壁（Static の四角）
    class Wall : public SpriteObject {
    public:
        Wall(const Vector2& position, const Vector2& size);
        void Initialize() override;
    };

    // ゴースト（Static の四角、ゴーストレイヤー）。プレイヤーとは当たらない設定ならすり抜ける
    class Ghost : public SpriteObject {
    public:
        Ghost(const Vector2& position, const Vector2& size);
        void Initialize() override;
    };

    // 押せる箱（Dynamic の四角）
    class Crate : public SpriteObject {
    public:
        Crate(const Vector2& position, float size);
        void Initialize() override;
    };

    // コイン（Trigger の円）。プレイヤーが触れたら消えて score を1増やす
    class Coin : public SpriteObject {
    public:
        // score はシーンが持つ変数を指す（シーンより先にコインが片付くので、指し先が無くなることはない）
        Coin(const Vector2& position, int* score);
        void Initialize() override;
        void OnCollisionEnter(const CollisionEvent& event) override;

    private:
        int* score = nullptr;
    };

    // 敵（Dynamic の円）。左右に行き来し、何かにぶつかったら向きを変える
    class Enemy : public SpriteObject {
    public:
        Enemy(const Vector2& position, float minX, float maxX);
        void Initialize() override;
        void Update() override;
        void OnCollisionEnter(const CollisionEvent& event) override;

    private:
        float velocityX = 0.0f;
        float minX = 0.0f;
        float maxX = 0.0f;
    };
}
