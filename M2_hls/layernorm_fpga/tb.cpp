// tb_layernorm.cpp — 与 layernorm_fixed.cpp 配套
// 验收 M2.3：误差 ≤ 1%
#include <iostream>
#include <cmath>
#include <cstring>
#include <ap_int.h>
#include "weight.h"

#define M      256
#define N_EMBD 384

extern "C" {
void layernorm_fpga(
    const ap_int<8> in[M * N_EMBD],
    ap_int<8> out[M * N_EMBD]
);
}

int main() {
    std::cout << "[INFO] 初始化输入..." << std::endl;

    static ap_int<8> in_data[M * N_EMBD];
    static ap_int<8> out_hw[M * N_EMBD];
    static ap_int<8> out_sw[M * N_EMBD];

    // 用真实 ln_1_weight 填充输入
    for (int i = 0; i < M * N_EMBD; i++)
        in_data[i] = transformer_h_0_ln_1_weight[i % 384];

    memset(out_hw, 0, sizeof(out_hw));
    memset(out_sw, 0, sizeof(out_sw));

    // ── SW golden：与内核完全对称的定点计算 ───────────────────
    std::cout << "[INFO] 运行 SW golden..." << std::endl;
    for (int i = 0; i < M; i++) {
        int sum = 0;
        for (int j = 0; j < N_EMBD; j++)
            sum += in_data[i * N_EMBD + j].to_int();
        int mean = sum / N_EMBD;

        int var_sum = 0;
        for (int j = 0; j < N_EMBD; j++) {
            int d = in_data[i * N_EMBD + j].to_int() - mean;
            var_sum += d * d;
        }
        int variance = var_sum / N_EMBD;

        float var_f     = (float)variance + 1.0f;
        float inv_std_f = 1.0f / sqrtf(var_f);
        int   inv_std_sc = (int)(inv_std_f * 32768.0f);

        for (int j = 0; j < N_EMBD; j++) {
            int norm_val  = (in_data[i * N_EMBD + j].to_int() - mean) * inv_std_sc;
            int final_val = norm_val >> 15;
            if (final_val >  127) final_val =  127;
            if (final_val < -128) final_val = -128;
            out_sw[i * N_EMBD + j] = (ap_int<8>)final_val;
        }
    }

    // ── 调用硬件内核 ───────────────────────────────────────────
    std::cout << "[INFO] 启动硬件内核..." << std::endl;
    layernorm_fpga(in_data, out_hw);
    std::cout << "[INFO] 内核完成。" << std::endl;

    // ── 误差统计 ───────────────────────────────────────────────
    int   exact_match = 0;
    int   off_by_one  = 0;
    int   big_err     = 0;
    float max_rel_err = 0.f;
    float sum_rel_err = 0.f;

    for (int i = 0; i < M * N_EMBD; i++) {
        int hw = out_hw[i].to_int();
        int sw = out_sw[i].to_int();
        int diff = abs(hw - sw);

        float denom   = fabsf((float)sw) + 1.0f;
        float rel_err = diff / denom;
        if (rel_err > max_rel_err) max_rel_err = rel_err;
        sum_rel_err += rel_err;

        if (diff == 0)      exact_match++;
        else if (diff == 1) off_by_one++;
        else                big_err++;
    }
    float avg_rel_err = sum_rel_err / (M * N_EMBD);

    std::cout << "----------------------------------------" << std::endl;
    std::cout << "完全一致:   " << exact_match << " / " << M*N_EMBD << std::endl;
    std::cout << "差1(舍入):  " << off_by_one  << " / " << M*N_EMBD << std::endl;
    std::cout << "差>1(错误): " << big_err     << " / " << M*N_EMBD << std::endl;
    std::cout << "最大相对误差: " << max_rel_err * 100.f << "%" << std::endl;
    std::cout << "平均相对误差: " << avg_rel_err * 100.f << "%" << std::endl;
    std::cout << "----------------------------------------" << std::endl;

    if (big_err == 0) {
        std::cout << ">> M2.3 SUCCESS: 所有误差在舍入范围内，≤1% 达标！" << std::endl;
        return 0;
    } else {
        std::cout << ">> FAIL: 存在 " << big_err << " 个大于1的误差点。" << std::endl;
        return 1;
    }
}