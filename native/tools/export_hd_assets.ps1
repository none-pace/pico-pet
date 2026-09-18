$ErrorActionPreference='Stop'
$repo=Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
Push-Location $repo
try {
 for($pitch=0;$pitch -lt 4;$pitch++){
  $action=if($pitch -eq 0){'open'}else{'goto'}
  & npx --yes --package @playwright/cli playwright-cli -s=pico-hd $action "http://127.0.0.1:8874/export-native.html?hd=1&pitch=$pitch&revision=$([DateTime]::UtcNow.Ticks)"
  if($LASTEXITCODE -ne 0){throw 'Export page failed'}
  $destination=(Join-Path $repo "native/assets/hd-rendered-$pitch.png").Replace('\','/') | ConvertTo-Json -Compress
  $code='async (page) => { await page.waitForFunction(() => document.body.dataset.ready === "true", null, {timeout:120000}); const pending=page.waitForEvent("download"); await page.evaluate(() => {const a=document.createElement("a");a.href=window.exportPNG;a.download="rendered.png";a.click();});const download=await pending;await download.saveAs(DESTINATION); }'.Replace('DESTINATION',$destination)
  & npx --yes --package @playwright/cli playwright-cli -s=pico-hd run-code $code
  if($LASTEXITCODE -ne 0){throw 'HD download failed'}
 }
 & python native/tools/prepare_assets.py --hd
 if($LASTEXITCODE -ne 0){throw 'HD preparation failed'}
}finally{Pop-Location}
