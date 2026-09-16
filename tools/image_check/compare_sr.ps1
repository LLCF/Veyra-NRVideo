# Numeric image comparison for SR verification (Windows PowerShell 5.1).
# Usage: powershell -File tools\image_check\compare_sr.ps1 -Reference <1080p.jpg> -Candidate <4K.jpg>
#
# Both images must be saved by the player's --smoke-save at the same smoke step.
# The candidate (SR path, work extent) is downscaled to the reference extent and
# compared, so "the SR output really depicts the same frame" becomes a number:
# mean absolute difference plus per-image gradient energy (detail level).
param(
    [Parameter(Mandatory = $true)][string]$Reference,
    [Parameter(Mandatory = $true)][string]$Candidate
)
Add-Type -AssemblyName System.Drawing

function Get-Pixels([System.Drawing.Bitmap]$bitmap, [int]$width, [int]$height) {
    $scaled = New-Object System.Drawing.Bitmap $width, $height
    $graphics = [System.Drawing.Graphics]::FromImage($scaled)
    $graphics.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    $graphics.DrawImage($bitmap, 0, 0, $width, $height)
    $graphics.Dispose()
    $rect = New-Object System.Drawing.Rectangle 0, 0, $width, $height
    $data = $scaled.LockBits($rect, [System.Drawing.Imaging.ImageLockMode]::ReadOnly,
                             [System.Drawing.Imaging.PixelFormat]::Format24bppRgb)
    $bytes = New-Object byte[] ($data.Stride * $height)
    [System.Runtime.InteropServices.Marshal]::Copy($data.Scan0, $bytes, 0, $bytes.Length)
    $scaled.UnlockBits($data)
    $scaled.Dispose()
    return @{ Bytes = $bytes; Stride = $data.Stride }
}

function Get-GradientEnergy($pixels, [int]$width, [int]$height) {
    $sum = 0.0
    $count = 0
    for ($y = 1; $y -lt $height - 1; $y += 2) {
        for ($x = 1; $x -lt $width - 1; $x += 2) {
            $i = $y * $pixels.Stride + $x * 3
            $gx = [int]$pixels.Bytes[$i + 3] - [int]$pixels.Bytes[$i - 3]
            $gy = [int]$pixels.Bytes[$i + $pixels.Stride] - [int]$pixels.Bytes[$i - $pixels.Stride]
            $sum += [Math]::Sqrt($gx * $gx + $gy * $gy)
            $count++
        }
    }
    if ($count -eq 0) { return 0.0 }
    return $sum / $count
}

$referenceBitmap = [System.Drawing.Bitmap]::FromFile((Resolve-Path -LiteralPath $Reference))
$candidateBitmap = [System.Drawing.Bitmap]::FromFile((Resolve-Path -LiteralPath $Candidate))
Write-Output ("reference={0}x{1} candidate={2}x{3}" -f $referenceBitmap.Width, $referenceBitmap.Height,
              $candidateBitmap.Width, $candidateBitmap.Height)
$width = $referenceBitmap.Width
$height = $referenceBitmap.Height
$referencePixels = Get-Pixels $referenceBitmap $width $height
$candidatePixels = Get-Pixels $candidateBitmap $width $height

$difference = 0.0
$referenceLuma = 0.0
$candidateLuma = 0.0
$count = 0
for ($y = 0; $y -lt $height; $y += 2) {
    for ($x = 0; $x -lt $width; $x += 2) {
        $i = $y * $referencePixels.Stride + $x * 3
        $rLuma = 0.2126 * $referencePixels.Bytes[$i + 2] + 0.7152 * $referencePixels.Bytes[$i + 1] + 0.0722 * $referencePixels.Bytes[$i]
        $cLuma = 0.2126 * $candidatePixels.Bytes[$i + 2] + 0.7152 * $candidatePixels.Bytes[$i + 1] + 0.0722 * $candidatePixels.Bytes[$i]
        $difference += [Math]::Abs($rLuma - $cLuma)
        $referenceLuma += $rLuma
        $candidateLuma += $cLuma
        $count++
    }
}
$mad = $difference / $count
$referenceEnergy = Get-GradientEnergy $referencePixels $width $height
$candidateEnergy = Get-GradientEnergy $candidatePixels $width $height
Write-Output ("meanLuma reference={0:N2} candidate={1:N2}" -f ($referenceLuma / $count), ($candidateLuma / $count))
Write-Output ("meanAbsDifference={0:N2} (0-255 scale; content agreement)" -f $mad)
Write-Output ("gradientEnergy reference={0:N3} candidate={1:N3} ratio={2:N3}" -f $referenceEnergy, $candidateEnergy,
              ($candidateEnergy / [Math]::Max($referenceEnergy, 0.001)))
if ($mad -le 12.0) { Write-Output "VERDICT same-content (SR output tracks the reference frame)" }
else { Write-Output "VERDICT content differs beyond a JPEG/resample margin - inspect" }

# When the candidate is larger, also compare it against a pure resample of the
# reference at the candidate's extent. An upscaler that added real detail must
# differ from the resample and carry more high-frequency energy than it.
if ($candidateBitmap.Width -gt $referenceBitmap.Width) {
    $wide = $candidateBitmap.Width
    $tall = $candidateBitmap.Height
    $resampled = Get-Pixels ([System.Drawing.Bitmap]::FromFile((Resolve-Path -LiteralPath $Reference))) $wide $tall
    $candidateWide = Get-Pixels ([System.Drawing.Bitmap]::FromFile((Resolve-Path -LiteralPath $Candidate))) $wide $tall
    $resampleDifference = 0.0
    $wideCount = 0
    for ($y = 0; $y -lt $tall; $y += 4) {
        for ($x = 0; $x -lt $wide; $x += 4) {
            $i = $y * $candidateWide.Stride + $x * 3
            $a = 0.2126 * $resampled.Bytes[$i + 2] + 0.7152 * $resampled.Bytes[$i + 1] + 0.0722 * $resampled.Bytes[$i]
            $b = 0.2126 * $candidateWide.Bytes[$i + 2] + 0.7152 * $candidateWide.Bytes[$i + 1] + 0.0722 * $candidateWide.Bytes[$i]
            $resampleDifference += [Math]::Abs($a - $b)
            $wideCount++
        }
    }
    $resampleEnergy = Get-GradientEnergy $resampled $wide $tall
    $candidateWideEnergy = Get-GradientEnergy $candidateWide $wide $tall
    Write-Output ("at {0}x{1}: meanAbsDifference(resample vs SR)={2:N2} gradientEnergy resample={3:N3} SR={4:N3} ratio={5:N3}" -f `
        $wide, $tall, ($resampleDifference / $wideCount), $resampleEnergy, $candidateWideEnergy,
        ($candidateWideEnergy / [Math]::Max($resampleEnergy, 0.001)))
}
$referenceBitmap.Dispose()
$candidateBitmap.Dispose()
