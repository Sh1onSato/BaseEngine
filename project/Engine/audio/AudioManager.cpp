#include "AudioManager.h"
#include "AudioCommon.h"
#include "Calculation.h"
#include "Camera.h"
#include "CameraManager.h"
#include "Logger.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
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
        Logger::CheckHResult(hr, std::format("AudioManager: CreateSubmixVoice(category {})", i));
        categoryVoices[i]->SetVolume(categoryVolumes[i]);
    }
    AudioCommon::GetInstance()->GetMasteringVoice()->SetVolume(masterVolume);

    // 3D の音の計算（X3DAudio）。スピーカーの並び（ステレオ・5.1 など）はマスターに合わせる
    outputChannels = masterDetails.InputChannels;
    DWORD channelMask = 0;
    HRESULT hr = AudioCommon::GetInstance()->GetMasteringVoice()->GetChannelMask(&channelMask);
    Logger::CheckHResult(hr, "AudioManager: GetChannelMask");
    hr = X3DAudioInitialize(channelMask, X3DAUDIO_SPEED_OF_SOUND, x3dAudio);
    Logger::CheckHResult(hr, "AudioManager: X3DAudioInitialize");
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
    if (isListenerFollowingCamera) {
        UpdateListenerFromCamera();
    }
    for (auto it = voices.begin(); it != voices.end(); ) {
        PlayingVoice& voice = it->second;

        // 3D の音は、リスナーや音の位置が動くので毎フレーム左右・距離の音量を計算し直す
        if (voice.is3D && voice.sourceVoice) {
            Apply3D(voice);
        }

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
    if (FAILED(hr)) {
        // 再生できなくてもゲームは続行させる（無音になるだけ）
        Logger::Log(std::format("AudioManager: CreateSourceVoice failed. HRESULT=0x{:08X}\n", static_cast<uint32_t>(hr)));
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
    voice.category = category;
    voice.sourceChannels = (std::max)(1u, static_cast<uint32_t>(soundData.wfex.nChannels));
    voices.emplace(handle, voice);
    return handle;
}

SoundHandle AudioManager::Play3D(const std::string& name, const Vector3& position, bool loop, float volume,
                                 float minDistance, float maxDistance, AudioCategory category) {
    const SoundHandle handle = Play(name, loop, volume, category);
    if (PlayingVoice* voice = FindVoice(handle)) {
        voice->is3D = true;
        voice->position = position;
        voice->minDistance = minDistance;
        voice->maxDistance = maxDistance;
        // 鳴り始めから正しい左右・音量にする（次の Update を待たない）
        Apply3D(*voice);
    }
    return handle;
}

void AudioManager::SetSoundPosition(SoundHandle handle, const Vector3& position) {
    PlayingVoice* voice = FindVoice(handle);
    if (voice && voice->sourceVoice && voice->is3D) {
        voice->position = position;
        Apply3D(*voice);
    }
}

void AudioManager::SetSoundRange(SoundHandle handle, float minDistance, float maxDistance) {
    PlayingVoice* voice = FindVoice(handle);
    if (voice && voice->sourceVoice && voice->is3D) {
        voice->minDistance = minDistance;
        voice->maxDistance = maxDistance;
        Apply3D(*voice);
    }
}

void AudioManager::SetPan(SoundHandle handle, float pan) {
    PlayingVoice* voice = FindVoice(handle);
    if (voice && voice->sourceVoice && !voice->is3D) {
        voice->hasPan = true;
        voice->pan = (std::clamp)(pan, -1.0f, 1.0f);
        ApplyPan(*voice);
    }
}

void AudioManager::SetListener(const Vector3& position, const Vector3& forward, const Vector3& up) {
    isListenerFollowingCamera = false;
    listenerPosition = position;
    // X3DAudio は前と上が直角・長さ 1 でないと正しく計算できないので整える
    listenerForward = Calculation::Normalize(forward);
    listenerUp = Calculation::Normalize(up - listenerForward * Calculation::Dot(up, listenerForward));
}

bool AudioManager::GetOutputGains(SoundHandle handle, float& outLeft, float& outRight) const {
    const PlayingVoice* voice = FindVoice(handle);
    if (!voice || !voice->sourceVoice || (!voice->is3D && !voice->hasPan)) {
        return false;
    }
    outLeft = voice->leftGain;
    outRight = voice->rightGain;
    return true;
}

void AudioManager::UpdateListenerFromCamera() {
    const Camera* camera = CameraManager::GetInstance()->GetActiveCamera();
    if (!camera) {
        return;
    }
    // カメラのワールド行列の行（Camera::Update と同じ作り方）: 1行目が上、2行目が前
    const Transform& transform = camera->GetTransform();
    const Matrix4x4 world = Calculation::MakeAffineMatrix({ 1.0f, 1.0f, 1.0f }, transform.rotate, transform.translate);
    listenerPosition = transform.translate;
    listenerUp = Calculation::Normalize(Vector3{ world.m[1][0], world.m[1][1], world.m[1][2] });
    listenerForward = Calculation::Normalize(Vector3{ world.m[2][0], world.m[2][1], world.m[2][2] });
}

void AudioManager::Apply3D(PlayingVoice& voice) {
    const uint32_t sourceChannels = (std::min)(voice.sourceChannels, kMaxMatrixChannels);
    const uint32_t destinationChannels = (std::min)(outputChannels, kMaxMatrixChannels);

    X3DAUDIO_LISTENER listener{};
    listener.Position = { listenerPosition.x, listenerPosition.y, listenerPosition.z };
    listener.OrientFront = { listenerForward.x, listenerForward.y, listenerForward.z };
    listener.OrientTop = { listenerUp.x, listenerUp.y, listenerUp.z };

    // 距離の音量: minDistance までは 1、maxDistance で 0 へ真っすぐ下がる（距離は maxDistance で割った 0〜1 で表す）
    const float maxDistance = (std::max)(voice.maxDistance, 1.0e-3f);
    const float innerRatio = std::clamp(voice.minDistance / maxDistance, 0.0f, 0.999f);
    X3DAUDIO_DISTANCE_CURVE_POINT points[3] = { { 0.0f, 1.0f }, { innerRatio, 1.0f }, { 1.0f, 0.0f } };
    X3DAUDIO_DISTANCE_CURVE curve{};
    // minDistance が 0 なら最初の2点が同じ距離になるので、2点だけにする
    curve.pPoints = innerRatio > 0.0f ? points : points + 1;
    curve.PointCount = innerRatio > 0.0f ? 3 : 2;
    if (innerRatio <= 0.0f) {
        points[1].Distance = 0.0f;
    }

    float azimuths[kMaxMatrixChannels] = {};
    X3DAUDIO_EMITTER emitter{};
    emitter.Position = { voice.position.x, voice.position.y, voice.position.z };
    emitter.OrientFront = { 0.0f, 0.0f, 1.0f };
    emitter.OrientTop = { 0.0f, 1.0f, 0.0f };
    emitter.ChannelCount = sourceChannels;
    emitter.pChannelAzimuths = azimuths; // ステレオの音も1点から鳴らす
    emitter.CurveDistanceScaler = maxDistance;
    emitter.pVolumeCurve = &curve;

    float matrix[kMaxMatrixChannels * kMaxMatrixChannels] = {};
    X3DAUDIO_DSP_SETTINGS dsp{};
    dsp.SrcChannelCount = sourceChannels;
    dsp.DstChannelCount = destinationChannels;
    dsp.pMatrixCoefficients = matrix;
    X3DAudioCalculate(x3dAudio, &listener, &emitter, X3DAUDIO_CALCULATE_MATRIX, &dsp);
    SetOutputMatrix(voice, matrix);
}

void AudioManager::ApplyPan(PlayingVoice& voice) {
    const uint32_t sourceChannels = (std::min)(voice.sourceChannels, kMaxMatrixChannels);
    const uint32_t destinationChannels = (std::min)(outputChannels, kMaxMatrixChannels);
    float matrix[kMaxMatrixChannels * kMaxMatrixChannels] = {};
    // 出力 D・入力 S の音量は matrix[入力の数 * D + S]（XAudio2 の並び）
    if (destinationChannels < 2) {
        for (uint32_t s = 0; s < sourceChannels; ++s) {
            matrix[s] = 1.0f;
        }
    } else {
        // 真ん中で左右とも 1、片側へ寄せるほど反対側を小さくする
        const float leftGain = (std::min)(1.0f, 1.0f - voice.pan);
        const float rightGain = (std::min)(1.0f, 1.0f + voice.pan);
        if (sourceChannels == 1) {
            matrix[0] = leftGain;
            matrix[1] = rightGain;
        } else {
            // ステレオ以上の音: 左は左へ、右は右へ（3つ目以降のチャンネルは同じ番号の出力へそのまま）
            for (uint32_t s = 0; s < sourceChannels && s < destinationChannels; ++s) {
                const float gain = s == 0 ? leftGain : (s == 1 ? rightGain : 1.0f);
                matrix[sourceChannels * s + s] = gain;
            }
        }
    }
    SetOutputMatrix(voice, matrix);
}

void AudioManager::SetOutputMatrix(PlayingVoice& voice, const float* matrix) {
    const uint32_t sourceChannels = (std::min)(voice.sourceChannels, kMaxMatrixChannels);
    const uint32_t destinationChannels = (std::min)(outputChannels, kMaxMatrixChannels);
    voice.sourceVoice->SetOutputMatrix(categoryVoices[static_cast<size_t>(voice.category)], sourceChannels, destinationChannels, matrix);
    // 確認表示用: 左（出力 0）と右（出力 1）に一番大きく入っている音量
    voice.leftGain = 0.0f;
    voice.rightGain = 0.0f;
    for (uint32_t s = 0; s < sourceChannels; ++s) {
        voice.leftGain = (std::max)(voice.leftGain, matrix[s]);
        if (destinationChannels >= 2) {
            voice.rightGain = (std::max)(voice.rightGain, matrix[sourceChannels + s]);
        }
    }
    if (destinationChannels < 2) {
        voice.rightGain = voice.leftGain;
    }
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
