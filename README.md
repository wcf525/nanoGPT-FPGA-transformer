基于 FPGA 的端到端 INT8 nanoGPT 硬件加速器系统


本项目是一套基于 Xilinx Zynq-7020 (PYNQ-Z2) 异构计算平台实现的端到端大语言模型（nanoGPT）全量化硬件加速器系统。项目实现了从 PyTorch 模型量化校准、核心大模型算子硬件流水线重构、SoC 系统级总线集成到物理级时序闭合的全栈软硬件协同设计（Codesign）。


🚀 核心技术指标

- 时序与主频：成功实现全局时序收敛（WNS = 4.456 ns），实际物理极限工作频率高达 180.37 MHz，远超大作业 100 MHz 基准线。

- 资源极度压缩：独创 行缓存融合架构（Row-Buffer Fusion），将多头注意力机制的片上存储开销由 N^2 降维至 N，整体 Block RAM 占用率压制在惊人的 13.21%。

- 高吞吐流水线：核心计算矩阵全部通过高层次综合（HLS）编译原语实现完全展开（Unroll），达成 `II = 1` 的极致全流水吞吐性能。

- 比特级对齐：全规模张量下 C-Simulation 错误率为 0，计算精度与 PyTorch Golden 参考模型实现 Bit-accurate 级完全对齐。


📂 规范化项目目录结构


本项目严格遵循工业级 SoC 开发的软硬件解耦与测试分离规范，目录树结构如下：

nanoGPT-FPGA-transformer

├── README.md                    ← 项目说明

├── M1_software

│   ├── export_weights.py        ← 量化导出脚本

│   └── weight.h                 ← 导出的权重文件

├── M2_hls

│   ├── matmul

│   │   ├── matmul.cpp   ← 内核

│   │   └── tb.cpp     ← tb

│   ├── attention

│   │   ├── attention.cpp

│   │   └── tb.cpp

│   ├── layernorm

│   │   ├── layernorm.cpp

│   │   └── tb.cpp

│   └── gelu

│       ├── gelu.cpp

│       └── tb.cpp

├── M2_ip    ← ip核

│   ├── xilinx_com_hls_matmul_fpga_1_0.zip

│   ├── xilinx_com_hls_multi_head_attn_1_0.zip

│   ├── xilinx_com_hls_layernorm_fpga_1_0.zip

│   └── xilinx_com_hls_gelu_fpga_1_0.zip

├── M3_nanogpt_core    ← bit流

│   ├──  nanogpt_core.bit

│   └── nanogpt_core.hwh

└── prompts    ← AI 对话日志

