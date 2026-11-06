# 重建 Estuary v1

原发布版本固定为官方 CLI/SDK **3.0.5**，历史测试引擎 **1.0.3**。源码只依赖官方 C++ SDK 和标准库；本仓库保留 `arena/cambc.toml` 的原始版本配置，SDK 本体由官方工具提供。

以下为新 Windows 环境的 PowerShell 操作，在仓库根目录执行。首次初始化和构建需要联网获取官方依赖。归档时没有执行这些命令。

```powershell
py -m venv .venv
& ./.venv/Scripts/python.exe -m pip install cambc==3.0.5
& ./.venv/Scripts/cambc.exe --version
& ./.venv/Scripts/cambc.exe init --yes fresh-arena
Copy-Item -LiteralPath ./arena/bots/estuary -Destination ./fresh-arena/bots -Recurse
Copy-Item -LiteralPath ./arena/cambc.toml -Destination ./fresh-arena/cambc.toml -Force
New-Item -ItemType Directory -Force -Path ./build | Out-Null
Push-Location ./fresh-arena
try {
    & ../.venv/Scripts/cambc.exe build estuary --release --output ../build/estuary.wasm
    if ($LASTEXITCODE -ne 0) { throw 'Release build failed' }
    & ../.venv/Scripts/cambc.exe bundle estuary --output ../build/estuary-submission.zip
    if ($LASTEXITCODE -ne 0) { throw 'Source bundle failed' }
} finally {
    Pop-Location
}
```

`fresh-arena` 应为首次创建的新目录。重建结果写入 `build/`，原始归档产物保留在 `artifacts/`。构建说明不启动对局，不提交到比赛平台。

如果固定版本的官方依赖不可用，应保留错误并记录新的工具版本，不能把其他 SDK 构建的 WASM 冒充原始产物。源码包只有 `src/` 中的六份文件，SDK 版本另记录于配置、清单和历史证据摘要。

## 验证下载的归档

在仓库根目录执行下列只读校验。它核对 `SHA256SUMS` 列出的全部文件，不需要安装游戏工具。

```powershell
$ErrorActionPreference = 'Stop'
Get-Content -LiteralPath ./SHA256SUMS | ForEach-Object {
    $parts = $_ -split '  ', 2
    if ($parts.Count -ne 2) { throw 'Invalid checksum entry' }
    $actual = (Get-FileHash -LiteralPath $parts[1] -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($actual -ne $parts[0]) { throw ('Checksum mismatch: ' + $parts[1]) }
}
'All archived file checksums match.'
```

`SHA256SUMS` 包括清单本身，不包括自身；清单列出其生成前的有效载荷文件，因此不会形成循环哈希。
