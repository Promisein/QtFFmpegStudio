param(
    [string] $InputFile = '',
    [string] $BuildDirectory = (Join-Path $PSScriptRoot '..\build'),
    [string] $FfmpegRoot = (Join-Path $PSScriptRoot '..\..\FFmpegProject\ffmpeg-4.4-full_build-shared'),
    [int] $Runs = 3,
    [string] $OutputDirectory = ''
)

$ErrorActionPreference = 'Stop'
if ($Runs -lt 1) { throw 'Runs 必须至少为 1' }
$buildPath = (Resolve-Path -LiteralPath $BuildDirectory).Path
$ffmpegRootPath = (Resolve-Path -LiteralPath $FfmpegRoot).Path
$backendExe = Join-Path $buildPath 'FFmpegProjectBackend.exe'
$ffmpegExe = Join-Path $ffmpegRootPath 'bin\ffmpeg.exe'
$ffprobeExe = Join-Path $ffmpegRootPath 'bin\ffprobe.exe'
foreach ($toolPath in @($backendExe, $ffmpegExe, $ffprobeExe)) {
    if (-not (Test-Path -LiteralPath $toolPath -PathType Leaf)) { throw "缺少工具：$toolPath" }
}
if (-not $OutputDirectory) {
    $OutputDirectory = Join-Path $buildPath ('mux-perf-' + (Get-Date -Format 'yyyyMMdd-HHmmss') + '-' + [guid]::NewGuid().ToString('N').Substring(0, 8))
}
if (Test-Path -LiteralPath $OutputDirectory) { throw "输出目录已存在：$OutputDirectory" }
New-Item -ItemType Directory -Path $OutputDirectory -ErrorAction Stop | Out-Null
$reportPath = (Resolve-Path -LiteralPath $OutputDirectory).Path
if ($InputFile) {
    $inputPath = (Resolve-Path -LiteralPath $InputFile).Path
} else {
    $inputPath = Join-Path $reportPath 'fixture-1080x1728-30fps-4s.mp4'
    $fixtureLog = Join-Path $reportPath 'fixture-create.log'
    $ErrorActionPreference = 'Continue'
    & $ffmpegExe -hide_banner -loglevel error `
        -f lavfi -i 'testsrc2=size=1080x1728:rate=30:duration=4' `
        -f lavfi -i 'sine=frequency=440:sample_rate=48000:duration=4' `
        -c:v mpeg4 -q:v 5 -pix_fmt yuv420p -c:a aac -b:a 128k -shortest $inputPath *> $fixtureLog
    if ($LASTEXITCODE -ne 0) { throw "生成固定测试素材失败：$fixtureLog" }
}

function Read-MediaInfo {
    param([string] $MediaPath)
    $json = & $ffprobeExe -v error -show_entries `
        'format=duration,size:stream=codec_type,codec_name,duration,start_time,nb_frames' `
        -of json $MediaPath
    if ($LASTEXITCODE -ne 0) { throw "ffprobe 读取失败：$MediaPath" }
    return ($json | ConvertFrom-Json)
}

$inputInfo = Read-MediaInfo $inputPath
$inputSeconds = [double]::Parse($inputInfo.format.duration, [cultureinfo]::InvariantCulture)
$inputVideo = $inputInfo.streams | Where-Object codec_type -eq video | Select-Object -First 1
$inputFrames = [int]$inputVideo.nb_frames
if ($inputFrames -le 0) { throw "无法读取输入视频帧数：$inputPath" }

function Invoke-Case {
    param([string] $BackendName, [int] $RunNumber)
    $tag = "$BackendName-$RunNumber"
    $outputPath = Join-Path $reportPath "$tag.mp4"
    $logPath = Join-Path $reportPath "$tag.log"
    $arguments = @('-i', $inputPath, '-o', $outputPath,
                   '-vcodec', 'h264', '-vbackend', $BackendName,
                   '-acodec', 'aac', '-speed', '1')
    $timer = [System.Diagnostics.Stopwatch]::StartNew()
    $ErrorActionPreference = 'Continue' # FFmpeg diagnostic output is expected on stderr.
    & $backendExe @arguments *> $logPath
    $exitCode = $LASTEXITCODE
    $timer.Stop()
    if ($exitCode -ne 0) {
        throw "后端失败（$tag，退出码 $exitCode）：`n$((Get-Content -LiteralPath $logPath | Select-Object -Last 12) -join "`n")"
    }

    $timingLine = (Select-String -LiteralPath $logPath -Pattern 'Mux 性能统计:' |
        Select-Object -Last 1).Line
    if (-not $timingLine) { throw "缺少 Mux 性能统计：$logPath" }
    $timings = @{}
    foreach ($entry in [regex]::Matches($timingLine, '([a-z_]+)=([0-9.]+)')) {
        $timings[$entry.Groups[1].Value] = [double]::Parse(
            $entry.Groups[2].Value, [cultureinfo]::InvariantCulture)
    }
    foreach ($key in @('total_ms','params_wait_ms','video_pop_ms','audio_pop_ms',
                       'write_ms','trailer_ms','sleep_ms','cleanup_ms',
                       'sleep_calls','video_packets','audio_packets')) {
        if (-not $timings.ContainsKey($key)) { throw "计时字段缺失：$key，文件 $logPath" }
    }

    $info = Read-MediaInfo $outputPath
    $video = $info.streams | Where-Object codec_type -eq video | Select-Object -First 1
    $audio = $info.streams | Where-Object codec_type -eq audio | Select-Object -First 1
    if (-not $video -or -not $audio -or $video.codec_name -ne 'h264' -or $audio.codec_name -ne 'aac') {
        throw "输出音视频编码不符：$outputPath"
    }
    $videoSeconds = [double]::Parse($video.duration, [cultureinfo]::InvariantCulture)
    $audioSeconds = [double]::Parse($audio.duration, [cultureinfo]::InvariantCulture)
    $outputFrames = [int]$video.nb_frames
    $videoStart = [double]::Parse($video.start_time, [cultureinfo]::InvariantCulture)
    $audioStart = [double]::Parse($audio.start_time, [cultureinfo]::InvariantCulture)
    $startDelta = [math]::Abs($videoStart - $audioStart)
    $endDelta = [math]::Abs(($videoStart + $videoSeconds) - ($audioStart + $audioSeconds))
    if ([math]::Abs($videoSeconds - $inputSeconds) -gt 0.25 -or
        $startDelta -gt 0.1 -or $endDelta -gt 0.25 -or $outputFrames -ne $inputFrames) {
        throw "输出帧数、时长或音画同步不符：$outputPath"
    }
    $decodeLog = Join-Path $reportPath "$tag-decode.log"
    & $ffmpegExe -hide_banner -loglevel error -xerror -i $outputPath `
        -map '0:v:0' -map '0:a:0' -f null NUL *> $decodeLog
    if ($LASTEXITCODE -ne 0) { throw "完整解码失败：$outputPath" }

    return [pscustomobject]@{
        backend = $BackendName
        run = $RunNumber
        process_ms = [math]::Round($timer.Elapsed.TotalMilliseconds, 3)
        mux_ms = $timings.total_ms
        params_wait_ms = $timings.params_wait_ms
        video_pop_ms = $timings.video_pop_ms
        audio_pop_ms = $timings.audio_pop_ms
        write_ms = $timings.write_ms
        trailer_ms = $timings.trailer_ms
        sleep_ms = $timings.sleep_ms
        sleep_calls = $timings.sleep_calls
        output_bytes = [long]$info.format.size
        video_frames = $outputFrames
        video_seconds = $videoSeconds
        audio_seconds = $audioSeconds
        start_delta_seconds = [math]::Round($startDelta, 6)
        end_delta_seconds = [math]::Round($endDelta, 6)
        full_decode = 'pass'
        output = $outputPath
    }
}

$results = [System.Collections.Generic.List[object]]::new()
foreach ($name in @('software', 'nvenc')) {
    Write-Host "预热：$name"
    $null = Invoke-Case $name 0
}
for ($round = 1; $round -le $Runs; ++$round) {
    $order = if ($round % 2 -eq 1) { @('software', 'nvenc') } else { @('nvenc', 'software') }
    foreach ($name in $order) {
        Write-Host "测量：$name，第 $round 次"
        $results.Add((Invoke-Case $name $round))
    }
}
$csvPath = Join-Path $reportPath 'results.csv'
$results | Export-Csv -LiteralPath $csvPath -NoTypeInformation -Encoding UTF8
$results | Format-Table backend,run,process_ms,mux_ms,params_wait_ms,video_pop_ms,audio_pop_ms,write_ms,trailer_ms,sleep_ms -AutoSize
Write-Host "结果：$csvPath"
