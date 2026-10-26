param(
    [string] $BuildDirectory = (Join-Path $PSScriptRoot '..\build'),
    [string] $FfmpegRoot = (Join-Path $PSScriptRoot '..\..\FFmpegProject\ffmpeg-4.4-full_build-shared')
)

$ErrorActionPreference = 'Stop'
$build = (Resolve-Path -LiteralPath $BuildDirectory).Path
$sdk = (Resolve-Path -LiteralPath $FfmpegRoot).Path
$backend = Join-Path $build 'FFmpegProjectBackend.exe'
$ffmpeg = Join-Path $sdk 'bin\ffmpeg.exe'
$ffprobe = Join-Path $sdk 'bin\ffprobe.exe'
foreach ($tool in @($backend, $ffmpeg, $ffprobe)) {
    if (-not (Test-Path -LiteralPath $tool)) { throw "缺少工具：$tool" }
}

$reportDirectory = Join-Path $build ('correctness-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Path $reportDirectory | Out-Null

function Invoke-Logged {
    param([string] $Exe, [string[]] $Arguments, [string] $Log)
    $ErrorActionPreference = 'Continue' # Native stderr is expected from FFmpeg on Windows PowerShell.
    & $Exe @Arguments 2>&1 | ForEach-Object { $_.ToString() } |
        Set-Content -LiteralPath $Log -Encoding UTF8
    return $LASTEXITCODE
}

function Get-MediaInfo {
    param([string] $Path)
    $json = & $ffprobe -v error -show_entries `
        'format=duration:stream=codec_type,codec_name,width,height,duration,start_time' `
        -of json $Path
    if ($LASTEXITCODE -ne 0) { throw "ffprobe 读取失败：$Path" }
    return ($json | ConvertFrom-Json)
}

$shortInput = Join-Path $reportDirectory '输入 中文 空格.mp4'
$longInput = Join-Path $reportDirectory 'long-input.mp4'
foreach ($item in @(@{Path=$shortInput;Seconds=4}, @{Path=$longInput;Seconds=60})) {
    $args = @('-hide_banner','-loglevel','error',
              '-f','lavfi','-i',"testsrc2=size=320x180:rate=25:duration=$($item.Seconds)",
              '-f','lavfi','-i',"sine=frequency=1000:sample_rate=48000:duration=$($item.Seconds)",
              '-c:v','mpeg4','-q:v','5','-pix_fmt','yuv420p',
              '-c:a','aac','-b:a','128k','-shortest',$item.Path)
    $code = Invoke-Logged $ffmpeg $args (Join-Path $reportDirectory "fixture-$($item.Seconds).log")
    if ($code -ne 0) { throw "生成 $($item.Seconds) 秒测试素材失败，退出码：$code" }
}

$cases = @(
    @{Name='normal';  Input=$shortInput; Speed=1.0; Width=320; Height=180; Video='mpeg4'; Audio='ac3';  Args=@('-vcodec','mpeg4','-acodec','ac3')},
    @{Name='rotate90';Input=$shortInput; Speed=1.0; Width=180; Height=320; Video='h264';  Audio='aac';  Args=@('-rotate','90','-vcodec','h264','-acodec','aac')},
    @{Name='speed2';  Input=$shortInput; Speed=2.0; Width=320; Height=180; Video='mpeg4'; Audio='ac3';  Args=@('-speed','2.0','-vcodec','mpeg4','-acodec','ac3')},
    @{Name='speed05'; Input=$shortInput; Speed=0.5; Width=320; Height=180; Video='h264';  Audio='aac';  Args=@('-speed','0.5','-vcodec','h264','-acodec','aac')},
    @{Name='long60';  Input=$longInput;  Speed=1.0; Width=320; Height=180; Video='mpeg4'; Audio='ac3';  Args=@('-vcodec','mpeg4','-acodec','ac3')}
)

$results = foreach ($case in $cases) {
    $output = Join-Path $reportDirectory ($case.Name + '.mp4')
    $backendCode = Invoke-Logged $backend (@('-i',$case.Input,'-o',$output) + $case.Args) `
        (Join-Path $reportDirectory ($case.Name + '-backend.log'))
    $errors = [System.Collections.Generic.List[string]]::new()
    $videoDuration = $null
    $audioDuration = $null
    $videoStart = $null
    $audioStart = $null
    $videoCodec = ''
    $audioCodec = ''
    $width = 0
    $height = 0
    $decodeCode = -1
    $silenceIntervals = $null
    $clockPsnr = $null
    $counterclockwisePsnr = $null
    $inputDuration = [double](Get-MediaInfo $case.Input).format.duration
    $expectedDuration = $inputDuration / $case.Speed

    if ($backendCode -ne 0) {
        $errors.Add("后端退出码 $backendCode")
    } elseif (-not (Test-Path -LiteralPath $output)) {
        $errors.Add('没有输出文件')
    } else {
        try {
            $info = Get-MediaInfo $output
            $video = $info.streams | Where-Object codec_type -eq video | Select-Object -First 1
            $audio = $info.streams | Where-Object codec_type -eq audio | Select-Object -First 1
            if (-not $video -or -not $audio) {
                $errors.Add('缺少视频流或音频流')
            } else {
                $videoDuration = [double]$video.duration
                $audioDuration = [double]$audio.duration
                $videoStart = [double]$video.start_time
                $audioStart = [double]$audio.start_time
                $videoCodec = $video.codec_name
                $audioCodec = $audio.codec_name
                $width = [int]$video.width
                $height = [int]$video.height
                if ([math]::Abs($videoDuration - $expectedDuration) -gt 0.22) {
                    $errors.Add('视频时长偏差超过 0.22 秒')
                }
                if ([math]::Abs($audioDuration - $expectedDuration) -gt 0.22) {
                    $errors.Add('音频时长偏差超过 0.22 秒')
                }
                if ([math]::Abs($videoDuration - $audioDuration) -gt 0.22) {
                    $errors.Add('音视频结束时间差超过 0.22 秒')
                }
                if ([math]::Abs($videoStart - $audioStart) -gt 0.1) {
                    $errors.Add('音视频起始时间差超过 0.1 秒')
                }
                if ($width -ne $case.Width -or $height -ne $case.Height) {
                    $errors.Add('输出分辨率不符')
                }
                if ($videoCodec -ne $case.Video -or $audioCodec -ne $case.Audio) {
                    $errors.Add('输出编码类型不符')
                }
            }
            $decodeCode = Invoke-Logged $ffmpeg @('-hide_banner','-loglevel','error','-xerror',
                '-i',$output,'-map','0:v:0','-map','0:a:0','-f','null','NUL') `
                (Join-Path $reportDirectory ($case.Name + '-decode.log'))
            if ($decodeCode -ne 0) { $errors.Add("完整解码失败，退出码 $decodeCode") }
            if ($case.Name -eq 'normal') {
                $silenceLog = Join-Path $reportDirectory 'normal-silence.log'
                $silenceCode = Invoke-Logged $ffmpeg @('-hide_banner','-loglevel','info',
                    '-i',$output,'-af','silencedetect=noise=-40dB:d=0.008',
                    '-vn','-f','null','NUL') $silenceLog
                if ($silenceCode -ne 0) { $errors.Add('静音检查失败') }
                else {
                    $silenceIntervals = @(Select-String -LiteralPath $silenceLog -Pattern 'silence_start:').Count
                    if ($silenceIntervals -gt 3) { $errors.Add('连续测试音中出现过多短静音') }
                }
            }
            if ($case.Name -eq 'rotate90') {
                foreach ($direction in @('clock', 'cclock')) {
                    $psnrLog = Join-Path $reportDirectory ("rotate90-$direction-psnr.log")
                    $filter = "[0:v]transpose=$direction[ref];[ref][1:v]psnr"
                    $psnrCode = Invoke-Logged $ffmpeg @('-hide_banner','-loglevel','info',
                        '-i',$case.Input,'-i',$output,'-filter_complex',$filter,
                        '-an','-f','null','NUL') $psnrLog
                    if ($psnrCode -ne 0) {
                        $errors.Add("$direction 方向比较失败")
                        continue
                    }
                    $line = (Select-String -LiteralPath $psnrLog -Pattern 'PSNR y:.*average:' |
                        Select-Object -Last 1).Line
                    $match = [regex]::Match($line, 'average:([0-9.]+)')
                    if (-not $match.Success) {
                        $errors.Add("$direction 方向比较缺少 PSNR")
                        continue
                    }
                    if ($direction -eq 'clock') { $clockPsnr = [double]$match.Groups[1].Value }
                    else { $counterclockwisePsnr = [double]$match.Groups[1].Value }
                }
                if ($null -ne $clockPsnr -and $null -ne $counterclockwisePsnr -and
                    ($clockPsnr -lt 25 -or $clockPsnr -lt ($counterclockwisePsnr + 10))) {
                    $errors.Add('90 度旋转方向与画面不符')
                }
            }
        } catch {
            $errors.Add($_.Exception.Message)
        }
    }

    [pscustomobject]@{
        Case = $case.Name
        Passed = $errors.Count -eq 0
        InputSeconds = [math]::Round($inputDuration,3)
        ExpectedSeconds = [math]::Round($expectedDuration,3)
        VideoSeconds = $videoDuration
        AudioSeconds = $audioDuration
        VideoStartSeconds = $videoStart
        AudioStartSeconds = $audioStart
        VideoCodec = $videoCodec
        AudioCodec = $audioCodec
        Width = $width
        Height = $height
        BackendExit = $backendCode
        FullDecodeExit = $decodeCode
        SilenceIntervals = $silenceIntervals
        ClockwisePsnr = $clockPsnr
        CounterclockwisePsnr = $counterclockwisePsnr
        Errors = ($errors -join '；')
    }
}

$results | Export-Csv -LiteralPath (Join-Path $reportDirectory 'results.csv') `
    -NoTypeInformation -Encoding UTF8
$results | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $reportDirectory 'results.json') `
    -Encoding UTF8
$results | Format-Table Case,Passed,ExpectedSeconds,VideoSeconds,AudioSeconds,FullDecodeExit,SilenceIntervals,Errors -AutoSize
Write-Output "验证记录：$reportDirectory"
if (@($results | Where-Object { -not $_.Passed }).Count -gt 0) { exit 1 }
