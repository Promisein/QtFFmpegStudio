param(
    [string] $BuildDirectory = (Join-Path $PSScriptRoot '..\build'),
    [string] $QtBin = 'D:\QT\6.8.3\mingw_64\bin',
    [string] $MinGWBin = 'D:\QT\Tools\mingw1310_64\bin',
    [string] $Destination = ''
)

$ErrorActionPreference = 'Stop'
$build = (Resolve-Path -LiteralPath $BuildDirectory).Path
$qt = (Resolve-Path -LiteralPath $QtBin).Path
$mingw = (Resolve-Path -LiteralPath $MinGWBin).Path
if (-not $Destination) {
    $Destination = Join-Path $PSScriptRoot ('..\dist\QtFFmpegStudio-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
}
$target = [System.IO.Path]::GetFullPath($Destination)
if ($target.TrimEnd('\') -eq $build.TrimEnd('\')) {
    throw '打包目录不能与构建目录相同。'
}
if (Test-Path -LiteralPath $target) {
    throw "打包目录已存在，请选择一个新目录：$target"
}
$required = @('QtFFmpegStudio.exe', 'FFmpegProjectBackend.exe', 'ffprobe.exe',
              'avcodec-58.dll', 'avformat-58.dll', 'avutil-56.dll',
              'swresample-3.dll', 'swscale-5.dll',
              'libgcc_s_seh-1.dll', 'libstdc++-6.dll', 'libwinpthread-1.dll')
foreach ($name in $required) {
    if (-not (Test-Path -LiteralPath (Join-Path $build $name))) {
        throw "构建目录缺少 $name；请先构建 QtFFmpegStudio 目标。"
    }
}
$deployTool = Join-Path $qt 'windeployqt.exe'
if (-not (Test-Path -LiteralPath $deployTool)) {
    throw "找不到 windeployqt：$deployTool"
}

New-Item -ItemType Directory -Path $target | Out-Null
foreach ($name in @('QtFFmpegStudio.exe', 'FFmpegProjectBackend.exe', 'ffprobe.exe')) {
    Copy-Item -LiteralPath (Join-Path $build $name) -Destination $target
}
Get-ChildItem -LiteralPath $build -Filter '*.dll' -File | ForEach-Object {
    Copy-Item -LiteralPath $_.FullName -Destination $target
}

$originalPath = $env:PATH
try {
    $env:PATH = "$qt;$mingw;$originalPath"
    & $deployTool --no-translations --dir $target (Join-Path $target 'QtFFmpegStudio.exe')
    if ($LASTEXITCODE -ne 0) { throw "windeployqt 退出码：$LASTEXITCODE" }
} finally {
    $env:PATH = $originalPath
}
foreach ($name in @('QtFFmpegStudio.exe', 'FFmpegProjectBackend.exe', 'ffprobe.exe',
                    'Qt6Core.dll', 'Qt6Gui.dll', 'Qt6Widgets.dll',
                    'platforms\qwindows.dll', 'avcodec-58.dll',
                    'libstdc++-6.dll')) {
    if (-not (Test-Path -LiteralPath (Join-Path $target $name))) {
        throw "打包结果缺少 $name：$target"
    }
}
Write-Output "打包完成：$target"
