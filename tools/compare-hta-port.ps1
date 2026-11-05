<#
.SYNOPSIS
Launch the original Spaceship.hta and a native port build, then capture matched
screens for a repeatable, human-guided visual/behavior comparison.

.DESCRIPTION
Windows-only: requires mshta.exe, the native executable, an interactive desktop,
and PowerShell 5.1 or later. Both applications are launched once. The operator
puts them into the same state and arranges both windows on screen before each
capture. Captures include the full virtual desktop so both apps are visible.

This is an evidence-collection harness, not an automated correctness oracle. It
does not synthesize clicks or claim semantic/gameplay parity.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateScript({ Test-Path -LiteralPath $_ -PathType Leaf })]
    [string] $NativeExe,

    [string] $HtaPath = (Join-Path (Split-Path -Parent $PSScriptRoot) 'Spaceship.hta'),

    [string] $OutDir = (Join-Path (Split-Path -Parent $PSScriptRoot) 'hta-port-comparison'),

    [string[]] $Scenarios = @('ship-home', 'engine-room', 'mess-hall', 'navigation')
)

$ErrorActionPreference = 'Stop'

if (-not $IsWindows -and $PSVersionTable.PSVersion.Major -ge 6) {
    throw 'This harness requires Windows (mshta.exe and an interactive desktop).'
}
if (-not (Test-Path -LiteralPath $HtaPath -PathType Leaf)) {
    throw "HTA file not found: $HtaPath"
}
$mshta = Join-Path $env:WINDIR 'System32\mshta.exe'
if (-not (Test-Path -LiteralPath $mshta -PathType Leaf)) {
    throw "mshta.exe not found: $mshta"
}
if (-not (Test-Path -LiteralPath $NativeExe -PathType Leaf)) {
    throw "Native executable not found: $NativeExe"
}

Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing
if (-not ('HtaPortCompare.NativeMethods' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
namespace HtaPortCompare {
  public static class NativeMethods {
    [DllImport("kernel32.dll")] public static extern IntPtr GetConsoleWindow();
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr hWnd, int nCmdShow);
  }
}
'@
}

$NativeExe = (Resolve-Path -LiteralPath $NativeExe).Path
$HtaPath = (Resolve-Path -LiteralPath $HtaPath).Path
$OutDir = [System.IO.Path]::GetFullPath($OutDir)
New-Item -ItemType Directory -Path $OutDir -Force | Out-Null
$started = Get-Date
$notes = New-Object 'System.Collections.Generic.List[string]'
$htaProcess = $null
$portProcess = $null

function Capture-Desktop([string] $Path) {
    $bounds = [System.Windows.Forms.SystemInformation]::VirtualScreen
    if ($bounds.Width -lt 1 -or $bounds.Height -lt 1) {
        throw 'No visible desktop bounds are available. Run this from an interactive Windows session.'
    }
    $bitmap = New-Object System.Drawing.Bitmap($bounds.Width, $bounds.Height)
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    try {
        $graphics.CopyFromScreen($bounds.Left, $bounds.Top, 0, 0, $bitmap.Size)
        $bitmap.Save($Path, [System.Drawing.Imaging.ImageFormat]::Png)
    }
    finally {
        $graphics.Dispose()
        $bitmap.Dispose()
    }
}

try {
    Write-Host 'Launching the original HTA and native port...'
    $htaProcess = Start-Process -FilePath $mshta `
        -ArgumentList ('"{0}"' -f $HtaPath) -PassThru
    $portProcess = Start-Process -FilePath $NativeExe `
        -WorkingDirectory (Split-Path -Parent $NativeExe) -PassThru
    Start-Sleep -Seconds 3

    if ($htaProcess.HasExited) { throw "mshta exited early (exit code $($htaProcess.ExitCode))." }
    if ($portProcess.HasExited) { throw "Native port exited early (exit code $($portProcess.ExitCode))." }

    Write-Host ''
    Write-Host 'Arrange the HTA and native windows side by side at matching sizes.'
    Write-Host 'For each checkpoint, perform the same actions in both applications.'
    Write-Host 'The script captures the entire virtual desktop; close menus/tooltips first.'
    [void](Read-Host 'Press Enter when both applications are ready')

    $index = 0
    foreach ($scenario in $Scenarios) {
        $index++
        $label = Read-Host "Checkpoint $index label (default: $scenario)"
        if ([string]::IsNullOrWhiteSpace($label)) { $label = $scenario }
        $slug = ($label.ToLowerInvariant() -replace '[^a-z0-9]+', '-') -replace '(^-|-$)', ''
        if ([string]::IsNullOrWhiteSpace($slug)) { $slug = 'checkpoint-{0:D2}' -f $index }

        Write-Host ''
        Write-Host "Checkpoint: $label"
        Write-Host 'Put both apps into the same state and make the two windows comparable.'
        [void](Read-Host 'Press Enter to capture this checkpoint')

        # Minimize the PowerShell console during capture so it does not cover either app.
        $console = [HtaPortCompare.NativeMethods]::GetConsoleWindow()
        if ($console -ne [IntPtr]::Zero) {
            [void][HtaPortCompare.NativeMethods]::ShowWindow($console, 6)
            Start-Sleep -Milliseconds 500
        }
        $imageName = '{0:D2}-{1}.png' -f $index, $slug
        $imagePath = Join-Path $OutDir $imageName
        try { Capture-Desktop $imagePath }
        finally {
            if ($console -ne [IntPtr]::Zero) {
                [void][HtaPortCompare.NativeMethods]::ShowWindow($console, 9)
            }
        }

        $observation = Read-Host 'Briefly record differences or actions that failed (blank if none)'
        $notes.Add(('### {0}. {1}' -f $index, $label))
        $notes.Add('')
        $notes.Add(('- Capture: `{0}`' -f $imageName))
        if ([string]::IsNullOrWhiteSpace($observation)) {
            $notes.Add('- Observations: not recorded.')
        }
        else {
            $notes.Add(('- Observations: {0}' -f $observation.Trim()))
        }
        $notes.Add('')
        Write-Host "Saved $imagePath"
    }

    $os = [System.Environment]::OSVersion.VersionString
    $nativeVersion = (Get-Item -LiteralPath $NativeExe).VersionInfo.FileVersion
    $report = @(
        '# HTA vs native port comparison',
        '',
        ('- Started: {0:yyyy-MM-dd HH:mm:ss zzz}' -f $started),
        ('- OS: {0}' -f $os),
        ('- HTA: `{0}`' -f $HtaPath),
        ('- Native executable: `{0}`' -f $NativeExe),
        ('- Native file version: {0}' -f $(if ($nativeVersion) { $nativeVersion } else { 'not set' })),
        ('- Virtual desktop: {0} x {1}' -f [System.Windows.Forms.SystemInformation]::VirtualScreen.Width, [System.Windows.Forms.SystemInformation]::VirtualScreen.Height),
        '- Method: operator matched states manually; each PNG is a full-desktop capture.',
        '- Scope: screenshots and operator notes are evidence, not an automated parity verdict.',
        '',
        '## Checkpoints',
        ''
    )
    $report += $notes.ToArray()
    $reportPath = Join-Path $OutDir 'comparison.md'
    Set-Content -LiteralPath $reportPath -Value $report -Encoding UTF8
    Write-Host "Comparison package saved to $OutDir"
    Write-Host "Open $reportPath and inspect the checkpoint screenshots."
}
finally {
    Write-Host ''
    Write-Host 'The two applications were left open so you can inspect or save state.'
    Write-Host 'Close them manually when finished; the harness does not terminate either process.'
}
