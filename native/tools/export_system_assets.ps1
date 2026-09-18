$ErrorActionPreference='Stop'
$repo=Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
Push-Location $repo
try {
 & npx --yes --package @playwright/cli playwright-cli -s=pico-system-assets eval '() => ({ready:document.body.dataset.ready,bytes:window.exportPNG?.length})'
 & npx --yes --package @playwright/cli playwright-cli -s=pico-system-assets run-code 'async (page) => { await page.waitForFunction(() => document.body.dataset.ready === "true", null, {timeout:120000}); const pending = page.waitForEvent("download"); await page.evaluate(() => { const a=document.createElement("a"); a.href=window.exportPNG; a.download="rendered.png"; a.click(); }); const download=await pending; await download.saveAs("native/assets/rendered.png"); }'
 if($LASTEXITCODE -ne 0){throw 'Asset export failed'}
 & python native/tools/prepare_assets.py
 if($LASTEXITCODE -ne 0){throw 'Asset preparation failed'}
}finally{Pop-Location}
