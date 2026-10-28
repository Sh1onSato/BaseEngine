#include "ObjectTestObjects.h"
#include "Framework.h"
#include "GameTime.h"
#include "Input.h"
#include "Sprite.h"
#include "SpriteCommon.h"
#include <cmath>

namespace {
    constexpr int kWorldLayer = 0;                 // 物はレイヤー0（文字UIは100で手前）
    const char* kPlayerTag = "Player";
    const char* kEnemyTag = "Enemy";
    constexpr float kPlayerSpeed = 300.0f;         // 1秒あたりのピクセル数
    constexpr float kPlayerSize = 48.0f;
    constexpr float kFlashTime = 0.6f;             // 敵に当たったときの点滅時間
    constexpr float kFlashInterval = 0.1f;         // 点滅の切り替え間隔
    constexpr float kCoinRadius = 16.0f;
    constexpr float kEnemyRadius = 24.0f;
    constexpr float kEnemySpeed = 150.0f;
    const Vector2 kCircleFrameSize = { 64.0f, 64.0f }; // test_sheet.png の1コマ

    const Vector4 kPlayerColor = { 0.2f, 0.4f, 1.0f, 1.0f };
    const Vector4 kPlayerTouchColor = { 0.6f, 0.8f, 1.0f, 1.0f };
    const Vector4 kPlayerFlashColor = { 1.0f, 0.3f, 0.3f, 1.0f };
    const Vector4 kWallColor = { 0.5f, 0.5f, 0.5f, 1.0f };
    const Vector4 kGhostColor = { 0.7f, 0.3f, 1.0f, 0.4f };
    const Vector4 kCrateColor = { 0.6f, 0.4f, 0.2f, 1.0f };
    const Vector4 kCoinColor = { 1.0f, 0.9f, 0.1f, 1.0f };
    const Vector4 kEnemyColor = { 1.0f, 0.2f, 0.2f, 1.0f };
}

namespace ObjectTest {

// ===== SpriteObject =====

SpriteObject::SpriteObject(const Vector2& position, const Vector2& size, const Vector4& color, bool isCircle)
    : size(size), color(color), isCircle(isCircle) {
    transform.translate = { position.x, position.y, 0.0f };
}

// Sprite の中身はヘッダーで見えないので、デストラクタは .cpp に置く（unique_ptr<Sprite> の破棄に必要）
SpriteObject::~SpriteObject() = default;

void SpriteObject::CreateSprite() {
    sprite = std::make_unique<Sprite>();
    if (isCircle) {
        sprite->Initialize(Framework::GetInstance()->GetSpriteCommon(), kCircleTexturePath);
        sprite->SetTexLeftTop({ 0.0f, 0.0f });
        sprite->SetTexSize(kCircleFrameSize);
    } else {
        // "white" はエンジン内部で作られる1x1の白画像なので LoadTexture は不要
        sprite->Initialize(Framework::GetInstance()->GetSpriteCommon(), "white");
    }
    sprite->SetSize(size); // 見た目の大きさ = 当たり判定の大きさ
    sprite->SetAnchorPoint({ 0.5f, 0.5f });
    sprite->SetLayer(kWorldLayer);
}

void SpriteObject::Draw() {
    sprite->SetPosition({ transform.translate.x, transform.translate.y });
    sprite->SetColor(color);
    sprite->Update();
    sprite->Draw();
}

// ===== Player =====

Player::Player(const Vector2& position)
    : SpriteObject(position, { kPlayerSize, kPlayerSize }, kPlayerColor, false) {}

void Player::Initialize() {
    SetName(kPlayerTag);
    SetTag(kPlayerTag);
    CreateSprite();
    Collider* collider = AddCollider(Collider::MakeRect(GetSize()));
    collider->SetType(ColliderType::kDynamic);
    collider->SetCollisionLayer(kPlayerLayer);
}

void Player::Update() {
    Move();
    if (flashTimer > 0.0f) {
        flashTimer -= Time::GetDeltaTime();
    }
    UpdateColor();
}

void Player::OnCollisionEnter(const CollisionEvent& event) {
    ++contactCount;
    if (event.other && event.other->HasTag(kEnemyTag)) {
        flashTimer = kFlashTime;
    }
}

void Player::OnCollisionExit(const CollisionEvent& /*event*/) {
    --contactCount;
}

void Player::Move() {
    Input* input = Framework::GetInstance()->GetInput();
    Vector2 direction = { 0.0f, 0.0f };
    if (input->PushKey(DIK_LEFT)) { direction.x -= 1.0f; }
    if (input->PushKey(DIK_RIGHT)) { direction.x += 1.0f; }
    if (input->PushKey(DIK_UP)) { direction.y -= 1.0f; }
    if (input->PushKey(DIK_DOWN)) { direction.y += 1.0f; }

    // 斜めでも速さが変わらないよう長さを1にそろえる
    float length = std::sqrt(direction.x * direction.x + direction.y * direction.y);
    if (length > 0.0f) {
        float step = kPlayerSpeed * Time::GetDeltaTime() / length;
        transform.translate.x += direction.x * step;
        transform.translate.y += direction.y * step;
    }
}

void Player::UpdateColor() {
    // 点滅中は一定間隔で赤と通常色を切り替える
    if (flashTimer > 0.0f && static_cast<int>(flashTimer / kFlashInterval) % 2 == 0) {
        SetColor(kPlayerFlashColor);
        return;
    }
    SetColor(contactCount > 0 ? kPlayerTouchColor : kPlayerColor);
}

// ===== Wall / Ghost / Crate =====

Wall::Wall(const Vector2& position, const Vector2& size)
    : SpriteObject(position, size, kWallColor, false) {}

void Wall::Initialize() {
    SetTag("Wall");
    CreateSprite();
    AddCollider(Collider::MakeRect(GetSize()))->SetType(ColliderType::kStatic);
}

Ghost::Ghost(const Vector2& position, const Vector2& size)
    : SpriteObject(position, size, kGhostColor, false) {}

void Ghost::Initialize() {
    SetTag("Ghost");
    CreateSprite();
    Collider* collider = AddCollider(Collider::MakeRect(GetSize()));
    collider->SetType(ColliderType::kStatic);
    collider->SetCollisionLayer(kGhostLayer);
}

Crate::Crate(const Vector2& position, float size)
    : SpriteObject(position, { size, size }, kCrateColor, false) {}

void Crate::Initialize() {
    SetTag("Crate");
    CreateSprite();
    AddCollider(Collider::MakeRect(GetSize()))->SetType(ColliderType::kDynamic);
}

// ===== Coin =====

Coin::Coin(const Vector2& position, int* score)
    : SpriteObject(position, { kCoinRadius * 2.0f, kCoinRadius * 2.0f }, kCoinColor, true), score(score) {}

void Coin::Initialize() {
    SetTag("Coin");
    CreateSprite();
    AddCollider(Collider::MakeCircle(kCoinRadius))->SetType(ColliderType::kTrigger);
}

void Coin::OnCollisionEnter(const CollisionEvent& event) {
    if (!event.other || !event.other->HasTag(kPlayerTag)) {
        return;
    }
    // ここで消しても、実際に消えるのはフレームの最後なので安全
    Destroy();
    if (score) {
        ++(*score);
    }
}

// ===== Enemy =====

Enemy::Enemy(const Vector2& position, float minX, float maxX)
    : SpriteObject(position, { kEnemyRadius * 2.0f, kEnemyRadius * 2.0f }, kEnemyColor, true),
      velocityX(kEnemySpeed), minX(minX), maxX(maxX) {}

void Enemy::Initialize() {
    SetTag(kEnemyTag);
    CreateSprite();
    AddCollider(Collider::MakeCircle(kEnemyRadius))->SetType(ColliderType::kDynamic);
}

void Enemy::Update() {
    transform.translate.x += velocityX * Time::GetDeltaTime();
    // 行き来する範囲の端で折り返す
    if (transform.translate.x < minX) { velocityX = std::abs(velocityX); }
    if (transform.translate.x > maxX) { velocityX = -std::abs(velocityX); }
}

void Enemy::OnCollisionEnter(const CollisionEvent& event) {
    // 押し返される向き（normal.x）が進む向きと逆なら、前にある物にぶつかったので折り返す
    if (event.normal.x * velocityX < 0.0f) {
        velocityX = -velocityX;
    }
}

}
