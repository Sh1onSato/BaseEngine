<#
GenerateTestTextures.ps1

Camera2D / SpriteAnimation（Engine\2d\Camera2D.h, SpriteAnimation.h）の動作確認用に、
外部ライブラリ無し（PowerShell標準機能 + System.Drawing のみ）でテスト用のpngを2種類生成するスクリプト。EngineTestSceneで使う。
- test_sheet.png : 4列x2行のスプライトシート（1コマ64x64、全体256x128、背景透明）。
                   コマごとに色の違う円と番号(1〜8)、コマごとに45度ずつ回る白い点を描いてある。
                   0〜3コマ目を "idle"、4〜7コマ目を "spin" として再生する想定
- test_grid.png  : 256x256のタイル状のグリッド（暗い背景、32pxごとに細い線、128pxごとに太い線）。
                   繰り返し(WRAP)で並べても継ぎ目が分からないようにしてある。ワールドの背景用
- test_frame.png : 48x48の角の丸い枠（UIPanel::SetSlice(16) の9スライス確認用。UITestSceneで使う）。
                   中は白（パネルの色がそのまま出る）、縁は灰色、四隅に小さな点。
                   9スライスなら、どんな大きさにしても角の丸みと点が同じ大きさのまま出る

このスクリプトはビルド対象外（.vcxprojに登録しない）。実行はユーザーまたは開発者が手動で行う。
    powershell -NoProfile -ExecutionPolicy Bypass -File .\Resources\textures\GenerateTestTextures.ps1
#>

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
$outDir = $PSScriptRoot

# HSV(色相0〜360) → Color
function Get-HueColor([double]$hue) {
    $h = ($hue % 360.0) / 60.0
    $x = 1.0 - [Math]::Abs(($h % 2.0) - 1.0)
    switch ([int][Math]::Floor($h)) {
        0 { $r = 1.0; $g = $x;  $b = 0.0 }
        1 { $r = $x;  $g = 1.0; $b = 0.0 }
        2 { $r = 0.0; $g = 1.0; $b = $x }
        3 { $r = 0.0; $g = $x;  $b = 1.0 }
        4 { $r = $x;  $g = 0.0; $b = 1.0 }
        default { $r = 1.0; $g = 0.0; $b = $x }
    }
    return [System.Drawing.Color]::FromArgb(255, [int]($r * 230 + 25), [int]($g * 230 + 25), [int]($b * 230 + 25))
}

# --- test_sheet.png ---
$frameSize = 64
$columns = 4
$rows = 2
$sheet = New-Object System.Drawing.Bitmap(($frameSize * $columns), ($frameSize * $rows), ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb))
$g = [System.Drawing.Graphics]::FromImage($sheet)
try {
    $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $g.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::AntiAliasGridFit
    $g.Clear([System.Drawing.Color]::Transparent)
    $font = New-Object System.Drawing.Font('Arial', 22, ([System.Drawing.FontStyle]::Bold), ([System.Drawing.GraphicsUnit]::Pixel))
    $format = New-Object System.Drawing.StringFormat
    $format.Alignment = [System.Drawing.StringAlignment]::Center
    $format.LineAlignment = [System.Drawing.StringAlignment]::Center
    $outline = New-Object System.Drawing.Pen([System.Drawing.Color]::FromArgb(255, 20, 20, 20), 2)
    for ($i = 0; $i -lt ($columns * $rows); $i++) {
        $x0 = ($i % $columns) * $frameSize
        $y0 = [Math]::Floor($i / $columns) * $frameSize
        # 円（コマごとに色相を45度ずらす。隣のコマにはみ出さないよう4px余白）
        $brush = New-Object System.Drawing.SolidBrush((Get-HueColor ($i * 45.0)))
        $g.FillEllipse($brush, $x0 + 4, $y0 + 4, $frameSize - 8, $frameSize - 8)
        $g.DrawEllipse($outline, $x0 + 4, $y0 + 4, $frameSize - 8, $frameSize - 8)
        $brush.Dispose()
        # 回る点（コマごとに45度ずつ。動いていることが分かるように）
        $angle = $i * [Math]::PI / 4.0
        $cx = $x0 + $frameSize / 2 + [Math]::Cos($angle) * 21
        $cy = $y0 + $frameSize / 2 + [Math]::Sin($angle) * 21
        $g.FillEllipse([System.Drawing.Brushes]::White, $cx - 5, $cy - 5, 10, 10)
        $g.DrawEllipse($outline, $cx - 5, $cy - 5, 10, 10)
        # 番号（1〜8）
        $rect = New-Object System.Drawing.RectangleF($x0, $y0, $frameSize, $frameSize)
        $g.DrawString(([string]($i + 1)), $font, [System.Drawing.Brushes]::Black, $rect, $format)
    }
    $outline.Dispose()
    $format.Dispose()
    $font.Dispose()
} finally {
    $g.Dispose()
}
$sheet.Save((Join-Path $outDir 'test_sheet.png'), [System.Drawing.Imaging.ImageFormat]::Png)
$sheet.Dispose()

# --- test_grid.png ---
# 線は各タイルの左端・上端だけに描く（右端・下端に描くと並べたとき線が2本分の太さになる）
$gridSize = 256
$grid = New-Object System.Drawing.Bitmap($gridSize, $gridSize, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb))
$g = [System.Drawing.Graphics]::FromImage($grid)
try {
    $g.Clear([System.Drawing.Color]::FromArgb(255, 32, 36, 48))
    $thin = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(255, 80, 90, 110))
    $thick = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(255, 150, 170, 200))
    for ($p = 0; $p -lt $gridSize; $p += 32) {
        if (($p % 128) -eq 0) {
            $g.FillRectangle($thick, $p, 0, 3, $gridSize)
            $g.FillRectangle($thick, 0, $p, $gridSize, 3)
        } else {
            $g.FillRectangle($thin, $p, 0, 1, $gridSize)
            $g.FillRectangle($thin, 0, $p, $gridSize, 1)
        }
    }
    $thin.Dispose()
    $thick.Dispose()
} finally {
    $g.Dispose()
}
$grid.Save((Join-Path $outDir 'test_grid.png'), [System.Drawing.Imaging.ImageFormat]::Png)
$grid.Dispose()

# --- test_frame.png ---
$frameTextureSize = 48
$radius = 12
$border = 3
$frame = New-Object System.Drawing.Bitmap($frameTextureSize, $frameTextureSize, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb))
$g = [System.Drawing.Graphics]::FromImage($frame)
try {
    $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $g.Clear([System.Drawing.Color]::Transparent)
    # 角の丸い四角の形
    $path = New-Object System.Drawing.Drawing2D.GraphicsPath
    $d = $radius * 2
    $max = $frameTextureSize - 1
    $path.AddArc(0, 0, $d, $d, 180, 90)
    $path.AddArc($max - $d, 0, $d, $d, 270, 90)
    $path.AddArc($max - $d, $max - $d, $d, $d, 0, 90)
    $path.AddArc(0, $max - $d, $d, $d, 90, 90)
    $path.CloseFigure()
    $g.FillPath([System.Drawing.Brushes]::White, $path)
    $pen = New-Object System.Drawing.Pen([System.Drawing.Color]::FromArgb(255, 120, 120, 120), $border)
    $pen.Alignment = [System.Drawing.Drawing2D.PenAlignment]::Inset
    $g.DrawPath($pen, $path)
    # 四隅の点（端 16px の中に収まる位置）
    $dot = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(255, 90, 90, 90))
    foreach ($p in @(@(7, 7), @(($frameTextureSize - 11), 7), @(7, ($frameTextureSize - 11)), @(($frameTextureSize - 11), ($frameTextureSize - 11)))) {
        $g.FillEllipse($dot, $p[0], $p[1], 4, 4)
    }
    $dot.Dispose()
    $pen.Dispose()
    $path.Dispose()
} finally {
    $g.Dispose()
}
$frame.Save((Join-Path $outDir 'test_frame.png'), [System.Drawing.Imaging.ImageFormat]::Png)
$frame.Dispose()

Write-Host "生成しました: $outDir\test_sheet.png, $outDir\test_grid.png, $outDir\test_frame.png"
