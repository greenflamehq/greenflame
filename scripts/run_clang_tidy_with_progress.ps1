param(
    [string]$CompilationDb = "build/x64-debug-clang",
    [string]$LogFile = "clang-tidy-full.log",
    [string]$ProgressFile = "clang-tidy-progress.log",
    [string]$StatusFile = "clang-tidy-status.txt",
    [int]$MaxFiles = 0
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$script_dir = Split-Path -Parent $PSCommandPath
$repo_root = Split-Path -Parent $script_dir
Set-Location $repo_root

function Resolve-RepoPath {
    param([string]$PathValue)

    if ([System.IO.Path]::IsPathRooted($PathValue)) {
        return $PathValue
    }

    return Join-Path $repo_root $PathValue
}

function Write-ProgressLine {
    param([string]$Message)

    $timestamp = Get-Date -Format "yyyy-MM-dd HH:mm:ss"
    $line = "[$timestamp] $Message"
    Add-Content -Path $progress_path -Value $line -Encoding utf8
    Write-Output $line
}

$log_path = Resolve-RepoPath $LogFile
$progress_path = Resolve-RepoPath $ProgressFile
$status_path = Resolve-RepoPath $StatusFile

Set-Content -Path $log_path -Value "" -Encoding utf8
Set-Content -Path $progress_path -Value "" -Encoding utf8
Set-Content -Path $status_path -Value "running" -Encoding utf8

$files = @(Get-ChildItem src,tests -Recurse -Filter *.cpp | Sort-Object FullName)
if ($MaxFiles -gt 0 -and $MaxFiles -lt $files.Count) {
    $files = @($files | Select-Object -First $MaxFiles)
}
$total = $files.Count
$start_time = Get-Date
$had_errors = $false

Write-ProgressLine "RUN START files=$total log=$log_path"

try {
    for ($index = 0; $index -lt $total; $index++) {
        $file = $files[$index]
        $current = $index + 1
        $display = $file.FullName

        Write-ProgressLine "START $current/$total $display"
        Add-Content -Path $log_path -Value "=== $display ===" -Encoding utf8

        & clang-tidy -p $CompilationDb $display *>> $log_path
        $exit_code = $LASTEXITCODE
        if ($exit_code -ne 0) {
            $had_errors = $true
        }

        Write-ProgressLine "DONE $current/$total exit=$exit_code $display"
    }

    $duration = [int]((Get-Date) - $start_time).TotalSeconds
    $overall_exit = if ($had_errors) { 1 } else { 0 }
    $status = "completed exit=$overall_exit files=$total duration_seconds=$duration"
    Set-Content -Path $status_path -Value $status -Encoding utf8
    Write-ProgressLine "RUN COMPLETE exit=$overall_exit files=$total duration_seconds=$duration"
    exit $overall_exit
} catch {
    $duration = [int]((Get-Date) - $start_time).TotalSeconds
    $message = $_.Exception.Message
    $status = "failed duration_seconds=$duration message=$message"
    Set-Content -Path $status_path -Value $status -Encoding utf8
    Write-ProgressLine "RUN FAILED duration_seconds=$duration message=$message"
    throw
}
