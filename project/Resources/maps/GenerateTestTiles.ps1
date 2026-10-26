<#
GenerateTestTiles.ps1

TILED_TEST（Engine\scene\TiledTestScene）で使うタイルセット画像 test_tiles.png を作るスクリプト。
32x32 のタイルを横4 x 縦2 に並べる（128x64）。タイル番号（左上から 0〜7）:
  0 草の地面 / 1 土 / 2 レンガ / 3 石 / 4 コイン / 5 ゴールの旗 / 6 雲 / 7 草むら
このスクリプトはビルド対象外。実行するときは:
    powershell -ExecutionPolicy Bypass -File .\Resources\maps\GenerateTestTiles.ps1
#>

$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Drawing

$tile = 32
$columns = 4
$rows = 2
$bitmap = New-Object System.Drawing.Bitmap ($tile * $columns), ($tile * $rows), ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
$g = [System.Drawing.Graphics]::FromImage($bitmap)
$g.Clear([System.Drawing.Color]::Transparent)
$g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::None

function Brush([int]$r, [int]$gr, [int]$b, [int]$a = 255) { New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb($a, $r, $gr, $b)) }
function Origin([int]$id) { @{ x = ($id % $columns) * $tile; y = [math]::Floor($id / $columns) * $tile } }

# 0 草の地面: 土の上に草
$o = Origin 0
$g.FillRectangle((Brush 139 90 43), $o.x, $o.y, $tile, $tile)
$g.FillRectangle((Brush 76 175 80), $o.x, $o.y, $tile, 10)
$g.FillRectangle((Brush 56 142 60), $o.x, ($o.y + 10), $tile, 3)

# 1 土
$o = Origin 1
$g.FillRectangle((Brush 139 90 43), $o.x, $o.y, $tile, $tile)
$g.FillRectangle((Brush 110 70 33), ($o.x + 6), ($o.y + 8), 5, 4)
$g.FillRectangle((Brush 110 70 33), ($o.x + 20), ($o.y + 20), 6, 4)

# 2 レンガ: 横の目地と、段ごとにずらした縦の目地
$o = Origin 2
$g.FillRectangle((Brush 178 76 50), $o.x, $o.y, $tile, $tile)
$mortar = Brush 230 210 190
foreach ($y in 0, 8, 16, 24) { $g.FillRectangle($mortar, $o.x, ($o.y + $y), $tile, 2) }
foreach ($row in 0..3) {
    $offset = if ($row % 2 -eq 0) { 0 } else { 8 }
    foreach ($x in 0, 16) { $g.FillRectangle($mortar, ($o.x + (($x + $offset) % $tile)), ($o.y + $row * 8), 2, 8) }
}

# 3 石
$o = Origin 3
$g.FillRectangle((Brush 120 120 128), $o.x, $o.y, $tile, $tile)
$g.FillRectangle((Brush 150 150 158), ($o.x + 2), ($o.y + 2), 28, 4)
$g.FillRectangle((Brush 90 90 98), ($o.x + 2), ($o.y + 26), 28, 4)

# 4 コイン
$o = Origin 4
$g.FillEllipse((Brush 255 200 0), ($o.x + 6), ($o.y + 4), 20, 24)
$g.FillEllipse((Brush 255 235 120), ($o.x + 11), ($o.y + 9), 8, 14)

# 5 ゴールの旗
$o = Origin 5
$g.FillRectangle((Brush 240 240 240), ($o.x + 6), ($o.y + 2), 3, 30)
$g.FillPolygon((Brush 220 40 40), [System.Drawing.Point[]]@(
    (New-Object System.Drawing.Point ($o.x + 9), ($o.y + 3)),
    (New-Object System.Drawing.Point ($o.x + 29), ($o.y + 9)),
    (New-Object System.Drawing.Point ($o.x + 9), ($o.y + 15))))

# 6 雲
$o = Origin 6
$cloud = Brush 255 255 255 230
$g.FillEllipse($cloud, ($o.x + 2), ($o.y + 12), 16, 14)
$g.FillEllipse($cloud, ($o.x + 10), ($o.y + 6), 16, 18)
$g.FillEllipse($cloud, ($o.x + 16), ($o.y + 12), 14, 14)

# 7 草むら
$o = Origin 7
$bush = Brush 46 160 67
$g.FillEllipse($bush, ($o.x + 1), ($o.y + 14), 16, 18)
$g.FillEllipse($bush, ($o.x + 9), ($o.y + 8), 16, 24)
$g.FillEllipse($bush, ($o.x + 16), ($o.y + 14), 15, 18)

$g.Dispose()
$outPath = Join-Path $PSScriptRoot "test_tiles.png"
$bitmap.Save($outPath, [System.Drawing.Imaging.ImageFormat]::Png)
$bitmap.Dispose()
Write-Host "生成: $outPath"
