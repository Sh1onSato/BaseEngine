#include "AnimationStateMachine.h"
#include "GameTime.h"
#include "Logger.h"
#include "SkinnedObject3d.h"
#include <algorithm>

void AnimationStateMachine::Initialize(SkinnedObject3d* target) {
    this->target = target;
}

void AnimationStateMachine::AddState(const std::string& name, const State& state) {
    states[name] = state;
}

void AnimationStateMachine::AddTransition(const Transition& transition) {
    transitions.push_back(transition);
}

void AnimationStateMachine::Start(const std::string& stateName) {
    EnterState(stateName, 0.0f);
}

void AnimationStateMachine::ChangeState(const std::string& stateName, float blendSeconds) {
    EnterState(stateName, blendSeconds);
}

float AnimationStateMachine::GetFloat(const std::string& name) const {
    auto it = floats.find(name);
    return it != floats.end() ? it->second : 0.0f;
}

bool AnimationStateMachine::GetBool(const std::string& name) const {
    auto it = bools.find(name);
    return it != bools.end() && it->second;
}

void AnimationStateMachine::EnterState(const std::string& stateName, float blendSeconds) {
    auto it = states.find(stateName);
    if (it == states.end() || !target) {
        Logger::Log("AnimationStateMachine: 状態が見つかりません: " + stateName + "\n");
        return;
    }
    const State& state = it->second;
    const std::string previousState = currentState;
    currentState = stateName;
    stateTime = 0.0f;

    if (state.blendAnimation.empty()) {
        target->PlayAnimation(state.animation, state.loop, blendSeconds);
    } else {
        target->PlayBlend(state.animation, state.blendAnimation, 0.0f, blendSeconds);
        UpdateBlendRate();
    }
    if (onStateChanged) {
        onStateChanged(previousState, currentState);
    }
}

bool AnimationStateMachine::CanTransition(const Transition& transition) {
    if (transition.to == currentState && transition.from == "*") {
        return false;
    }
    if (transition.from != "*" && transition.from != currentState) {
        return false;
    }
    if (transition.requireFinished && !target->IsAnimationFinished()) {
        return false;
    }
    if (!transition.trigger.empty() && !triggers.contains(transition.trigger)) {
        return false;
    }
    if (transition.condition && !transition.condition(*this)) {
        return false;
    }
    // 使ったトリガーは消す（同じ合図で2回切り替わらないように）
    if (!transition.trigger.empty()) {
        triggers.erase(transition.trigger);
    }
    return true;
}

void AnimationStateMachine::UpdateBlendRate() {
    auto it = states.find(currentState);
    if (it == states.end() || it->second.blendAnimation.empty()) {
        return;
    }
    const State& state = it->second;
    const float range = state.blendParameterMax - state.blendParameterMin;
    const float value = GetFloat(state.blendParameter);
    const float rate = range != 0.0f ? (value - state.blendParameterMin) / range : 0.0f;
    target->SetBlendRate(std::clamp(rate, 0.0f, 1.0f));
}

void AnimationStateMachine::Update() {
    if (!target || currentState.empty()) {
        triggers.clear();
        return;
    }
    stateTime += Time::GetDeltaTime();

    for (const Transition& transition : transitions) {
        if (CanTransition(transition)) {
            EnterState(transition.to, transition.blendSeconds);
            break;
        }
    }
    UpdateBlendRate();
    // 使われなかったトリガーは、このフレームで消える
    triggers.clear();
}
