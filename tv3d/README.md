# PICO 电视模型与预览

现代宽屏 LCD，连续机械几何与像素贴图。当前 75 个网格、30,124 个三角形，屏幕贴图 168 × 96；倒角、螺丝、散热孔和接口使用真实几何。

## 生成与验证

在 tv3d 目录执行：

```powershell
python -m pip install -r requirements.txt
python build_tv.py
npm ci
python validate_model.py
node validate_gltf.mjs
npm start
```

打开 http://127.0.0.1:8765。生成的 tv_bot.glb、textures 和预览图不提交，model.json 记录尺寸及屏幕表情路径。build_tv.py 只保留当前 LCD 模型实现。运行 python package_desktop.py 打包模型；预览 PNG 仅在导出时间不早于模型时加入。

## 模型约定

根节点 pet-root 位于地面中心，脚底 Y=0，+Y 向上，+Z 向前；body 包含组件，screen-display 为独立屏幕。previewOffsetY 只用于预览取景。GLB 使用 KHR_materials_unlit 与顶点色，无骨骼或动画剪辑，当前清单无独立旋转枢轴；原生应用按部件名实现天线变形。

LCD 不含 CRT 扫描线或辉光。Three.js 替换屏幕贴图使用 flipY=false、SRGBColorSpace、最近邻过滤，关闭 mipmap。屏幕不透明，无需透明排序。原生应用另有轻量材质光照。

## 更新原生资源

先生成并验证模型，再从仓库根目录执行：

```powershell
python native/tools/prepare_realtime_model.py
python native/tools/prepare_screen_mesh.py
powershell -NoProfile -ExecutionPolicy Bypass -File native/build.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File native/tools/run_checks.ps1
```

生成的 realtime-model.bin、realtime-atlas.png、realtime-faces.png 与 screen-mesh.h 是编译输入，需要提交。仓库已包含全部嵌入资源，日常 C++ 修改不需要运行模型工具。

body-* / faces-* 及 hd-body-* / hd-faces-* 离线姿态仍用于兼容路径和检查。重新生成使用 native/tools/export_system_assets.ps1、export_hd_assets.ps1 和 prepare_assets.py；需要本地预览服务器、开发用 Three.js 与 Playwright CLI，参数见脚本。
