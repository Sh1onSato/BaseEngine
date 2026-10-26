#pragma once
#include "Audio.h"
#include "Structs.h"
#include <x3daudio.h>
#include <cstdint>
#include <map>
#include <string>
#include <unordered_map>

// 音量カテゴリ（カテゴリごとにサブミックスボイスを通すので、設定画面の「BGM音量」「SE音量」をまとめて変えられる）
enum class AudioCategory {
    kBGM,
    kSE,
    kCount,
};

// 再生中の音を後から操作するためのハンドル。0は無効値
using SoundHandle = uint32_t;
constexpr SoundHandle kInvalidSoundHandle = 0;

class AudioManager {
public:
    static AudioManager* GetInstance();

    // 初期化
    void Initialize();

    // 終了処理
    void Finalize();

    // 毎フレームの更新（再生終了したボイスの解放とフェード処理）。Framework::Update()から呼ばれる
    void Update(float deltaTime);

    // 音声ファイルの読み込み（wavはそのまま、それ以外(mp3等)はMedia Foundationでデコード）
    void Load(const std::string& name, const std::string& filePath);
    // 音声データの破棄（そのデータを再生中のボイスは停止される）
    void Unload(const std::string& name);

    // 再生。戻り値のハンドルでStop/Pause/SetVolume等ができる（使わないなら捨ててよい。終了後は自動解放される）
    SoundHandle Play(const std::string& name, bool loop = false, float volume = 1.0f, AudioCategory category = AudioCategory::kSE);

    // BGM再生（BGMは同時に1曲だけ。再生中の曲があればフェードアウトさせて切り替える）
    SoundHandle PlayBGM(const std::string& name, float volume = 1.0f, float fadeInSeconds = 0.0f);
    void StopBGM(float fadeOutSeconds = 0.0f);
    SoundHandle GetCurrentBGM() const { return currentBgm; }

    // --- ハンドル単位の操作（既に終了・無効なハンドルを渡しても何もしない） ---
    void Stop(SoundHandle handle, float fadeOutSeconds = 0.0f);
    void Pause(SoundHandle handle);
    void Resume(SoundHandle handle);
    void SetVolume(SoundHandle handle, float volume);
    // 再生速度（=音程）の倍率。1.0が等倍（ボイス作成時の上限により最大2.0）
    void SetPitch(SoundHandle handle, float ratio);
    // 左右の振り分け（-1 で左だけ、0 で真ん中、1 で右だけ）。2D のゲームで画面の左右に合わせる用。3D の音には効かない
    void SetPan(SoundHandle handle, float pan);
    bool IsPlaying(SoundHandle handle) const;

    // --- 3D の音（聞く人＝リスナーからの向きで左右に振り分け、遠いほど小さくする） ---
    // position で鳴らす。minDistance までは元の音量、そこから maxDistance に向かって小さくなり、maxDistance より遠いと聞こえない
    SoundHandle Play3D(const std::string& name, const Vector3& position, bool loop = false, float volume = 1.0f,
                       float minDistance = 1.0f, float maxDistance = 30.0f, AudioCategory category = AudioCategory::kSE);
    // 鳴っている 3D の音の位置を変える（動く物に付けた音は毎フレーム呼ぶ）
    void SetSoundPosition(SoundHandle handle, const Vector3& position);
    void SetSoundRange(SoundHandle handle, float minDistance, float maxDistance);

    // リスナー（耳）の位置と向き。既定ではアクティブなカメラ（CameraManager）に毎フレーム付いていく。
    // SetListener を呼ぶとカメラに付いていくのをやめる（キャラクターの位置で聞きたいときなど）。
    // 2D（y が下向き）なら SetListener({ 画面の中心 x, y, -1 }, { 0, 0, 1 }, { 0, -1, 0 }) のようにして Play3D に x, y, 0 を渡す
    void SetListener(const Vector3& position, const Vector3& forward, const Vector3& up);
    void SetListenerFollowCamera(bool follow) { isListenerFollowingCamera = follow; }
    bool IsListenerFollowingCamera() const { return isListenerFollowingCamera; }
    const Vector3& GetListenerPosition() const { return listenerPosition; }

    // 3D の音・左右の振り分けを付けた音の、今の左右の音量（確認表示用。距離の減衰を含む。無ければ false）
    bool GetOutputGains(SoundHandle handle, float& outLeft, float& outRight) const;

    // 全ての音を停止（シーン遷移時など）
    void StopAll();
    // 全ての音を一時停止／再開（ポーズメニュー用）
    void PauseAll();
    void ResumeAll();

    // --- 音量設定（0.0〜1.0） ---
    void SetMasterVolume(float volume);
    float GetMasterVolume() const { return masterVolume; }
    void SetCategoryVolume(AudioCategory category, float volume);
    float GetCategoryVolume(AudioCategory category) const { return categoryVolumes[static_cast<size_t>(category)]; }

private:
    AudioManager() = default;
    ~AudioManager() = default;
    AudioManager(const AudioManager&) = delete;
    AudioManager& operator=(const AudioManager&) = delete;

    struct PlayingVoice {
        IXAudio2SourceVoice* sourceVoice = nullptr;
        std::string soundName;
        float volume = 1.0f;       // 呼び出し側が指定した音量（フェードの目標値）
        float fadeFrom = 0.0f;
        float fadeTo = 0.0f;
        float fadeDuration = 0.0f; // 0ならフェード中ではない
        float fadeElapsed = 0.0f;
        bool stopAfterFade = false;
        bool isPaused = false;

        AudioCategory category = AudioCategory::kSE;
        uint32_t sourceChannels = 1;
        // 3D の音
        bool is3D = false;
        Vector3 position = { 0.0f, 0.0f, 0.0f };
        float minDistance = 1.0f;
        float maxDistance = 30.0f;
        // 左右の振り分け（3D でない音）
        bool hasPan = false;
        float pan = 0.0f;
        // 最後に設定した左右の音量（確認表示用）
        float leftGain = 1.0f;
        float rightGain = 1.0f;
    };

    PlayingVoice* FindVoice(SoundHandle handle);
    const PlayingVoice* FindVoice(SoundHandle handle) const;
    void StartFade(PlayingVoice& voice, float to, float duration, bool stopAfterFade);
    void DestroyVoice(PlayingVoice& voice);
    // アクティブなカメラの位置と向きをリスナーにする
    void UpdateListenerFromCamera();
    // 3D の音の左右・距離の音量を X3DAudio で計算して、出力の行列に入れる
    void Apply3D(PlayingVoice& voice);
    // 左右の振り分けを出力の行列に入れる
    void ApplyPan(PlayingVoice& voice);
    // 出力の行列を設定し、確認表示用に左右の音量を覚える
    void SetOutputMatrix(PlayingVoice& voice, const float* matrix);

    static AudioManager* instance;
    std::map<std::string, Audio::SoundData> soundMap;

    std::unordered_map<SoundHandle, PlayingVoice> voices;
    SoundHandle nextHandle = 1;
    SoundHandle currentBgm = kInvalidSoundHandle;

    IXAudio2SubmixVoice* categoryVoices[static_cast<size_t>(AudioCategory::kCount)] = {};
    float categoryVolumes[static_cast<size_t>(AudioCategory::kCount)] = { 1.0f, 1.0f };
    float masterVolume = 1.0f;

    // 3D の音
    // 出力の行列で扱うチャンネル数の上限（7.1 = 8）
    static constexpr uint32_t kMaxMatrixChannels = 8;
    X3DAUDIO_HANDLE x3dAudio = {};
    uint32_t outputChannels = 2; // カテゴリのサブミックス（= マスター）のチャンネル数
    bool isListenerFollowingCamera = true;
    Vector3 listenerPosition = { 0.0f, 0.0f, 0.0f };
    Vector3 listenerForward = { 0.0f, 0.0f, 1.0f };
    Vector3 listenerUp = { 0.0f, 1.0f, 0.0f };
};
