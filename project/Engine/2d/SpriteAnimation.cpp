#include "SpriteAnimation.h"
#include "Sprite.h"
#include "Logger.h"
#include <algorithm>
#include <format>

void SpriteAnimation::SetSheet(uint32_t columns, uint32_t rows, const Vector2& frameSize) {
	// 0列・0行は割り算できないので最低1にする
	this->columns = (std::max)(columns, 1u);
	this->rows = (std::max)(rows, 1u);
	this->frameSize = frameSize;
}

void SpriteAnimation::AddClip(const std::string& name, uint32_t startFrame, uint32_t frameCount, float framesPerSecond, bool loop) {
	Clip clip;
	clip.startFrame = startFrame;
	clip.frameCount = (std::max)(frameCount, 1u);
	clip.framesPerSecond = framesPerSecond;
	clip.loop = loop;
	// unordered_mapの要素のアドレスは再ハッシュでも変わらないので currentClip はそのまま使える
	clips[name] = clip;
	// 再生中のクリップを上書きした場合はコマ位置が範囲外にならないよう最初から
	if (currentClipName == name) {
		clipFrameIndex = 0;
		elapsed = 0.0f;
		isFinished = false;
	}
}

void SpriteAnimation::Play(const std::string& name, bool restart) {
	// 同じクリップを再生中なら続きから（restart指定時のみ最初から）
	if (currentClip != nullptr && currentClipName == name && !restart) {
		return;
	}
	auto it = clips.find(name);
	if (it == clips.end()) {
		Logger::Log(std::format("SpriteAnimation: 未登録のクリップ \"{}\" を再生しようとしました（無視します）\n", name));
		return;
	}
	currentClip = &it->second;
	currentClipName = name;
	clipFrameIndex = 0;
	elapsed = 0.0f;
	isFinished = false;
}

void SpriteAnimation::Update(float deltaTime) {
	if (currentClip == nullptr || isFinished || currentClip->framesPerSecond <= 0.0f) {
		return;
	}
	elapsed += deltaTime * speed;
	const float frameDuration = 1.0f / currentClip->framesPerSecond;
	// 重いフレームで大きくdeltaTimeが進んでも、その分まとめてコマを進める
	while (elapsed >= frameDuration) {
		elapsed -= frameDuration;
		if (clipFrameIndex + 1 < currentClip->frameCount) {
			++clipFrameIndex;
		} else if (currentClip->loop) {
			clipFrameIndex = 0;
		} else {
			// ループしないクリップは最後のコマで止める
			isFinished = true;
			elapsed = 0.0f;
			break;
		}
	}
}

void SpriteAnimation::Apply(Sprite& sprite) const {
	uint32_t frame = GetCurrentFrame();
	// 左→右、上→下の順に番号が振られている
	uint32_t column = frame % columns;
	uint32_t row = (frame / columns) % rows;
	sprite.SetTexLeftTop({ frameSize.x * static_cast<float>(column), frameSize.y * static_cast<float>(row) });
	sprite.SetTexSize(frameSize);
}

uint32_t SpriteAnimation::GetCurrentFrame() const {
	if (currentClip == nullptr) {
		return 0;
	}
	return currentClip->startFrame + (std::min)(clipFrameIndex, currentClip->frameCount - 1);
}
