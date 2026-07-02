param(
    [Parameter(Mandatory = $true)]
    [ValidateSet("Add", "Remove")]
    [string] $Action,

    [Parameter(Mandatory = $true)]
    [ValidateSet("User", "Machine")]
    [string] $Scope,

    [Parameter(Mandatory = $true)]
    [string] $Entry
)

$ErrorActionPreference = "Stop"

if ($Scope -eq "Machine") {
    $env_key = "HKLM:\SYSTEM\CurrentControlSet\Control\Session Manager\Environment"
    $uninstall_key = "HKLM:\Software\Microsoft\Windows\CurrentVersion\Uninstall\greenflame"
} else {
    $env_key = "HKCU:\Environment"
    $uninstall_key = "HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\greenflame"
}

function Get-PathValue {
    $path_value = Get-ItemProperty -Path $env_key -Name Path -ErrorAction SilentlyContinue
    if ($null -eq $path_value) {
        return ""
    }

    return [string] $path_value.Path
}

function Set-PathValue {
    param([string] $Value)

    New-ItemProperty -Path $env_key -Name Path -Value $Value -PropertyType ExpandString -Force | Out-Null
}

function Get-PathParts {
    param([string] $Value)

    if ([string]::IsNullOrEmpty($Value)) {
        return @()
    }

    return @($Value -split ";" | Where-Object { $_ -ne "" })
}

function Test-PathPart {
    param(
        [string[]] $Parts,
        [string] $Value
    )

    foreach ($part in $Parts) {
        if ([string]::Equals($part, $Value, [StringComparison]::OrdinalIgnoreCase)) {
            return $true
        }
    }

    return $false
}

function Set-PathOwnership {
    param(
        [bool] $Added,
        [string] $PathEntry
    )

    if (-not (Test-Path -LiteralPath $uninstall_key)) {
        New-Item -Path $uninstall_key -Force | Out-Null
    }

    New-ItemProperty -Path $uninstall_key -Name PathAddedByInstaller -Value ([int] $Added) -PropertyType DWord -Force | Out-Null
    if ($Added) {
        New-ItemProperty -Path $uninstall_key -Name PathEntry -Value $PathEntry -PropertyType String -Force | Out-Null
    } else {
        Remove-ItemProperty -Path $uninstall_key -Name PathEntry -ErrorAction SilentlyContinue
    }
}

if ($Action -eq "Add") {
    $current_path = Get-PathValue
    $parts = Get-PathParts $current_path

    if (Test-PathPart -Parts $parts -Value $Entry) {
        $ownership = Get-ItemProperty -Path $uninstall_key -ErrorAction SilentlyContinue
        if ($null -ne $ownership -and
            $ownership.PathAddedByInstaller -eq 1 -and
            [string]::Equals($ownership.PathEntry, $Entry, [StringComparison]::OrdinalIgnoreCase)) {
            Set-PathOwnership $true $Entry
        } else {
            Set-PathOwnership $false ""
        }

        return
    }

    if ([string]::IsNullOrEmpty($current_path)) {
        $new_path = $Entry
    } else {
        $new_path = $current_path.TrimEnd(";") + ";" + $Entry
    }

    Set-PathValue $new_path
    Set-PathOwnership $true $Entry
    return
}

$ownership = Get-ItemProperty -Path $uninstall_key -ErrorAction SilentlyContinue
if ($null -eq $ownership -or
    $ownership.PathAddedByInstaller -ne 1 -or
    [string]::IsNullOrEmpty($ownership.PathEntry)) {
    return
}

$current_path = Get-PathValue
$parts = Get-PathParts $current_path
$new_parts = @($parts | Where-Object {
    -not [string]::Equals($_, $ownership.PathEntry, [StringComparison]::OrdinalIgnoreCase)
})

if ($new_parts.Count -ne $parts.Count) {
    Set-PathValue ($new_parts -join ";")
}
