#include "AudioCommon.h"
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
    assert(SUCCEEDED(hr));

    hr = XAudio2Create(&xAudio2, 0, XAUDIO2_DEFAULT_PROCESSOR);
    assert(SUCCEEDED(hr));

    hr = xAudio2->CreateMasteringVoice(&masteringVoice);
    assert(SUCCEEDED(hr));
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
