#include "AudioCommon.h"
#include "Logger.h"
#include <cassert>

AudioCommon* AudioCommon::instance = nullptr;

AudioCommon* AudioCommon::GetInstance() {
    if (instance == nullptr) {
        instance = new AudioCommon();
    }
    return instance;
}

void AudioCommon::Initialize() {
    // Media Foundationの初期化
    HRESULT hr = MFStartup(MF_VERSION, MFSTARTUP_NOSOCKET);
    Logger::CheckHResult(hr, "AudioCommon: MFStartup");

    hr = XAudio2Create(&xAudio2, 0, XAUDIO2_DEFAULT_PROCESSOR);
    Logger::CheckHResult(hr, "AudioCommon: XAudio2Create");

    hr = xAudio2->CreateMasteringVoice(&masteringVoice);
    Logger::CheckHResult(hr, "AudioCommon: CreateMasteringVoice");
}

void AudioCommon::Finalize() {
    if (masteringVoice) {
        masteringVoice->DestroyVoice();
        masteringVoice = nullptr;
    }
    xAudio2.Reset();

    // Media Foundationの終了処理
    MFShutdown();

    delete instance;
    instance = nullptr;
}
