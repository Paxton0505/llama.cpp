# 系統規格與 llama.cpp 建置基準

> 更新日期：2026-10-16。硬體數值取自本機 Windows / WMI / `nvidia-smi` / `nvcc`；即時可用記憶體會變動，拓撲狀態請在更換驅動或硬體後重查。

## 主機規格

| 項目 | 本機資料 |
| --- | --- |
| 作業系統 | Windows 11 Pro for Workstations，版本 `10.0.26300`，Build `26300` |
| 系統廠牌 / 型號 | WMI Manufacturer: HUANANZHI；Model 未回報 |
| CPU | Intel Xeon E5-2696 v4 @ 2.20 GHz；22 核心、44 邏輯處理器 |
| 系統記憶體 | 127.9 GiB；取樣時可用 111.6 GiB |
| GPU | 2 x NVIDIA GeForce RTX 2080 Ti，使用者確認為 22 GB modded 卡 |
| GPU 記憶體 | 每卡 `22528 MiB`（約 22 GiB），由 `nvidia-smi` 回報 |
| GPU 架構 | Turing，Compute Capability `7.5` / SM75 |
| GPU PCI 位址 | GPU0 `03:00.0`；GPU1 `04:00.0` |
| NVIDIA 驅動 | `617.42`；每卡 power limit `280 W` |
| CUDA Toolkit | `13.4.92`，路徑 `C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v13.4` |

### GPU 互連狀態

`nvidia-smi nvlink -s` 在兩張卡上都列出 Link 0、Link 1，各為 `25.781 GB/s`；`nvidia-smi topo -p2p r/w/n` 對兩卡都回報 `OK`，但 `nvidia-smi topo -m` 在本機執行失敗。兩張卡目前皆為 Windows `WDDM` driver mode。llama.cpp 執行時呼叫 `cudaDeviceCanAccessPeer` 則回報 `cudaSuccess`、`can_access=0`；因此 NVIDIA topology 能力表不能代表本程序可實際啟用 CUDA peer access。一般 `GGML_CUDA_P2P` 開關已做 MTP 關閉的生成 A/B，但這不等同於驗證專用 P2P all-reduce kernel。

## 目前專案設定

- 啟動設定位於本機 workspace root 的 `SERVER_PROFILES.md`，入口為 `start.ps1`；Q6 profile 使用 `engines\paxton0505\llama.cpp\bin\llama-server.exe`，Q8 profile 仍指向舊的 `engines\llama.cpp\llama-server.exe`。
- 目前模型為 Qwen3.8-27B 的 Q6_K_XL / Q8_K_XL GGUF，另有 BF16 mmproj。
- Q6 profile 使用雙卡 tensor split（`--tensor-split 33,33`）、Flash Attention、`--ctx-size 196608`、batch/ubatch `512/128`、Q8_0 K/V cache 與 adaptive MTP draft 範圍 2–10。Q8 profile 是舊版設定，使用 batch/ubatch `2048/512` 與固定 MTP draft 3。
- 目前 profile 是服務執行參數，不代表 llama.cpp 的 CMake build flags；修改執行參數前，需以實際模型、上下文長度與 VRAM 使用量重新測試。

## llama.cpp 最佳建置參數

以下資料取自目前已成功建置的 `engines/paxton0505/llama.cpp/CMakeCache.txt`，source directory 為 `repos/llama.cpp`。這是以雙 RTX 2080 Ti（SM75）與 Xeon E5-2696 v4 為目標的本機最佳組態；不代表其他 GPU 或主機的最佳值。

| 分類 | 已驗證設定 |
| --- | --- |
| Source | 本機 workspace 的 `repos/llama.cpp`；整合目標為 fork `master`，adaptive MTP implementation commit `556da39b75` |
| Build tree / generator | `engines/paxton0505/llama.cpp` / Ninja |
| Build type | `Release` |
| Host compiler | MSVC `14.51.36231` toolset；binary 顯示 MSVC `19.51.36260.0` |
| CUDA compiler / arch | CUDA `13.4.92` / `CMAKE_CUDA_ARCHITECTURES=75` |
| CUDA / FA / graphs | `GGML_CUDA=ON`、`GGML_CUDA_FA=ON`、`GGML_CUDA_GRAPHS=ON` |
| Flash Attention quant pairs | `q4_0-q4_0;q8_0-q8_0;f16-f16;bf16-bf16` |
| CPU kernels | `GGML_NATIVE=ON`，針對本機 Xeon 編譯 |
| Forced matmul paths | `GGML_CUDA_FORCE_CUBLAS=OFF`、`GGML_CUDA_FORCE_MMQ=OFF`，保留自動選擇 |
| Targets | `LLAMA_BUILD_SERVER=ON`、`LLAMA_BUILD_TESTS=ON`、`LLAMA_BUILD_EXAMPLES=ON` |
| NCCL option | Cache 為 `GGML_CUDA_NCCL=ON`；Windows runtime 預設仍選 internal all-reduce，不應只憑此 cache 值假定正在使用 NCCL |

從本機 workspace root 的 Visual Studio Developer Shell 可重建相同組態：

```powershell
$vs = 'C:\Program Files\Microsoft Visual Studio\18\Community'
Import-Module "$vs\Common7\Tools\Microsoft.VisualStudio.DevShell.dll"
Enter-VsDevShell -VsInstanceId '448708da' -Arch amd64 -HostArch amd64 -SkipAutomaticLocation -DevCmdArguments '-no_logo'
$cmake = "$vs\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
$src = 'repos/llama.cpp'
$build = 'engines/paxton0505/llama.cpp'

& $cmake -S $src -B $build -G Ninja `
    -DCMAKE_BUILD_TYPE=Release `
    -DCMAKE_C_COMPILER='C:/Program Files/Microsoft Visual Studio/18/Community/VC/Tools/MSVC/14.51.36231/bin/Hostx64/x64/cl.exe' `
    -DCMAKE_CXX_COMPILER='C:/Program Files/Microsoft Visual Studio/18/Community/VC/Tools/MSVC/14.51.36231/bin/Hostx64/x64/cl.exe' `
    -DCMAKE_CUDA_COMPILER='C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v13.4/bin/nvcc.exe' `
    -DCMAKE_CUDA_ARCHITECTURES=75 `
    -DGGML_CUDA=ON `
    -DGGML_CUDA_FA=ON `
    '-DGGML_CUDA_FA_QUANTS=q4_0-q4_0;q8_0-q8_0;f16-f16;bf16-bf16' `
    -DGGML_CUDA_GRAPHS=ON `
    -DGGML_CUDA_NCCL=ON `
    -DGGML_CUDA_FORCE_CUBLAS=OFF `
    -DGGML_CUDA_FORCE_MMQ=OFF `
    -DGGML_NATIVE=ON `
    -DLLAMA_BUILD_SERVER=ON `
    -DLLAMA_BUILD_TESTS=ON `
    -DLLAMA_BUILD_EXAMPLES=ON
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

& $cmake --build $build --target llama-server test-speculative-adaptive --parallel 44
```

`--parallel 44` 使用本機 44 個邏輯處理器；若同時執行其他工作或記憶體壓力過大，降為 `--parallel 22`。`SM75` 與 `GGML_NATIVE=ON` 都是依本機硬體選擇，移至其他機器重建時應重新設定。一般 `GGML_CUDA_P2P` 是執行期選項，A/B 未顯示本機吞吐收益，故不列為預設 build flag 或服務環境變數。

目前 clean build binary 回報 `0.7.0-dev`、build `11687`、commit `0b5ee9ccbb`；這是合併 fork `master` 歷史後、加入文件提交前的 binary。每次修改 source 後應重新建置，並用 `llama-server.exe --version` 更新 binary 資訊。

## 2080 Ti 優化參考

參考專案：[Lisiyuan233/rtx-2080ti-llm-optimization](https://github.com/Lisiyuan233/rtx-2080ti-llm-optimization)。該專案測試平台是 3 張 22 GB RTX 2080 Ti（其中 GPU0/1 使用 NVLink）、雙 Xeon E5-2680 v2、Ubuntu 24.04；模型、OS、CPU、llama.cpp 版本都與本機不同，效能數字不可直接視為本機預期值。

### 可優先驗證的設定

- 雙卡優先使用互連良好的 GPU pair 與等比例 tensor split。該專案在其 NVLink pair 上，雙卡 tensor split 1:1 明顯勝過包含無 NVLink GPU 的三卡配置；本機目前也只有兩張卡，但仍應先確認 peer access。
- MTP draft 上限 3 在該專案測試中優於 4、5、6；本機現有 profile 已設為 3，可先保留。
- 該專案測試的 MTP 工作負載中，F16 KV 快於 Q8_0 KV；本機 profile 目前選 Q8_0，且模型量化與 196608 context 不同。僅在 VRAM 足夠時比較 F16 與 Q8_0，避免直接切換導致載入失敗或上下文容量下降。
- 該專案測試 40 多種 CPU / NUMA / thread / polling 組合，結果多在噪音範圍；先保留 llama.cpp 預設 CPU 執行緒與等待策略，不預設加入 affinity、poll 或 wait-mode 調校。

### P2P A/B 與 all-reduce patch（實驗性）

一般 `GGML_CUDA_P2P` 開關以同一 Q6 模型、30-token prompt、128-token generation、相同雙卡 tensor split 與 batch/ubatch 設定測試；為隔離 MTP 路徑，兩組均未啟用 speculative decoding：

| 設定 | Generation throughput |
| --- | ---: |
| P2P off，兩次 | 22.64、22.52 tok/s（平均 22.58） |
| P2P on，兩次 | 22.33、21.38 tok/s（平均 21.86） |

這組小樣本沒有顯示 P2P 收益；平均值約低 3.2%，不足以判定穩定退化，但不應據此把 P2P 設為預設。此結果不是 adaptive MTP 效能數據。Q6 adaptive MTP 的 metadata pool headroom 已由 16 提高到 64；相同雙卡 profile 的實機請求已成功完成，clean runtime 樣本為 19-token prompt、47-token completion、42 個 draft tokens 中接受 30 個，生成速度約 33.74 tok/s。

參考專案另提供獨立的雙卡 CUDA P2P all-reduce patch，依賴 `GGML_CUDA_ALLREDUCE=internal`，其原始測試基準為 llama.cpp commit `9723942`。本機已將 opt-in device-staging kernel 移植到 `repos/llama.cpp/ggml/src/ggml-cuda/allreduce.cu`，`llama-server` 可成功編譯連結，且 `test-speculative-adaptive` 通過。使用 `GGML_CUDA_P2P=1` 啟動 Q6 雙卡 server 時，CUDA runtime 回報 `cudaDeviceCanAccessPeer(...)=cudaSuccess` 但 `can_access=0`，pipeline 因而選用 host-staging fallback，沒有執行新 kernel。fallback 路徑的 23-token chat prompt / 43-token completion 正常完成，測得 `22.05 tok/s`；這不是 direct P2P kernel 的正確性或效能結果。雖然 `nvidia-smi topo -p2p r/w/n` 顯示 `OK`，目前兩張卡均使用 `WDDM`，peer access 的落差原因尚未在其他 driver mode 驗證。direct kernel 尚未完成 runtime correctness / A-B 測試；目前僅以 opt-in 實驗功能推送，不應列為已驗證最佳化或設為預設。外部專案在其 NVLink 雙卡平台報告短生成約 +4–5%、長生成約 +1.5–3%，不可直接當成本機預期值。

更多實驗條件與限制見 [結果表](https://github.com/Lisiyuan233/rtx-2080ti-llm-optimization/blob/main/RESULTS.md)、[CPU 編排測試](https://github.com/Lisiyuan233/rtx-2080ti-llm-optimization/blob/main/docs/CPU-ORCHESTRATION.zh-CN.md) 與 [P2P patch 說明](https://github.com/Lisiyuan233/rtx-2080ti-llm-optimization/blob/main/patches/llama.cpp/README.md)。

## 重查清單

- GPU、驅動或主機板更動後，重新記錄 `nvidia-smi --query-gpu=name,memory.total,pci.bus_id,driver_version --format=csv`、`nvidia-smi nvlink -s` 與 `nvidia-smi topo -m`。
- CPU 更動後，重新查核心與邏輯處理器數，更新 `--parallel`。
- 更新 llama.cpp 或套用 patch 後，記錄 commit、CMake configure 命令、建置命令與同一模型／prompt 的 benchmark，避免把不同版本或工作負載的結果混在一起。
