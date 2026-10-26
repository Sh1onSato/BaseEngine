#include "AudioManager.h"
#include "AudioCommon.h"
#include "Calculation.h"
#include "Camera.h"
#include "CameraManager.h"
#include "Logger.h"
#include <xaudio2fx.h>
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <format>

AudioManager* AudioManager::instance = nullptr;

namespace {
    float Clamp01(float value) {
        return (std::clamp)(value, 0.0f, 1.0f);
    }

    // 1 秒あたりこれより速く動いたら、ワープ（カメラの切り替えなど）とみなしてドップラーに使わない
    constexpr float kTeleportSpeed = 150.0f;
    // こもり具合 1 のときに残る一番高い音（Hz）。0 のときは何も削らない
    constexpr float kMuffleMinCutoff = 400.0f;
    constexpr float kMuffleMaxCutoff = 20000.0f;
    // 遮り具合 1 のときに下げる音量の割合（こもるだけでなく少し小さくもなる）
    constexpr float kOcclusionVolumeDrop = 0.5f;

    // 前の位置からの速さ。dt が 0 やワープのときは 0
    Vector3 MakeVelocity(const Vector3& current, const Vector3& previous, float deltaTime) {
        if (deltaTime <= 0.0f) {
            return { 0.0f, 0.0f, 0.0f };
        }
        const Vector3 velocity = (current - previous) * (1.0f / deltaTime);
        return Calculation::Length(velocity) > kTeleportSpeed ? Vector3{ 0.0f, 0.0f, 0.0f } : velocity;
    }

    // ReverbPreset の並びと同じ順番（kOff の分は使わない）
    const XAUDIO2FX_REVERB_I3DL2_PARAMETERS kReverbPresets[] = {
        XAUDIO2FX_I3DL2_PRESET_DEFAULT,
        XAUDIO2FX_I3DL2_PRESET_SMALLROOM,
        XAUDIO2FX_I3DL2_PRESET_LARGEROOM,
        XAUDIO2FX_I3DL2_PRESET_CONCERTHALL,
        XAUDIO2FX_I3DL2_PRESET_CAVE,
        XAUDIO2FX_I3DL2_PRESET_STONECORRIDOR,
        XAUDIO2FX_I3DL2_PRESET_SEWERPIPE,
        XAUDIO2FX_I3DL2_PRESET_FOREST,
        XAUDIO2FX_I3DL2_PRESET_CITY,
        XAUDIO2FX_I3DL2_PRESET_MOUNTAINS,
        XAUDIO2FX_I3DL2_PRESET_UNDERWATER,
    };
    static_assert(std::size(kReverbPresets) == static_cast<size_t>(ReverbPreset::kCount));
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
    // 反響は ソースボイス → 反響 → カテゴリ と通すので、カテゴリは反響より後に処理する（processingStage を 1 にする）
    IXAudio2* xAudio2 = AudioCommon::GetInstance()->GetXAudio2();
    XAUDIO2_VOICE_DETAILS masterDetails{};
    AudioCommon::GetInstance()->GetMasteringVoice()->GetVoiceDetails(&masterDetails);
    outputSampleRate = masterDetails.InputSampleRate;
    for (size_t i = 0; i < static_cast<size_t>(AudioCategory::kCount); ++i) {
        HRESULT hr = xAudio2->CreateSubmixVoice(&categoryVoices[i], masterDetails.InputChannels, masterDetails.InputSampleRate,
                                                XAUDIO2_VOICE_USEFILTER, 1);
        Logger::CheckHResult(hr, std::format("AudioManager: CreateSubmixVoice(category {})", i));
        categoryVoices[i]->SetVolume(categoryVolumes[i]);
        SetCategoryMuffle(static_cast<AudioCategory>(i), categoryMuffles[i]);

        // 反響用（モノラルで受けて反響させ、カテゴリへ流す）。最初は止めておく
        IUnknown* reverbEffect = nullptr;
        hr = XAudio2CreateReverb(&reverbEffect);
        Logger::CheckHResult(hr, "AudioManager: XAudio2CreateReverb");
        XAUDIO2_EFFECT_DESCRIPTOR effectDescriptor{ reverbEffect, FALSE, 1 };
        XAUDIO2_EFFECT_CHAIN effectChain{ 1, &effectDescriptor };
        XAUDIO2_SEND_DESCRIPTOR toCategory{ 0, categoryVoices[i] };
        XAUDIO2_VOICE_SENDS sends{ 1, &toCategory };
        hr = xAudio2->CreateSubmixVoice(&reverbVoices[i], 1, masterDetails.InputSampleRate, 0, 0, &sends, &effectChain);
        Logger::CheckHResult(hr, std::format("AudioManager: CreateSubmixVoice(reverb {})", i));
        // ボイスが効果を持つので、こちらの参照は手放す
        reverbEffect->Release();
    }
    SetReverb(reverbPreset);
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

    // 反響はカテゴリへ流しているので、カテゴリより先に破棄する
    for (auto& reverbVoice : reverbVoices) {
        if (reverbVoice) {
            reverbVoice->DestroyVoice();
            reverbVoice = nullptr;
        }
    }
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
    // ドップラー用に、前の Update からの動きで速さを求める
    listenerVelocity = hasPreviousListenerPosition ? MakeVelocity(listenerPosition, previousListenerPosition, deltaTime)
                                                   : Vector3{ 0.0f, 0.0f, 0.0f };
    previousListenerPosition = listenerPosition;
    hasPreviousListenerPosition = true;

    for (auto it = voices.begin(); it != voices.end(); ) {
        PlayingVoice& voice = it->second;

        // 3D の音は、リスナーや音の位置が動くので毎フレーム左右・距離の音量を計算し直す
        if (voice.is3D && voice.sourceVoice) {
            voice.velocity = MakeVelocity(voice.position, voice.previousPosition, deltaTime);
            voice.previousPosition = voice.position;
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

    // 出力先をカテゴリのサブミックスボイス（こもらせるフィルター付き）と、反響用のサブミックスボイスにする
    const size_t categoryIndex = static_cast<size_t>(category);
    XAUDIO2_SEND_DESCRIPTOR sendDescriptors[2] = {
        { XAUDIO2_SEND_USEFILTER, categoryVoices[categoryIndex] },
        { 0, reverbVoices[categoryIndex] },
    };
    XAUDIO2_VOICE_SENDS sends{ 2, sendDescriptors };

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
    // 反響へ送る量（3D でない音は既定で 0。送らないと XAudio2 の既定の行列でそのまま流れてしまう）
    ApplyReverbSend(voice);
    voices.emplace(handle, voice);
    return handle;
}

SoundHandle AudioManager::Play3D(const std::string& name, const Vector3& position, bool loop, float volume,
                                 float minDistance, float maxDistance, AudioCategory category) {
    const SoundHandle handle = Play(name, loop, volume, category);
    if (PlayingVoice* voice = FindVoice(handle)) {
        voice->is3D = true;
        voice->position = position;
        voice->previousPosition = position;
        voice->minDistance = minDistance;
        voice->maxDistance = maxDistance;
        voice->reverbSend = 1.0f;
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

float AudioManager::GetFrequencyRatio(SoundHandle handle) const {
    const PlayingVoice* voice = FindVoice(handle);
    if (!voice || !voice->sourceVoice) {
        return 1.0f;
    }
    float ratio = 1.0f;
    voice->sourceVoice->GetFrequencyRatio(&ratio);
    return ratio;
}

void AudioManager::SetOcclusion(SoundHandle handle, float amount) {
    PlayingVoice* voice = FindVoice(handle);
    if (!voice || !voice->sourceVoice) {
        return;
    }
    amount = Clamp01(amount);
    if (voice->occlusion == amount) {
        return; // 毎フレーム同じ値で呼ばれることが多いので、変わったときだけ設定し直す
    }
    voice->occlusion = amount;
    ApplyOcclusionFilter(*voice);
    // 音量を下げる分は出力の行列に入れる
    voice->is3D ? Apply3D(*voice) : ApplyPan(*voice);
}

void AudioManager::SetReverb(ReverbPreset preset) {
    reverbPreset = preset;
    for (IXAudio2SubmixVoice* reverbVoice : reverbVoices) {
        if (!reverbVoice) {
            continue;
        }
        if (preset == ReverbPreset::kOff) {
            // 効果を止めると入ってきた音がそのまま流れるので、音量も 0 にする
            reverbVoice->DisableEffect(0);
            reverbVoice->SetVolume(0.0f);
            continue;
        }
        XAUDIO2FX_REVERB_PARAMETERS native{};
        ReverbConvertI3DL2ToNative(&kReverbPresets[static_cast<size_t>(preset)], &native);
        reverbVoice->SetEffectParameters(0, &native, sizeof(native));
        reverbVoice->EnableEffect(0);
        reverbVoice->SetVolume(1.0f);
    }
}

const char* AudioManager::GetReverbName(ReverbPreset preset) {
    switch (preset) {
    case ReverbPreset::kOff: return "なし";
    case ReverbPreset::kSmallRoom: return "小さい部屋";
    case ReverbPreset::kLargeRoom: return "広い部屋";
    case ReverbPreset::kHall: return "ホール";
    case ReverbPreset::kCave: return "洞窟";
    case ReverbPreset::kStoneCorridor: return "石の廊下";
    case ReverbPreset::kSewerPipe: return "下水管";
    case ReverbPreset::kForest: return "森";
    case ReverbPreset::kCity: return "街中";
    case ReverbPreset::kMountains: return "山";
    case ReverbPreset::kUnderwater: return "水中";
    default: return "?";
    }
}

void AudioManager::SetReverbSend(SoundHandle handle, float amount) {
    PlayingVoice* voice = FindVoice(handle);
    if (voice && voice->sourceVoice) {
        voice->reverbSend = Clamp01(amount);
        ApplyReverbSend(*voice);
    }
}

void AudioManager::SetCategoryMuffle(AudioCategory category, float amount) {
    const size_t index = static_cast<size_t>(category);
    categoryMuffles[index] = Clamp01(amount);
    if (categoryVoices[index]) {
        const XAUDIO2_FILTER_PARAMETERS filter = MakeMuffleFilter(categoryMuffles[index]);
        categoryVoices[index]->SetFilterParameters(&filter);
    }
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
    listener.Velocity = { listenerVelocity.x, listenerVelocity.y, listenerVelocity.z };

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
    emitter.Velocity = { voice.velocity.x, voice.velocity.y, voice.velocity.z };
    emitter.OrientFront = { 0.0f, 0.0f, 1.0f };
    emitter.OrientTop = { 0.0f, 1.0f, 0.0f };
    emitter.ChannelCount = sourceChannels;
    emitter.pChannelAzimuths = azimuths; // ステレオの音も1点から鳴らす
    emitter.CurveDistanceScaler = maxDistance;
    emitter.pVolumeCurve = &curve;
    emitter.DopplerScaler = dopplerScale;

    float matrix[kMaxMatrixChannels * kMaxMatrixChannels] = {};
    X3DAUDIO_DSP_SETTINGS dsp{};
    dsp.SrcChannelCount = sourceChannels;
    dsp.DstChannelCount = destinationChannels;
    dsp.pMatrixCoefficients = matrix;
    X3DAudioCalculate(x3dAudio, &listener, &emitter, X3DAUDIO_CALCULATE_MATRIX | X3DAUDIO_CALCULATE_DOPPLER, &dsp);
    // 遮られている分だけ小さくする
    const float occlusionGain = 1.0f - kOcclusionVolumeDrop * voice.occlusion;
    for (float& gain : matrix) {
        gain *= occlusionGain;
    }
    SetOutputMatrix(voice, matrix);
    ApplyReverbSend(voice);
    voice.dopplerFactor = dopplerScale > 0.0f ? dsp.DopplerFactor : 1.0f;
    ApplyFrequency(voice);
}

void AudioManager::ApplyOcclusionFilter(PlayingVoice& voice) {
    const XAUDIO2_FILTER_PARAMETERS filter = MakeMuffleFilter(voice.occlusion);
    voice.sourceVoice->SetOutputFilterParameters(categoryVoices[static_cast<size_t>(voice.category)], &filter);
}

void AudioManager::ApplyReverbSend(PlayingVoice& voice) {
    float level = voice.reverbSend;
    if (voice.is3D) {
        // 遠いほど反響も小さくする（maxDistance で 0。聞こえない音の反響だけ残らないように）
        const float distance = Calculation::Length(voice.position - listenerPosition);
        level *= 1.0f - Clamp01(distance / (std::max)(voice.maxDistance, 1.0e-3f));
    }
    // 反響用のサブミックスはモノラルなので、全チャンネルを混ぜて送る
    const uint32_t sourceChannels = (std::min)(voice.sourceChannels, kMaxMatrixChannels);
    float matrix[kMaxMatrixChannels] = {};
    for (uint32_t s = 0; s < sourceChannels; ++s) {
        matrix[s] = level / static_cast<float>(sourceChannels);
    }
    voice.sourceVoice->SetOutputMatrix(reverbVoices[static_cast<size_t>(voice.category)], sourceChannels, 1, matrix);
}

void AudioManager::ApplyFrequency(PlayingVoice& voice) {
    const float ratio = voice.pitch * voice.dopplerFactor;
    voice.sourceVoice->SetFrequencyRatio((std::clamp)(ratio, XAUDIO2_MIN_FREQ_RATIO, XAUDIO2_DEFAULT_FREQ_RATIO));
}

XAUDIO2_FILTER_PARAMETERS AudioManager::MakeMuffleFilter(float amount) const {
    // 0 で 20000Hz（削らない）、1 で kMuffleMinCutoff。耳の感じ方に合わせて、Hz は掛け算で下げていく
    const float cutoff = kMuffleMaxCutoff * std::pow(kMuffleMinCutoff / kMuffleMaxCutoff, Clamp01(amount));
    // XAudio2 のフィルターの値は 2 * sin(π * Hz / サンプリング周波数)（xaudio2.h の XAudio2CutoffFrequencyToRadians と同じ式）。
    // XAUDIO2_MAX_FILTER_FREQUENCY（1.0）で、OneOverQ が 1.0 のときはフィルターが効かない
    constexpr float kPi = 3.14159265f;
    const float sampleRate = static_cast<float>(outputSampleRate);
    const float frequency = cutoff * 6.0f >= sampleRate ? XAUDIO2_MAX_FILTER_FREQUENCY : 2.0f * std::sin(kPi * cutoff / sampleRate);
    return { LowPassFilter, frequency, 1.0f };
}

void AudioManager::ApplyPan(PlayingVoice& voice) {
    const uint32_t sourceChannels = (std::min)(voice.sourceChannels, kMaxMatrixChannels);
    const uint32_t destinationChannels = (std::min)(outputChannels, kMaxMatrixChannels);
    float matrix[kMaxMatrixChannels * kMaxMatrixChannels] = {};
    // 出力 D・入力 S の音量は matrix[入力の数 * D + S]（XAudio2 の並び）
    // 遮られている分だけ全体を小さくする
    const float occlusionGain = 1.0f - kOcclusionVolumeDrop * voice.occlusion;
    if (destinationChannels < 2) {
        for (uint32_t s = 0; s < sourceChannels; ++s) {
            matrix[s] = occlusionGain;
        }
    } else {
        // 真ん中で左右とも 1、片側へ寄せるほど反対側を小さくする
        const float leftGain = (std::min)(1.0f, 1.0f - voice.pan) * occlusionGain;
        const float rightGain = (std::min)(1.0f, 1.0f + voice.pan) * occlusionGain;
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
        voice->pitch = ratio;
        ApplyFrequency(*voice);
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
