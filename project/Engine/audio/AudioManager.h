#pragma once
#include "Audio.h"
#include <map>
#include <string>

class AudioManager {
public:
    static AudioManager* GetInstance();

    // 初期化
    void Initialize();

    // 終了処理
    void Finalize();

    // 音声ファイルの読み込み
    void Load(const std::string& name, const std::string& filePath);

    // 再生
    void Play(const std::string& name, bool loop = false, float volume = 1.0f);

private:
    AudioManager() = default;
    ~AudioManager() = default;
    AudioManager(const AudioManager&) = delete;
    AudioManager& operator=(const AudioManager&) = delete;

    static AudioManager* instance;
    std::map<std::string, Audio::SoundData> soundMap;
};
