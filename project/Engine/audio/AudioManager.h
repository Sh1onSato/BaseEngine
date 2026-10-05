#pragma once
#include "Audio.h"
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
    bool IsPlaying(SoundHandle handle) const;

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
    };

    PlayingVoice* FindVoice(SoundHandle handle);
    const PlayingVoice* FindVoice(SoundHandle handle) const;
    void StartFade(PlayingVoice& voice, float to, float duration, bool stopAfterFade);
    void DestroyVoice(PlayingVoice& voice);

    static AudioManager* instance;
    std::map<std::string, Audio::SoundData> soundMap;

    std::unordered_map<SoundHandle, PlayingVoice> voices;
    SoundHandle nextHandle = 1;
    SoundHandle currentBgm = kInvalidSoundHandle;

    IXAudio2SubmixVoice* categoryVoices[static_cast<size_t>(AudioCategory::kCount)] = {};
    float categoryVolumes[static_cast<size_t>(AudioCategory::kCount)] = { 1.0f, 1.0f };
    float masterVolume = 1.0f;
};
