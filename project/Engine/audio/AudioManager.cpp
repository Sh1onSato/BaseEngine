#include "AudioManager.h"
#include "AudioCommon.h"
#include <cassert>

AudioManager* AudioManager::instance = nullptr;

AudioManager* AudioManager::GetInstance() {
    if (instance == nullptr) {
        instance = new AudioManager();
    }
    return instance;
}

void AudioManager::Initialize() {
    AudioCommon::GetInstance()->Initialize();
}

void AudioManager::Finalize() {
    for (auto& pair : soundMap) {
        Audio::SoundUnload(&pair.second);
    }
    soundMap.clear();
    
    AudioCommon::GetInstance()->Finalize();
    
    delete instance;
    instance = nullptr;
}

void AudioManager::Load(const std::string& name, const std::string& filePath) {
    if (soundMap.find(name) != soundMap.end()) {
        return;
    }

    Audio::SoundData data;
    // 拡張子を取得して判別
    std::string ext = filePath.substr(filePath.find_last_of(".") + 1);
    if (ext == "wav") {
        data = Audio::SoundLoadWave(filePath);
    } else {
        data = Audio::SoundLoadCompressed(filePath);
    }
    
    soundMap[name] = data;
}

void AudioManager::Play(const std::string& name, bool loop, float volume) {
    auto it = soundMap.find(name);
    assert(it != soundMap.end());

    Audio::SoundPlayWave(it->second, loop, volume);
}
