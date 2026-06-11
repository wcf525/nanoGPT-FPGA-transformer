// tb_multi_head_attn.cpp
// 验收 M2.2：HW 输出与 SW golden bit-accurate
#include <iostream>
#include <cstring>
#include <cstdlib>
#include <ap_int.h>
#include "weight.h"

#define M      256
#define N_EMBD 384
#define N_HEAD   6
#define D_HEAD  64

extern "C" {
void multi_head_attn(
    const ap_int<8> Q[M * N_EMBD],
    const ap_int<8> K[M * N_EMBD],
    const ap_int<8> V[M * N_EMBD],
    ap_int<8> Out[M * N_EMBD]
);
}

// ── 与内核完全一致的 softmax 近似函数 ──────────────────────────
static ap_int<16> softmax_lut_ref(ap_int<16> x) {
    if (x < -32) return 0;
    if (x <   0) return (x + 32) >> 2;
    if (x <  32) return 8 + ((x) >> 1);
    return 31;
}

int main() {
    std::cout << "[INFO] 初始化 Q/K/V 输入..." << std::endl;

    static ap_int<8> Q_in[M * N_EMBD];
    static ap_int<8> K_in[M * N_EMBD];
    static ap_int<8> V_in[M * N_EMBD];
    static ap_int<8> Out_hw[M * N_EMBD];
    static ap_int<8> Out_sw[M * N_EMBD];

    // 用 weight.h 的真实权重填充 Q/K/V（循环复用）
    const int w_len = 442368; // attn_c_attn_weight 大小
    for (int i = 0; i < M * N_EMBD; i++) {
        Q_in[i] = transformer_h_0_attn_c_attn_weight[i % w_len];
        K_in[i] = transformer_h_0_attn_c_attn_weight[(i + 100) % w_len];
        V_in[i] = transformer_h_0_attn_c_attn_weight[(i + 200) % w_len];
    }
    memset(Out_hw, 0, sizeof(Out_hw));
    memset(Out_sw, 0, sizeof(Out_sw));

    // ── SW golden（与内核逻辑完全对称）────────────────────────
    std::cout << "[INFO] 运行 SW golden..." << std::endl;
    static ap_int<8>  lQ[M][D_HEAD], lK[M][D_HEAD], lV[M][D_HEAD];
    static ap_int<16> score_row[M];

    for (int h = 0; h < N_HEAD; h++) {
        for (int i = 0; i < M; i++)
            for (int j = 0; j < D_HEAD; j++) {
                lQ[i][j] = Q_in[i * N_EMBD + h * D_HEAD + j];
                lK[i][j] = K_in[i * N_EMBD + h * D_HEAD + j];
                lV[i][j] = V_in[i * N_EMBD + h * D_HEAD + j];
            }

        for (int i = 0; i < M; i++) {
            // score
            for (int j = 0; j < M; j++) {
                ap_int<16> acc = 0;
                for (int k = 0; k < D_HEAD; k++)
                    acc += lQ[i][k] * lK[j][k];
                score_row[j] = acc >> 2;
            }
            // softmax
            int sum_exp = 0;
            for (int j = 0; j < M; j++) {
                score_row[j] = softmax_lut_ref(score_row[j]);
                sum_exp += score_row[j].to_int();
            }
            if (sum_exp != 0)
                for (int j = 0; j < M; j++)
                    score_row[j] = (score_row[j] * 4096) / sum_exp;
            // out
            ap_int<32> obuf[D_HEAD] = {};
            for (int k = 0; k < M; k++) {
                ap_int<16> sv = score_row[k];
                for (int j = 0; j < D_HEAD; j++)
                    obuf[j] += sv * lV[k][j];
            }
            for (int j = 0; j < D_HEAD; j++) {
                // 与内核完全一致：用 .range(7,0) 取低8位，模拟位截断
                ap_int<32> shifted = obuf[j] >> 9;
                Out_sw[i * N_EMBD + h * D_HEAD + j] = shifted.range(7, 0);
            }
        }
    }

    // ── 调用硬件内核 ────────────────────────────────────────────
    std::cout << "[INFO] 启动硬件内核..." << std::endl;
    multi_head_attn(Q_in, K_in, V_in, Out_hw);
    std::cout << "[INFO] 内核完成。" << std::endl;

    // ── 比对结果 ────────────────────────────────────────────────
    int err = 0;
    for (int i = 0; i < M * N_EMBD; i++) {
        if (Out_hw[i] != Out_sw[i]) {
            err++;
            if (err <= 5)
                std::cout << "[ERROR] idx=" << i
                          << " HW=" << Out_hw[i]
                          << " SW=" << Out_sw[i] << std::endl;
        }
    }

    std::cout << "----------------------------------------" << std::endl;
    if (err == 0) {
        std::cout << ">> M2.2 SUCCESS: HW 与 SW golden 100% bit-accurate！" << std::endl;
        return 0;
    } else {
        std::cout << ">> FAIL: " << err << " 个不匹配。" << std::endl;
        return 1;
    }
}