#pragma once
#include <xaudio2.h>
#include <wrl.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>

#pragma comment(lib, "xaudio2.lib")
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")

class AudioCommon {
public:
    static AudioCommon* GetInstance();

    // 初期化
    void Initialize();

    // 終了処理
    void Finalize();

    // ゲッター
    IXAudio2* GetXAudio2() const { return xAudio2.Get(); }

private:
    AudioCommon() = default;
    ~AudioCommon() = default;
    AudioCommon(const AudioCommon&) = delete;
    AudioCommon& operator=(const AudioCommon&) = delete;

    static AudioCommon* instance;
    Microsoft::WRL::ComPtr<IXAudio2> xAudio2;
    IXAudio2MasteringVoice* masteringVoice = nullptr;
};
