<#
GenerateTestArm.ps1

GltfLoader（Engine\3d\GltfLoader.h/.cpp）の動作確認用に、2ジョイントで曲がる棒のglTFを
外部ライブラリ無し（PowerShell標準機能のみ）で2種類生成するスクリプト。
- TestArm_embedded.gltf : 頂点/インデックス/JOINTS_0/WEIGHTS_0/inverseBindMatricesを
                           全てbase64データURIとしてgltf内に埋め込んだ版
- TestArm_external.gltf + TestArm_external.bin : 同内容だが外部.binファイル参照版

形状はEngine\scene\SkinningTestScene.cpp(フェーズ1)のBuildBarVertices()と同じ寸法感：
半太さ0.3の四角柱、下半分(y=0->1)がjoint0、上半分(y=1->2)がjoint1、
継ぎ目(y=1)でJOINTS_0/WEIGHTS_0を0.5/0.5にブレンドする。

このスクリプトはビルド対象外（.vcxprojに登録しない）。実行はユーザーまたは開発者が手動で行う。
    powershell -ExecutionPolicy Bypass -File .\Resources\gltf\GenerateTestArm.ps1

【注意】inverseBindMatricesは本来「バインドポーズにおけるジョイントのワールド行列の逆行列」だが、
このテストデータでは簡略化のため単位行列を採用している（フェーズ2はJSON/glTFパーサの検証が目的であり、
実際のスキニング計算の正しさの検証はフェーズ3で行うため）。
#>

$ErrorActionPreference = "Stop"
$invariant = [System.Globalization.CultureInfo]::InvariantCulture

$outDir = $PSScriptRoot
if (-not (Test-Path $outDir)) {
    New-Item -ItemType Directory -Force -Path $outDir | Out-Null
}

# ---------- 数値フォーマットヘルパー（JSONは常に"."区切り。ロケール依存を避けるためInvariantCultureで固定） ----------
function Fmt([double]$v) {
    return $v.ToString("0.######", $invariant)
}
function FmtArrF([double[]]$arr) {
    $parts = @()
    foreach ($v in $arr) { $parts += (Fmt $v) }
    return "[" + ($parts -join ", ") + "]"
}
function FmtArrI([int[]]$arr) {
    $parts = @()
    foreach ($v in $arr) { $parts += [string]$v }
    return "[" + ($parts -join ", ") + "]"
}

# ---------- 1. 頂点データの組み立て（SkinningTestScene::BuildBarVertices()と同じ寸法・分割） ----------
$kHalfThickness = 0.3

# 断面(XZ平面)の4隅。C0->C1->C2->C3の順で上から見てCCW
$corners = @(
    @{ x = -$kHalfThickness; z = -$kHalfThickness },
    @{ x =  $kHalfThickness; z = -$kHalfThickness },
    @{ x =  $kHalfThickness; z =  $kHalfThickness },
    @{ x = -$kHalfThickness; z =  $kHalfThickness }
)
# 各面(Ci->Cjの辺)の外向き法線
$faceNormals = @(
    @(0.0, 0.0, -1.0),
    @(1.0, 0.0,  0.0),
    @(0.0, 0.0,  1.0),
    @(-1.0, 0.0, 0.0)
)
# segment: y0->y1の範囲と、y0側/y1側それぞれのボーン0・ボーン1の重み
$segments = @(
    @{ y0 = 0.0; y1 = 1.0; w0 = @(1.0, 0.0); w1 = @(0.5, 0.5) }, # 下半分: joint0 -> 継ぎ目
    @{ y0 = 1.0; y1 = 2.0; w0 = @(0.5, 0.5); w1 = @(0.0, 1.0) }  # 上半分: 継ぎ目 -> joint1
)

$positions = New-Object System.Collections.Generic.List[double]
$normals   = New-Object System.Collections.Generic.List[double]
$texcoords = New-Object System.Collections.Generic.List[double]
$joints    = New-Object System.Collections.Generic.List[int]
$weights   = New-Object System.Collections.Generic.List[double]
$indices   = New-Object System.Collections.Generic.List[int]
$vertexIndex = 0

foreach ($seg in $segments) {
    for ($i = 0; $i -lt 4; $i++) {
        $j  = ($i + 1) % 4
        $ci = $corners[$i]
        $cj = $corners[$j]
        $n  = $faceNormals[$i]

        # bottomI, bottomJ, topJ, topI の4頂点（1つの面＝1つの四角形＝2三角形）
        $positions.AddRange([double[]]@($ci.x, $seg.y0, $ci.z))
        $positions.AddRange([double[]]@($cj.x, $seg.y0, $cj.z))
        $positions.AddRange([double[]]@($cj.x, $seg.y1, $cj.z))
        $positions.AddRange([double[]]@($ci.x, $seg.y1, $ci.z))

        for ($k = 0; $k -lt 4; $k++) { $normals.AddRange([double[]]$n) }

        $texcoords.AddRange([double[]]@(0.0, 1.0))
        $texcoords.AddRange([double[]]@(1.0, 1.0))
        $texcoords.AddRange([double[]]@(1.0, 0.0))
        $texcoords.AddRange([double[]]@(0.0, 0.0))

        for ($k = 0; $k -lt 4; $k++) { $joints.AddRange([int[]]@(0, 1, 0, 0)) }

        $weights.AddRange([double[]]@($seg.w0[0], $seg.w0[1], 0.0, 0.0)) # bottomI
        $weights.AddRange([double[]]@($seg.w0[0], $seg.w0[1], 0.0, 0.0)) # bottomJ
        $weights.AddRange([double[]]@($seg.w1[0], $seg.w1[1], 0.0, 0.0)) # topJ
        $weights.AddRange([double[]]@($seg.w1[0], $seg.w1[1], 0.0, 0.0)) # topI

        # 外側から見てCCWが前面（PSOのFrontCounterClockwise=FALSE、SkinningTestSceneと同じ巻き順）
        $vi0 = $vertexIndex
        $vi1 = $vertexIndex + 1
        $vi2 = $vertexIndex + 2
        $vi3 = $vertexIndex + 3
        $indices.Add($vi0); $indices.Add($vi1); $indices.Add($vi2)
        $indices.Add($vi0); $indices.Add($vi2); $indices.Add($vi3)
        $vertexIndex += 4
    }
}

$vertexCount = $positions.Count / 3
Write-Host "頂点数: $vertexCount / インデックス数: $($indices.Count)"

# POSITIONアクセサのmin/max（glTF仕様上の推奨。本エンジンのGltfLoaderは参照しないが正規のglTFに近づけるため付与）
$posX = @(); $posY = @(); $posZ = @()
for ($i = 0; $i -lt $positions.Count; $i += 3) {
    $posX += $positions[$i]; $posY += $positions[$i + 1]; $posZ += $positions[$i + 2]
}
$posMin = @(($posX | Measure-Object -Minimum).Minimum, ($posY | Measure-Object -Minimum).Minimum, ($posZ | Measure-Object -Minimum).Minimum)
$posMax = @(($posX | Measure-Object -Maximum).Maximum, ($posY | Measure-Object -Maximum).Maximum, ($posZ | Measure-Object -Maximum).Maximum)

# ---------- 2. アニメーションデータ（joint1のROTATIONチャンネル。t=0->0.5->1.0でLINEAR補間） ----------
$animTimes = @(0.0, 0.5, 1.0)
# X軸まわり60度回転のクォータニオン(x,y,z,w) = (sin(30deg), 0, 0, cos(30deg))。往復するのでt=0とt=1.0は単位クォータニオン
$halfAngle = [Math]::PI / 6.0 # 30deg
$bendQuatX = [Math]::Sin($halfAngle)
$bendQuatW = [Math]::Cos($halfAngle)
$animRotations = @(
    0.0, 0.0, 0.0, 1.0,             # t=0.0: 単位クォータニオン
    $bendQuatX, 0.0, 0.0, $bendQuatW, # t=0.5: 約60度曲げ
    0.0, 0.0, 0.0, 1.0              # t=1.0: 単位クォータニオンに戻る（ループしやすい）
)

# ---------- 3. スキン（簡略化のため単位行列2つ。コメント参照） ----------
$identity4x4 = @(
    1.0, 0.0, 0.0, 0.0,
    0.0, 1.0, 0.0, 0.0,
    0.0, 0.0, 1.0, 0.0,
    0.0, 0.0, 0.0, 1.0
)
$inverseBindMatrices = $identity4x4 + $identity4x4 # joint0用 + joint1用（2つとも単位行列）

# ---------- 4. バイト列へのパッキング（リトルエンディアン。glTF仕様通り） ----------
function BytesOfFloatArray([double[]]$values) {
    $list = New-Object System.Collections.Generic.List[byte]
    foreach ($v in $values) { $list.AddRange([BitConverter]::GetBytes([float]$v)) }
    return , $list.ToArray()
}
function BytesOfUInt16Array([int[]]$values) {
    $list = New-Object System.Collections.Generic.List[byte]
    foreach ($v in $values) { $list.AddRange([BitConverter]::GetBytes([uint16]$v)) }
    return , $list.ToArray()
}
function BytesOfUByteArray([int[]]$values) {
    $bytes = New-Object byte[] ($values.Count)
    for ($i = 0; $i -lt $values.Count; $i++) { $bytes[$i] = [byte]$values[$i] }
    return , $bytes
}

$bufferBytes = New-Object System.Collections.Generic.List[byte]
$bufferViews = New-Object System.Collections.Generic.List[hashtable]
$viewIndex = @{}

function AddBufferView([string]$name, [byte[]]$bytes) {
    # 4バイト境界にパディング（glTFの慣例に合わせる。本パーサはパディング無しでも読めるが正規の形に近づける）
    while (($bufferBytes.Count % 4) -ne 0) { $bufferBytes.Add(0) | Out-Null }
    $offset = $bufferBytes.Count
    $bufferBytes.AddRange($bytes)
    $index = $bufferViews.Count
    $bufferViews.Add(@{ byteOffset = $offset; byteLength = $bytes.Length }) | Out-Null
    $viewIndex[$name] = $index
}

AddBufferView "positions" (BytesOfFloatArray $positions.ToArray())
AddBufferView "normals"   (BytesOfFloatArray $normals.ToArray())
AddBufferView "texcoords" (BytesOfFloatArray $texcoords.ToArray())
AddBufferView "joints"    (BytesOfUByteArray $joints.ToArray())
AddBufferView "weights"   (BytesOfFloatArray $weights.ToArray())
AddBufferView "indices"   (BytesOfUInt16Array $indices.ToArray())
AddBufferView "times"     (BytesOfFloatArray $animTimes)
AddBufferView "rotations" (BytesOfFloatArray $animRotations)
AddBufferView "ibm"       (BytesOfFloatArray $inverseBindMatrices)

$totalBufferBytes = $bufferBytes.ToArray()
Write-Host "バッファ総バイト数: $($totalBufferBytes.Length)"

# ---------- 5. accessors / bufferViews のJSON断片を組み立てる ----------
function AccessorJson([int]$bufferViewIdx, [int]$componentType, [string]$type, [int]$count, [string]$minMax = "") {
    $json = "{ `"bufferView`": $bufferViewIdx, `"componentType`": $componentType, `"count`": $count, `"type`": `"$type`""
    if ($minMax -ne "") { $json += $minMax }
    $json += " }"
    return $json
}

$accessorPosition = AccessorJson $viewIndex["positions"] 5126 "VEC3" $vertexCount (", `"min`": $(FmtArrF $posMin), `"max`": $(FmtArrF $posMax)")
$accessorNormal   = AccessorJson $viewIndex["normals"]   5126 "VEC3" $vertexCount
$accessorTexcoord = AccessorJson $viewIndex["texcoords"] 5126 "VEC2" $vertexCount
$accessorJoints   = AccessorJson $viewIndex["joints"]    5121 "VEC4" $vertexCount   # UNSIGNED_BYTE
$accessorWeights  = AccessorJson $viewIndex["weights"]   5126 "VEC4" $vertexCount
$accessorIndices  = AccessorJson $viewIndex["indices"]   5123 "SCALAR" $indices.Count # UNSIGNED_SHORT
$accessorTimes    = AccessorJson $viewIndex["times"]     5126 "SCALAR" $animTimes.Count
$accessorRotations = AccessorJson $viewIndex["rotations"] 5126 "VEC4" $animTimes.Count
$accessorIbm      = AccessorJson $viewIndex["ibm"]       5126 "MAT4" 2

# accessorのインデックスは定義順（0=position,1=normal,2=texcoord,3=joints,4=weights,5=indices,6=times,7=rotations,8=ibm）
$accessorsJson = "[" + (@($accessorPosition, $accessorNormal, $accessorTexcoord, $accessorJoints, $accessorWeights, $accessorIndices, $accessorTimes, $accessorRotations, $accessorIbm) -join ",`n    ") + "]"

$bufferViewsJsonParts = @()
foreach ($bv in $bufferViews) {
    $bufferViewsJsonParts += "{ `"buffer`": 0, `"byteOffset`": $($bv.byteOffset), `"byteLength`": $($bv.byteLength) }"
}
$bufferViewsJson = "[" + ($bufferViewsJsonParts -join ",`n    ") + "]"

# accessorIndexByName相当（可読性のためのコメント: 0=POSITION,1=NORMAL,2=TEXCOORD_0,3=JOINTS_0,4=WEIGHTS_0,5=indices,6=times,7=rotations,8=inverseBindMatrices）
$idxPosition = 0; $idxNormal = 1; $idxTexcoord = 2; $idxJoints = 3; $idxWeights = 4
$idxIndices = 5; $idxTimes = 6; $idxRotations = 7; $idxIbm = 8

# ---------- 6. gltf全体のJSON（buffers部分だけembedded/externalで差し替える） ----------
function BuildGltfJson([string]$buffersJson) {
    return @"
{
  "asset": { "version": "2.0", "generator": "GenerateTestArm.ps1 (custom script, no external libraries)" },
  "scene": 0,
  "scenes": [ { "nodes": [0] } ],
  "nodes": [
    { "name": "ArmMesh", "mesh": 0, "skin": 0, "children": [1] },
    { "name": "Joint0", "children": [2] },
    { "name": "Joint1", "translation": [0.0, 1.0, 0.0] }
  ],
  "meshes": [
    {
      "primitives": [
        {
          "attributes": { "POSITION": $idxPosition, "NORMAL": $idxNormal, "TEXCOORD_0": $idxTexcoord, "JOINTS_0": $idxJoints, "WEIGHTS_0": $idxWeights },
          "indices": $idxIndices
        }
      ]
    }
  ],
  "skins": [
    { "joints": [1, 2], "inverseBindMatrices": $idxIbm }
  ],
  "animations": [
    {
      "samplers": [ { "input": $idxTimes, "output": $idxRotations, "interpolation": "LINEAR" } ],
      "channels": [ { "sampler": 0, "target": { "node": 2, "path": "rotation" } } ]
    }
  ],
  "accessors": $accessorsJson,
  "bufferViews": $bufferViewsJson,
  "buffers": $buffersJson
}
"@
}

# ---------- 7. embedded版（base64データURI） ----------
$base64 = [Convert]::ToBase64String($totalBufferBytes)
$embeddedBuffersJson = "[ { `"byteLength`": $($totalBufferBytes.Length), `"uri`": `"data:application/octet-stream;base64,$base64`" } ]"
$embeddedJson = BuildGltfJson $embeddedBuffersJson

$embeddedPath = Join-Path $outDir "TestArm_embedded.gltf"
# BOM無しUTF-8で書き込む（自前JsonParserはBOMを読み飛ばさないため、BOM付きだと先頭で構文エラーになる）
[System.IO.File]::WriteAllText($embeddedPath, $embeddedJson, (New-Object System.Text.UTF8Encoding $false))
Write-Host "生成: $embeddedPath"

# ---------- 8. external版（.bin参照） ----------
$binFileName = "TestArm_external.bin"
$binPath = Join-Path $outDir $binFileName
[System.IO.File]::WriteAllBytes($binPath, $totalBufferBytes)
Write-Host "生成: $binPath"

$externalBuffersJson = "[ { `"byteLength`": $($totalBufferBytes.Length), `"uri`": `"$binFileName`" } ]"
$externalJson = BuildGltfJson $externalBuffersJson

$externalPath = Join-Path $outDir "TestArm_external.gltf"
[System.IO.File]::WriteAllText($externalPath, $externalJson, (New-Object System.Text.UTF8Encoding $false))
Write-Host "生成: $externalPath"

# ---------- 9. JSON構文検証（ConvertFrom-Jsonでパース可能かのみ確認。GltfLoaderの意味的な正しさは別途コードレビューで確認） ----------
foreach ($path in @($embeddedPath, $externalPath)) {
    try {
        $null = Get-Content -Raw -Path $path | ConvertFrom-Json
        Write-Host "OK: $path はJSONとして正しい形式です"
    } catch {
        Write-Host "NG: $path のJSON構文エラー: $($_.Exception.Message)"
        throw
    }
}
