<#
.SYNOPSIS
    PowerShell script to download Sonar BuildWrapper for Windows MSBuild
.DESCRIPTION
    Download "build-wrapper-win-x86.zip" and extract it to given path.

.PARAMETER Destination
    Folder to move "build-wrapper-win-x86-64.exe"
#>
using namespace System
param
(
    [String]$Uri = "https://sonarcloud.io/static/cpp/build-wrapper-win-x86.zip",
    [String]$Destination = (Get-Location)
)
Invoke-WebRequest -Uri $Uri -OutFile "build-wrapper-win.zip"
Expand-Archive -Path "build-wrapper-win.zip" -DestinationPath $Destination
Move-Item -Force -Path "./build-wrapper-win-x86/build-wrapper-win-x86-64.exe" -Destination $Destination
