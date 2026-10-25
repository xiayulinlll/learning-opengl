$ErrorActionPreference = 'Continue'
Set-Location 'D:\OpenGL\start-learning-opengl-main'
$log = 'D:\OpenGL\start-learning-opengl-main\_run.out'
function Log($m) { $t = (Get-Date).ToString('HH:mm:ss'); Add-Content -Path $log -Value "[$t] $m" -Encoding UTF8 }
Set-Content -Path $log -Value "=== base commit (exact paths) ===" -Encoding UTF8
$env:GIT_TERMINAL_PROMPT = '0'

$big = @(
  'lib/libassimp.a',
  'lib/libassimp.dll.a',
  'output/libassimp.dll',
  'src/41_shadow_mapping/images/2021-11-25_155337.gif',
  'static/model/cerberus/Cerberus.obj',
  'static/model/nanosuit/nanosuit.blend',
  'static/model/rock/Rock-Texture-Surface.jpg',
  'static/texture/Alexs_Apt_2k.hdr',
  'static/texture/Ditch-River_2k.hdr'
)

git read-tree --empty 2>&1 | Out-Null
Log "empty-index exit=$LASTEXITCODE"

$gitArgs = @('add', '-A', '--', '.')
foreach ($p in $big) { $gitArgs += (':(exclude,literal)' + $p) }
& git @gitArgs 2>&1 | ForEach-Object { Log "git: $_" }
Log "add exit=$LASTEXITCODE staged=$(@(git ls-files).Count)"

$stillBig = @(git ls-files | Where-Object { $big -contains $_ })
Log "big_still_staged=$($stillBig.Count)"
$stillBig | ForEach-Object { Log "STILL: $_" }

$tree = (git write-tree).Trim()
$commit = (git commit-tree $tree -m "feat: OpenGL learning examples (learnopengl-cn)").Trim()
Log "tree=$tree commit=$commit"
git update-ref refs/heads/main $commit 2>&1 | ForEach-Object { Log "ref: $_" }
git symbolic-ref HEAD refs/heads/main 2>&1 | Out-Null
git reset -q 2>&1 | Out-Null
Log "head=$(git rev-parse --short HEAD) tracked=$(@(git ls-files).Count)"
Log "=== done ==="
