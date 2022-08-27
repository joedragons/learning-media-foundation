<#
.SYNOPSIS
    PowerShell script to create coverage for SonarCloud Analysis
.DESCRIPTION
    https://docs.microsoft.com/en-us/visualstudio/test/using-code-coverage-to-determine-how-much-code-is-being-tested
#>
using namespace System
param
(
    [String]$ReportPath = "reports"
)
# Codecoverage.exe (Azure Pipelines)
$env:Path = "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\Team Tools\Dynamic Code Coverage Tools;$env:Path"

# Acquire coverage file and generate temporary coverage file
$coverage_files = Get-ChildItem -Recurse "$ReportPath/**/*.coverage";
if ($null -eq $coverage_files) {
    Write-Output "coverage file not found"
    return
}
$coverage_file = $coverage_files[0] 
# Write-Output $coverage_file

$coverage_xml_file = "$ReportPath/coverage-report.coveragexml"
CodeCoverage analyze /output:$coverage_xml_file $coverage_file

# Get-ChildItem "./TestResults"

# Filter lines with invalid line number 
#   and Create a new coverage xml
# $final_coverage_xml_filepath = "./TestResults/luncliff-media.coveragexml"
# $xml_lines = Get-Content $temp_coverage_xml_filepath
# foreach ($text in $xml_lines) {
#     if ($text -match 15732480) {
#         Write-Output "removed $text"
#         continue;
#     }
#     else {
#         Add-Content $final_coverage_xml_filepath $text;
#     }
# }
# Tree ./TestResults /F

# Display information of a new coverage xml
# Get-ChildItem $final_coverage_xml_filepath
# Get-Content   $final_coverage_xml_filepath
