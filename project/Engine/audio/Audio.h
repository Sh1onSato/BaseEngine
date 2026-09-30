#pragma once
#include <xaudio2.h>
#include <string>
#include <vector>

class Audio {
public:
    // 音声データ構造体
    struct SoundData {
        WAVEFORMATEX wfex;      // 波形フォーマット
        BYTE* pBuffer;          // バッファ
        unsigned int bufferSize;// バッファサイズ
    };

    // 音声データの読み込み
    static SoundData SoundLoadWave(const std::string& filename);

    // 圧縮音声データの読み込み (MP3など)
    static SoundData SoundLoadCompressed(const std::string& filename);

    // 音声データの解放
    static void SoundUnload(SoundData* soundData);

    // 再生用の一時的なハンドルクラス（将来的に停止や音量調節ができるように）
    struct Voice {
        IXAudio2SourceVoice* pSourceVoice = nullptr;
    };

    // 音声再生
    static Voice SoundPlayWave(const SoundData& soundData, bool loop = false, float volume = 1.0f);
};
