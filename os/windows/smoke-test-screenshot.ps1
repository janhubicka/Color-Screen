param(
    [string]$OutputFile = "shot.png",
    [int]$DelaySeconds = 10
)

Write-Host "Waiting $DelaySeconds seconds before capturing screenshot to $OutputFile..."
Start-Sleep -Seconds $DelaySeconds

try {
    Add-Type -AssemblyName System.Windows.Forms
    Add-Type -AssemblyName System.Drawing

    $Screen = [System.Windows.Forms.Screen]::PrimaryScreen
    $Width  = $Screen.Bounds.Width
    $Height = $Screen.Bounds.Height
    $Left   = $Screen.Bounds.Left
    $Top    = $Screen.Bounds.Top

    $Bitmap = New-Object System.Drawing.Bitmap($Width, $Height)
    $Graphics = [System.Drawing.Graphics]::FromImage($Bitmap)

    Write-Host "Capturing screen: ${Width}x${Height} at ($Left, $Top)"
    $Graphics.CopyFromScreen($Left, $Top, 0, 0, $Bitmap.Size)

    $Bitmap.Save($OutputFile, [System.Drawing.Imaging.ImageFormat]::Png)

    $Graphics.Dispose()
    $Bitmap.Dispose()
    Write-Host "Screenshot saved successfully."
}
catch {
    Write-Error "Failed to capture screenshot: $_"
    exit 1
}
