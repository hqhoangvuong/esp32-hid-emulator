# Compiles every "types" snippet from main/macros/snippets.txt inside a temporary copy of
# scratch-workspace, each wrapped in its own namespace. Needs the .NET SDK and a host build of
# check_script (cmake --build build-sim --config Debug). Exit code is dotnet's.
param(
    [string]$CheckScript = (Join-Path $PSScriptRoot '..\..\build-sim\check_script\Debug\check_script.exe')
)
$ErrorActionPreference = 'Stop'
$repo = Resolve-Path (Join-Path $PSScriptRoot '..\..')
$tmp = Join-Path ([IO.Path]::GetTempPath()) ('snippet-check-' + [Guid]::NewGuid().ToString('N').Substring(0, 8))

Copy-Item -Recurse (Join-Path $repo 'scratch-workspace') $tmp
Remove-Item -Recurse -Force (Join-Path $tmp 'bin'), (Join-Path $tmp 'obj') -ErrorAction SilentlyContinue
try {
    & $CheckScript (Join-Path $repo 'main\macros\developer.txt') (Join-Path $repo 'main\macros\snippets.txt') `
        --emit-cs (Join-Path $tmp 'Snippets.cs')
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    dotnet build $tmp --nologo -v q
    exit $LASTEXITCODE
}
finally {
    Remove-Item -Recurse -Force $tmp -ErrorAction SilentlyContinue
}
