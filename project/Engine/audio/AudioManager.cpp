#include "AudioManager.h"
#include "AudioCommon.h"
#include "Logger.h"
#include <algorithm>
#include <cassert>
#include <format>

AudioManager* AudioManager::instance = nullptr;

namespace {
    float Clamp01(float value) {
        return (std::clamp)(value, 0.0f, 1.0f);
    }
}

AudioManager* AudioManager::GetInstance() {
    if (instance == nullptr) {
        instance = new AudioManager();
    }
    return instance;
}

void AudioManager::Initialize() {
    AudioCommon::GetInstance()->Initialize();

    // カテゴリ別のサブミックスボイスを作成（ソースボイス → カテゴリ → マスター の経路にする）
    IXAudio2* xAudio2 = AudioCommon::GetInstance()->GetXAudio2();
    XAUDIO2_VOICE_DETAILS masterDetails{};
    AudioCommon::GetInstance()->GetMasteringVoice()->GetVoiceDetails(&masterDetails);
    for (size_t i = 0; i < static_cast<size_t>(AudioCategory::kCount); ++i) {
        HRESULT hr = xAudio2->CreateSubmixVoice(&categoryVoices[i], masterDetails.InputChannels, masterDetails.InputSampleRate);
        assert(SUCCEEDED(hr));
        categoryVoices[i]->SetVolume(categoryVolumes[i]);
    }
    AudioCommon::GetInstance()->GetMasteringVoice()->SetVolume(masterVolume);
}

void AudioManager::Finalize() {
    // ボイスがバッファを参照している間にバッファを解放してはいけないので、先にボイスを全て破棄する
    for (auto& [handle, voice] : voices) {
        DestroyVoice(voice);
    }
    voices.clear();
    currentBgm = kInvalidSoundHandle;

    for (auto& categoryVoice : categoryVoices) {
        if (categoryVoice) {
            categoryVoice->DestroyVoice();
            categoryVoice = nullptr;
        }
    }

    for (auto& pair : soundMap) {
        Audio::SoundUnload(&pair.second);
    }
    soundMap.clear();

    AudioCommon::GetInstance()->Finalize();

    delete instance;
    instance = nullptr;
}

void AudioManager::Update(float deltaTime) {
    for (auto it = voices.begin(); it != voices.end(); ) {
        PlayingVoice& voice = it->second;

        // フェード処理
        if (voice.fadeDuration > 0.0f && !voice.isPaused) {
            voice.fadeElapsed += deltaTime;
            float t = Clamp01(voice.fadeElapsed / voice.fadeDuration);
            voice.sourceVoice->SetVolume(voice.fadeFrom + (voice.fadeTo - voice.fadeFrom) * t);
            if (t >= 1.0f) {
                voice.fadeDuration = 0.0f;
                if (voice.stopAfterFade) {
                    DestroyVoice(voice);
                }
            }
        }

        // 再生終了（キューにバッファが残っていない）したボイスを解放
        bool finished = voice.sourceVoice == nullptr;
        if (!finished) {
            XAUDIO2_VOICE_STATE state{};
            voice.sourceVoice->GetState(&state, XAUDIO2_VOICE_NOSAMPLESPLAYED);
            finished = state.BuffersQueued == 0;
        }

        if (finished) {
            DestroyVoice(voice);
            if (it->first == currentBgm) {
                currentBgm = kInvalidSoundHandle;
            }
            it = voices.erase(it);
        } else {
            ++it;
        }
    }
}

void AudioManager::Load(const std::string& name, const std::string& filePath) {
    if (soundMap.find(name) != soundMap.end()) {
        return;
    }

    Audio::SoundData data;
    // 拡張子を取得して判別
    std::string ext = filePath.substr(filePath.find_last_of(".") + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (ext == "wav") {
        data = Audio::SoundLoadWave(filePath);
    } else {
        data = Audio::SoundLoadCompressed(filePath);
    }

    soundMap[name] = data;
}

void AudioManager::Unload(const std::string& name) {
    auto it = soundMap.find(name);
    if (it == soundMap.end()) {
        return;
    }

    // このデータを参照しているボイスを先に止める
    for (auto voiceIt = voices.begin(); voiceIt != voices.end(); ) {
        if (voiceIt->second.soundName == name) {
            DestroyVoice(voiceIt->second);
            if (voiceIt->first == currentBgm) {
                currentBgm = kInvalidSoundHandle;
            }
            voiceIt = voices.erase(voiceIt);
        } else {
            ++voiceIt;
        }
    }

    Audio::SoundUnload(&it->second);
    soundMap.erase(it);
}

SoundHandle AudioManager::Play(const std::string& name, bool loop, float volume, AudioCategory category) {
    auto it = soundMap.find(name);
    if (it == soundMap.end()) {
        Logger::Log(std::format("AudioManager: 未ロードの音声が再生されました \"{}\"\n", name));
        assert(false);
        return kInvalidSoundHandle;
    }

    const Audio::SoundData& soundData = it->second;
    IXAudio2* xAudio2 = AudioCommon::GetInstance()->GetXAudio2();

    // 出力先をカテゴリのサブミックスボイスにする
    XAUDIO2_SEND_DESCRIPTOR sendDescriptor{ 0, categoryVoices[static_cast<size_t>(category)] };
    XAUDIO2_VOICE_SENDS sends{ 1, &sendDescriptor };

    IXAudio2SourceVoice* sourceVoice = nullptr;
    HRESULT hr = xAudio2->CreateSourceVoice(&sourceVoice, &soundData.wfex, 0, XAUDIO2_DEFAULT_FREQ_RATIO, nullptr, &sends);
    assert(SUCCEEDED(hr));
    if (FAILED(hr)) {
        return kInvalidSoundHandle;
    }

    XAUDIO2_BUFFER buf = {};
    buf.pAudioData = soundData.pBuffer;
    buf.AudioBytes = soundData.bufferSize;
    buf.Flags = XAUDIO2_END_OF_STREAM;
    if (loop) {
        buf.LoopCount = XAUDIO2_LOOP_INFINITE;
    }

    sourceVoice->SubmitSourceBuffer(&buf);
    sourceVoice->SetVolume(volume);
    sourceVoice->Start();

    SoundHandle handle = nextHandle++;
    if (nextHandle == kInvalidSoundHandle) {
        nextHandle = 1;
    }

    PlayingVoice voice;
    voice.sourceVoice = sourceVoice;
    voice.soundName = name;
    voice.volume = volume;
    voices.emplace(handle, voice);
    return handle;
}

SoundHandle AudioManager::PlayBGM(const std::string& name, float volume, float fadeInSeconds) {
    // 前の曲は同じ長さでフェードアウト（0なら即停止）
    StopBGM(fadeInSeconds);

    SoundHandle handle = Play(name, true, fadeInSeconds > 0.0f ? 0.0f : volume, AudioCategory::kBGM);
    if (fadeInSeconds > 0.0f) {
        if (PlayingVoice* voice = FindVoice(handle)) {
            voice->volume = volume;
            StartFade(*voice, volume, fadeInSeconds, false);
        }
    }
    currentBgm = handle;
    return handle;
}

void AudioManager::StopBGM(float fadeOutSeconds) {
    if (currentBgm != kInvalidSoundHandle) {
        Stop(currentBgm, fadeOutSeconds);
        currentBgm = kInvalidSoundHandle;
    }
}

void AudioManager::Stop(SoundHandle handle, float fadeOutSeconds) {
    PlayingVoice* voice = FindVoice(handle);
    if (!voice) {
        return;
    }
    if (fadeOutSeconds > 0.0f && !voice->isPaused) {
        StartFade(*voice, 0.0f, fadeOutSeconds, true);
    } else {
        // 実際の解放はUpdate()で行う（イテレーション中の削除を避けるため、ここでは止めるだけ）
        DestroyVoice(*voice);
    }
}

void AudioManager::Pause(SoundHandle handle) {
    PlayingVoice* voice = FindVoice(handle);
    if (voice && voice->sourceVoice && !voice->isPaused) {
        voice->sourceVoice->Stop();
        voice->isPaused = true;
    }
}

void AudioManager::Resume(SoundHandle handle) {
    PlayingVoice* voice = FindVoice(handle);
    if (voice && voice->sourceVoice && voice->isPaused) {
        voice->sourceVoice->Start();
        voice->isPaused = false;
    }
}

void AudioManager::SetVolume(SoundHandle handle, float volume) {
    PlayingVoice* voice = FindVoice(handle);
    if (voice && voice->sourceVoice) {
        voice->volume = volume;
        voice->fadeDuration = 0.0f; // 明示的な音量指定はフェードより優先
        voice->stopAfterFade = false;
        voice->sourceVoice->SetVolume(volume);
    }
}

void AudioManager::SetPitch(SoundHandle handle, float ratio) {
    PlayingVoice* voice = FindVoice(handle);
    if (voice && voice->sourceVoice) {
        voice->sourceVoice->SetFrequencyRatio((std::clamp)(ratio, XAUDIO2_MIN_FREQ_RATIO, XAUDIO2_DEFAULT_FREQ_RATIO));
    }
}

bool AudioManager::IsPlaying(SoundHandle handle) const {
    const PlayingVoice* voice = FindVoice(handle);
    return voice && voice->sourceVoice && !voice->isPaused;
}

void AudioManager::StopAll() {
    for (auto& [handle, voice] : voices) {
        DestroyVoice(voice);
    }
    currentBgm = kInvalidSoundHandle;
}

void AudioManager::PauseAll() {
    for (auto& [handle, voice] : voices) {
        Pause(handle);
    }
}

void AudioManager::ResumeAll() {
    for (auto& [handle, voice] : voices) {
        Resume(handle);
    }
}

void AudioManager::SetMasterVolume(float volume) {
    masterVolume = Clamp01(volume);
    if (IXAudio2MasteringVoice* master = AudioCommon::GetInstance()->GetMasteringVoice()) {
        master->SetVolume(masterVolume);
    }
}

void AudioManager::SetCategoryVolume(AudioCategory category, float volume) {
    size_t index = static_cast<size_t>(category);
    categoryVolumes[index] = Clamp01(volume);
    if (categoryVoices[index]) {
        categoryVoices[index]->SetVolume(categoryVolumes[index]);
    }
}

AudioManager::PlayingVoice* AudioManager::FindVoice(SoundHandle handle) {
    auto it = voices.find(handle);
    return it != voices.end() ? &it->second : nullptr;
}

const AudioManager::PlayingVoice* AudioManager::FindVoice(SoundHandle handle) const {
    auto it = voices.find(handle);
    return it != voices.end() ? &it->second : nullptr;
}

void AudioManager::StartFade(PlayingVoice& voice, float to, float duration, bool stopAfterFade) {
    if (!voice.sourceVoice) {
        return;
    }
    float current = 0.0f;
    voice.sourceVoice->GetVolume(&current);
    voice.fadeFrom = current;
    voice.fadeTo = to;
    voice.fadeDuration = duration;
    voice.fadeElapsed = 0.0f;
    voice.stopAfterFade = stopAfterFade;
}

void AudioManager::DestroyVoice(PlayingVoice& voice) {
    if (voice.sourceVoice) {
        voice.sourceVoice->Stop();
        voice.sourceVoice->DestroyVoice();
        voice.sourceVoice = nullptr;
    }
}
