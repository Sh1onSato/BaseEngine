<#
GenerateTestSounds.ps1

AudioManager（Engine\audio\AudioManager.h/.cpp）の動作確認用に、外部ライブラリ無し（PowerShell標準機能のみ）で
テスト用のwav（16bit PCM モノラル 44100Hz）を3種類生成するスクリプト。EngineTestSceneで使う。
- test_se.wav    : 0.15秒の「ピッ」（880Hz、減衰あり）。SE再生・連打の確認用
- test_bgm_a.wav : 4秒の和音（220/275/330Hz）。ループの継ぎ目でプツッと鳴らないよう、全成分が4秒でちょうど整数周期になる周波数にしてある
- test_bgm_b.wav : 4秒の別の和音（196/247/294Hz）に2Hzの音量の揺れを付けたもの。BGM切り替え(クロスフェード)で違いが分かるようにしてある

このスクリプトはビルド対象外（.vcxprojに登録しない）。実行はユーザーまたは開発者が手動で行う。
    powershell -ExecutionPolicy Bypass -File .\Resources\sounds\GenerateTestSounds.ps1
#>

$ErrorActionPreference = 'Stop'
$sampleRate = 44100
$outDir = $PSScriptRoot

function Write-Wav([string]$path, [double[]]$samples) {
    $dataSize = $samples.Length * 2
    $stream = [System.IO.File]::Create($path)
    $writer = New-Object System.IO.BinaryWriter($stream)
    try {
        $writer.Write([System.Text.Encoding]::ASCII.GetBytes('RIFF'))
        $writer.Write([int](36 + $dataSize))
        $writer.Write([System.Text.Encoding]::ASCII.GetBytes('WAVE'))
        $writer.Write([System.Text.Encoding]::ASCII.GetBytes('fmt '))
        $writer.Write([int]16)                 # fmtチャンクのサイズ（WAVEFORMATEXのcbSize無し版）
        $writer.Write([int16]1)                # PCM
        $writer.Write([int16]1)                # モノラル
        $writer.Write([int]$sampleRate)
        $writer.Write([int]($sampleRate * 2))  # 1秒あたりのバイト数
        $writer.Write([int16]2)                # ブロックサイズ
        $writer.Write([int16]16)               # ビット深度
        $writer.Write([System.Text.Encoding]::ASCII.GetBytes('data'))
        $writer.Write([int]$dataSize)
        foreach ($s in $samples) {
            $v = [Math]::Max(-1.0, [Math]::Min(1.0, $s))
            $writer.Write([int16]([Math]::Round($v * 32767)))
        }
    } finally {
        $writer.Close()
    }
    Write-Host "Generated: $path"
}

function New-Chord([double]$seconds, [double[]]$freqs, [double]$amplitude, [double]$wobbleHz) {
    $count = [int]($seconds * $sampleRate)
    $samples = [double[]]::new($count)
    $twoPi = 2.0 * [Math]::PI
    for ($i = 0; $i -lt $count; $i++) {
        $t = $i / $sampleRate
        $v = 0.0
        foreach ($f in $freqs) { $v += [Math]::Sin($twoPi * $f * $t) }
        $gain = $amplitude
        if ($wobbleHz -gt 0) { $gain *= 0.6 + 0.4 * [Math]::Sin($twoPi * $wobbleHz * $t) }
        $samples[$i] = $v * $gain
    }
    return ,$samples
}

# SE: 880Hz、指数減衰。先頭と末尾のクリックノイズ防止に短いフェードを付ける
$seCount = [int](0.15 * $sampleRate)
$se = [double[]]::new($seCount)
for ($i = 0; $i -lt $seCount; $i++) {
    $t = $i / $sampleRate
    $attack = [Math]::Min(1.0, $i / 200.0)
    $release = [Math]::Min(1.0, ($seCount - $i) / 400.0)
    $se[$i] = [Math]::Sin(2.0 * [Math]::PI * 880.0 * $t) * [Math]::Exp(-$t * 18.0) * $attack * $release * 0.6
}
Write-Wav (Join-Path $outDir 'test_se.wav') $se

Write-Wav (Join-Path $outDir 'test_bgm_a.wav') (New-Chord 4.0 @(220.0, 275.0, 330.0) 0.15 0.0)
Write-Wav (Join-Path $outDir 'test_bgm_b.wav') (New-Chord 4.0 @(196.0, 247.0, 294.0) 0.15 2.0)
