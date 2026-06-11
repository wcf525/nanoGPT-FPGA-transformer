// =============================================================
// tb_matmul_v4.cpp — 与 matmul_fpga_v4 配套的测试台
// TILE_M/TILE_N/TILE_K 仅在内核里定义，tb 不依赖它们
// =============================================================

#include <iostream>
#include <cstring>
#include <ap_int.h>
#include "weight.h"   // 与 tb.cpp 同目录

#define M   256
#define K   256
#define N   384

extern "C" {
void matmul_fpga(
    const ap_int<8>  A[M * K],
    const ap_int<8>  B[K * N],
    ap_int<32>       C[M * N]
);
}

int main() {
    std::cout << "[INFO] 初始化矩阵..." << std::endl;

    static ap_int<8>  A_flat[M * K];
    static ap_int<8>  B_flat[K * N];
    static ap_int<32> C_hw[M * N];
    static ap_int<32> C_sw[M * N];

    for (int i = 0; i < M; i++)
        for (int j = 0; j < K; j++)
            A_flat[i * K + j] = (i + j) % 7 - 3;

    const int weight_total = 384 * 128;
    for (int k = 0; k < K; k++)
        for (int n = 0; n < N; n++) {
            int idx = (k * 128 + n) % weight_total;
            B_flat[k * N + n] = transformer_h_0_attn_c_attn_weight[idx];
        }

    memset(C_hw, 0, sizeof(C_hw));
    memset(C_sw, 0, sizeof(C_sw));

    std::cout << "[INFO] CPU 参考计算..." << std::endl;
    for (int i = 0; i < M; i++)
        for (int j = 0; j < N; j++) {
            ap_int<32> sum = 0;
            for (int k = 0; k < K; k++)
                sum += A_flat[i * K + k] * B_flat[k * N + j];
            C_sw[i * N + j] = sum;
        }

    std::cout << "[INFO] 启动硬件内核..." << std::endl;
    matmul_fpga(A_flat, B_flat, C_hw);
    std::cout << "[INFO] 内核完成。" << std::endl;

    int err = 0;
    for (int i = 0; i < M * N; i++) {
        if (C_hw[i] != C_sw[i]) {
            err++;
            if (err <= 5) {
                std::cout << "[ERROR] idx=" << i
                          << " HW=" << C_hw[i]
                          << " SW=" << C_sw[i] << std::endl;
            }
        }
    }

    std::cout << "-- 采样 C[0][0]=" << C_sw[0]
              << " C[1][1]=" << C_sw[N+1] << std::endl;

    if (err == 0) {
        std::cout << ">> SUCCESS: 100% 匹配！" << std::endl;
        return 0;
    } else {
        std::cout << ">> FAIL: " << err << " 个错误。" << std::endl;
        return 1;
    }
}