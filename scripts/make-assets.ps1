# Draws the source images for `npx capacitor-assets generate` into assets/.
# Windows PowerShell:  powershell -File scripts\make-assets.ps1
Add-Type -AssemblyName System.Drawing
$out = Join-Path $PSScriptRoot '..\assets'
New-Item -ItemType Directory -Force $out | Out-Null

$asphalt = [Drawing.ColorTranslator]::FromHtml('#0A0E14')
$paper   = [Drawing.ColorTranslator]::FromHtml('#F6F5F0')
$red     = [Drawing.ColorTranslator]::FromHtml('#D61F26')
$ink     = [Drawing.ColorTranslator]::FromHtml('#111111')

# size: canvas px, d: sign diameter as a fraction of the canvas, bg: fill the background or leave transparent
function Draw($name, $size, $d, $bg) {
  $bmp = New-Object Drawing.Bitmap $size, $size
  $g = [Drawing.Graphics]::FromImage($bmp)
  $g.SmoothingMode = 'AntiAlias'; $g.TextRenderingHint = 'AntiAliasGridFit'
  if ($bg) { $g.Clear($asphalt) } else { $g.Clear([Drawing.Color]::Transparent) }
  if ($d -gt 0) {
    $D = $size * $d; $x = ($size - $D) / 2
    $ring = $D * 0.11
    $g.FillEllipse((New-Object Drawing.SolidBrush $red), [single]$x, [single]$x, [single]$D, [single]$D)
    $g.FillEllipse((New-Object Drawing.SolidBrush $paper), [single]($x + $ring), [single]($x + $ring), [single]($D - 2 * $ring), [single]($D - 2 * $ring))
    $font = New-Object Drawing.Font 'Arial', ([single]($D * 0.34)), ([Drawing.FontStyle]::Bold), ([Drawing.GraphicsUnit]::Pixel)
    $fmt = New-Object Drawing.StringFormat; $fmt.Alignment = 'Center'; $fmt.LineAlignment = 'Center'
    $rect = New-Object Drawing.RectangleF 0, ([single]($size * 0.012)), $size, $size
    $g.DrawString('80', $font, (New-Object Drawing.SolidBrush $ink), $rect, $fmt)
  }
  $bmp.Save((Join-Path $out $name), [Drawing.Imaging.ImageFormat]::Png)
  $g.Dispose(); $bmp.Dispose()
  "assets/$name"
}

Draw 'icon-only.png'       1024 0.84 $true    # iOS + legacy Android icon
Draw 'icon-foreground.png' 1024 0.58 $false   # Android adaptive icon (inside the 66% safe zone)
Draw 'icon-background.png' 1024 0    $true
Draw 'splash.png'          2732 0.26 $true
Draw 'splash-dark.png'     2732 0.26 $true
