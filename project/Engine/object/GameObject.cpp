#include "GameObject.h"

void GameObject::SetName(const std::string& newName) {
    name = newName;
}

void GameObject::SetTag(const std::string& newTag) {
    tag = newTag;
}

bool GameObject::HasTag(const std::string& checkTag) const {
    return tag == checkTag;
}

void GameObject::SetActive(bool active) {
    isActive = active;
}

void GameObject::Destroy() {
    // ここではすぐに消さず「消す予定」の印を付けるだけ。
    // Update のループ中に物を消すと、ループ中のリストが壊れてしまうため、
    // 実際の削除は GameObjectManager::Update の最後にまとめて行う
    isDestroyed = true;
}

Collider* GameObject::AddCollider(std::unique_ptr<Collider> collider) {
    if (!collider) {
        return nullptr;
    }
    collider->owner = this;
    Collider* result = collider.get();
    colliders.push_back(std::move(collider));
    return result;
}

Rigidbody* GameObject::AddRigidbody() {
    if (!rigidbody) {
        rigidbody = std::make_unique<Rigidbody>();
        rigidbody->owner = this;
    }
    return rigidbody.get();
}

Collider* GameObject::GetCollider(size_t index) const {
    if (index >= colliders.size()) {
        return nullptr;
    }
    return colliders[index].get();
}
