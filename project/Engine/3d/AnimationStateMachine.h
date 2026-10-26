#pragma once
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

class SkinnedObject3d;

/// <summary>
/// SkinnedObject3d のアニメーションを「状態」と「切り替えの条件」で動かすクラス（待機 → 歩く → ジャンプ → 待機 …）。
/// ゲーム側は毎フレーム パラメーター（移動の速さ・地面にいるか など）を入れて Update を呼ぶだけで、
/// 条件に合った状態へ、決めた秒数でなめらかにつないで切り替わる。
///
/// 使い方:
///   AnimationStateMachine animator;                 // キャラクターのメンバー
///   animator.Initialize(character);                 // SkinnedObject3d*（SetGltfData の後）
///   animator.AddState("Idle", { .animation = "Idle" });
///   animator.AddState("Move", { .animation = "Walk", .blendAnimation = "Run",   // Speed 1〜4 で Walk → Run に混ぜる
///                               .blendParameter = "Speed", .blendParameterMin = 1.0f, .blendParameterMax = 4.0f });
///   animator.AddState("Jump", { .animation = "Jump", .loop = false });
///   animator.AddTransition({ .from = "Idle", .to = "Move", .condition = [](const AnimationStateMachine& a) { return a.GetFloat("Speed") > 0.1f; } });
///   animator.AddTransition({ .from = "Move", .to = "Idle", .condition = [](const AnimationStateMachine& a) { return a.GetFloat("Speed") <= 0.1f; } });
///   animator.AddTransition({ .from = "*", .to = "Jump", .blendSeconds = 0.1f, .trigger = "Jump" });     // どの状態からでも
///   animator.AddTransition({ .from = "Jump", .to = "Idle", .requireFinished = true });                  // 最後まで再生したら
///   animator.Start("Idle");
///   // 毎フレーム（character->Update() の前）
///   animator.SetFloat("Speed", speed);
///   if (jumped) { animator.SetTrigger("Jump"); }
///   animator.Update();
///
/// 注意:
///   - 遷移は AddTransition した順に調べ、最初に条件が合った1つだけを使う（1フレームに1回だけ切り替わる）
///   - トリガーは、その Update で使われなければ消える（押した瞬間だけ反応する）
/// </summary>
class AnimationStateMachine {
public:
    // 状態1つ分（何を再生するか）
    struct State {
        // 再生するアニメーションの名前
        std::string animation;
        // 空でなければ animation と混ぜる（SkinnedObject3d::PlayBlend）。割合は blendParameter の値で決まる
        std::string blendAnimation;
        // 混ぜる割合を決めるパラメーター。blendParameterMin で animation だけ、blendParameterMax で blendAnimation だけになる
        std::string blendParameter;
        float blendParameterMin = 0.0f;
        float blendParameterMax = 1.0f;
        // false なら最後で止まる（requireFinished の遷移で次の状態へ）。混ぜるときは常にループ
        bool loop = true;
    };

    // 状態の切り替え1つ分（いつ、どこへ）
    struct Transition {
        // 元の状態（"*" ならどの状態からでも。ただし to と同じ状態からは切り替えない）
        std::string from;
        std::string to;
        // つなぐ秒数
        float blendSeconds = 0.2f;
        // 空でなければ、SetTrigger(trigger) されたフレームだけ切り替える（使われたトリガーは消える）
        std::string trigger;
        // 空でなければ、true を返したときだけ切り替える
        std::function<bool(const AnimationStateMachine&)> condition;
        // true なら、今の状態のループしないアニメーションが最後まで行ってから切り替える
        bool requireFinished = false;
    };

    // 動かす SkinnedObject3d（アニメーションが入った後のもの）
    void Initialize(SkinnedObject3d* target);
    void AddState(const std::string& name, const State& state);
    void AddTransition(const Transition& transition);
    // 最初の状態から始める（つながずにすぐ切り替える）
    void Start(const std::string& stateName);

    // 毎フレーム呼ぶ（SkinnedObject3d::Update の前）。条件に合った遷移があれば切り替え、混ぜる割合を更新する
    void Update();
    // 条件に関係なく状態を変える（同じ状態なら最初から）
    void ChangeState(const std::string& stateName, float blendSeconds = 0.2f);

    // ===== パラメーター（条件や混ぜる割合に使う値） =====
    void SetFloat(const std::string& name, float value) { floats[name] = value; }
    float GetFloat(const std::string& name) const;
    void SetBool(const std::string& name, bool value) { bools[name] = value; }
    bool GetBool(const std::string& name) const;
    // 次の Update で1回だけ使える合図（ジャンプ・攻撃など押した瞬間の操作）
    void SetTrigger(const std::string& name) { triggers.insert(name); }

    // ===== 今の状態 =====
    const std::string& GetCurrentState() const { return currentState; }
    // 今の状態に入ってからの秒数
    float GetStateTime() const { return stateTime; }
    // 状態が変わったときに呼ぶ関数（足音・効果音・当たり判定の出し入れなどに）
    void SetOnStateChanged(std::function<void(const std::string& from, const std::string& to)> callback) { onStateChanged = std::move(callback); }

private:
    // state を再生し始める
    void EnterState(const std::string& stateName, float blendSeconds);
    // transition が今使えるか（使えるならトリガーを消費する）
    bool CanTransition(const Transition& transition);
    // 混ぜる状態なら、パラメーターから割合を決めて SkinnedObject3d に渡す
    void UpdateBlendRate();

    SkinnedObject3d* target = nullptr;
    std::map<std::string, State> states;
    std::vector<Transition> transitions;
    std::string currentState;
    float stateTime = 0.0f;

    std::map<std::string, float> floats;
    std::map<std::string, bool> bools;
    std::set<std::string> triggers;
    std::function<void(const std::string&, const std::string&)> onStateChanged;
};
