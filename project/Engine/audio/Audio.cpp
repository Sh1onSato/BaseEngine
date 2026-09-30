#include "Audio.h"
#include "AudioCommon.h"
#include "StringUtility.h"
#include <fstream>
#include <cassert>
#include <vector>

// Media Foundation 関連の明示的なインクルード
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>

#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")

// チャンクヘッダ
struct ChunkHeader {
    char id[4];
    int size;
};

// RIFFヘッダチャンク
struct RiffHeader {
    ChunkHeader chunk;
    char type[4];
};

// FMTチャンク
struct FormatChunk {
    ChunkHeader chunk;
    WAVEFORMATEX fmt;
};

Audio::SoundData Audio::SoundLoadWave(const std::string& filename) {
    std::ifstream file;
    file.open(filename, std::ios_base::binary);
    assert(file.is_open());

    RiffHeader riff;
    file.read((char*)&riff, sizeof(riff));
    if (strncmp(riff.chunk.id, "RIFF", 4) != 0 || strncmp(riff.type, "WAVE", 4) != 0) {
        assert(false);
    }

    FormatChunk format = {};
    while (file.read((char*)&format.chunk, sizeof(ChunkHeader))) {
        if (strncmp(format.chunk.id, "fmt ", 4) == 0) {
            assert(format.chunk.size <= sizeof(format.fmt));
            file.read((char*)&format.fmt, format.chunk.size);
            break;
        } else {
            file.seekg(format.chunk.size, std::ios_base::cur);
        }
    }

    ChunkHeader data;
    while (file.read((char*)&data, sizeof(data))) {
        if (strncmp(data.id, "data", 4) == 0) {
            break;
        } else {
            file.seekg(data.size, std::ios_base::cur);
        }
    }

    BYTE* pBuffer = new BYTE[data.size];
    file.read((char*)pBuffer, data.size);
    file.close();

    SoundData soundData = {};
    soundData.wfex = format.fmt;
    soundData.pBuffer = pBuffer;
    soundData.bufferSize = data.size;

    return soundData;
}

Audio::SoundData Audio::SoundLoadCompressed(const std::string& filename) {
    HRESULT hr;

    // 1. SourceReaderの生成
    Microsoft::WRL::ComPtr<IMFSourceReader> pSourceReader = nullptr;
    std::wstring wFilename = StringUtility::ConvertString(filename);
    hr = MFCreateSourceReaderFromURL(wFilename.c_str(), NULL, &pSourceReader);
    assert(SUCCEEDED(hr));

    // 2. メディアタイプの選択（PCMにデコードするように設定）
    Microsoft::WRL::ComPtr<IMFMediaType> pTargetMediaType = nullptr;
    MFCreateMediaType(&pTargetMediaType);
    pTargetMediaType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
    pTargetMediaType->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
    hr = pSourceReader->SetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, NULL, pTargetMediaType.Get());
    assert(SUCCEEDED(hr));

    // 3. 最終的なフォーマットの取得
    Microsoft::WRL::ComPtr<IMFMediaType> pActualMediaType = nullptr;
    hr = pSourceReader->GetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, &pActualMediaType);
    assert(SUCCEEDED(hr));

    WAVEFORMATEX wfex = {};
    GUID subType;
    hr = pActualMediaType->GetGUID(MF_MT_SUBTYPE, &subType);
    assert(SUCCEEDED(hr));

    // PCMフォーマットとして属性を取得
    UINT32 numChannels = 0;
    UINT32 samplesPerSecond = 0;
    UINT32 bitsPerSample = 0;
    UINT32 blockAlignment = 0;
    UINT32 bytesPerSecond = 0;

    pActualMediaType->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS, &numChannels);
    pActualMediaType->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, &samplesPerSecond);
    pActualMediaType->GetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, &bitsPerSample);
    pActualMediaType->GetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, &blockAlignment);
    pActualMediaType->GetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, &bytesPerSecond);

    wfex.wFormatTag = WAVE_FORMAT_PCM;
    wfex.nChannels = (WORD)numChannels;
    wfex.nSamplesPerSec = samplesPerSecond;
    wfex.wBitsPerSample = (WORD)bitsPerSample;
    wfex.nBlockAlign = (WORD)blockAlignment;
    wfex.nAvgBytesPerSec = bytesPerSecond;
    wfex.cbSize = 0;

    // 4. データの読み込み
    std::vector<BYTE> audioData;
    while (true) {
        Microsoft::WRL::ComPtr<IMFSample> pSample = nullptr;
        DWORD dwStreamFlags = 0;
        hr = pSourceReader->ReadSample((DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, 0, NULL, &dwStreamFlags, NULL, &pSample);
        if (FAILED(hr) || (dwStreamFlags & MF_SOURCE_READERF_ENDOFSTREAM)) break;

        if (pSample) {
            Microsoft::WRL::ComPtr<IMFMediaBuffer> pBuffer = nullptr;
            pSample->ConvertToContiguousBuffer(&pBuffer);

            BYTE* pRawData = nullptr;
            DWORD dwCurrentLength = 0;
            pBuffer->Lock(&pRawData, NULL, &dwCurrentLength);

            size_t oldSize = audioData.size();
            audioData.resize(oldSize + dwCurrentLength);
            memcpy(&audioData[oldSize], pRawData, dwCurrentLength);

            pBuffer->Unlock();
        }
    }

    // 5. 戻り値の作成
    SoundData soundData = {};
    soundData.wfex = wfex;
    soundData.bufferSize = (unsigned int)audioData.size();
    soundData.pBuffer = new BYTE[soundData.bufferSize];
    memcpy(soundData.pBuffer, audioData.data(), soundData.bufferSize);

    return soundData;
}

void Audio::SoundUnload(SoundData* soundData) {
    delete[] soundData->pBuffer;
    soundData->pBuffer = nullptr;
    soundData->bufferSize = 0;
    soundData->wfex = {};
}

Audio::Voice Audio::SoundPlayWave(const SoundData& soundData, bool loop, float volume) {
    HRESULT hr;
    IXAudio2* xAudio2 = AudioCommon::GetInstance()->GetXAudio2();

    IXAudio2SourceVoice* pSourceVoice = nullptr;
    hr = xAudio2->CreateSourceVoice(&pSourceVoice, &soundData.wfex);
    assert(SUCCEEDED(hr));

    XAUDIO2_BUFFER buf = {};
    buf.pAudioData = soundData.pBuffer;
    buf.AudioBytes = soundData.bufferSize;
    buf.Flags = XAUDIO2_END_OF_STREAM;
    if (loop) {
        buf.LoopCount = XAUDIO2_LOOP_INFINITE;
    }

    pSourceVoice->SubmitSourceBuffer(&buf);
    pSourceVoice->SetVolume(volume);
    pSourceVoice->Start();

    return Voice{ pSourceVoice };
}
