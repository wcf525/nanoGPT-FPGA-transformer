// tb_gelu.cpp — 与 gelu_fixed.cpp 配套
// 验收 M2.4：与 PyTorch F.gelu 误差 ≤ 0.5%
#include <iostream>
#include <cmath>
#include <cstring>
#include <ap_int.h>
#include "weight.h"

#define TOTAL_ELEMENTS (256 * 384)

extern "C" {
void gelu_fpga(
    const ap_int<8> in[TOTAL_ELEMENTS],
    ap_int<8> out[TOTAL_ELEMENTS]
);
}

static float gelu_ref(float x) {
    const float c = 0.7978845608f;
    return 0.5f * x * (1.0f + tanhf(c * (x + 0.044715f * x * x * x)));
}

int main() {
    std::cout << "[INFO] 初始化输入（循环覆盖全部 int8 值）..." << std::endl;

    static ap_int<8> in_data[TOTAL_ELEMENTS];
    static ap_int<8> out_hw[TOTAL_ELEMENTS];

    for (int i = 0; i < TOTAL_ELEMENTS; i++)
        in_data[i] = (ap_int<8>)((i % 256) - 128);

    memset(out_hw, 0, sizeof(out_hw));

    std::cout << "[INFO] 启动硬件内核..." << std::endl;
    gelu_fpga(in_data, out_hw);
    std::cout << "[INFO] 内核完成。" << std::endl;

    // ── 误差统计（对比 PyTorch gelu_ref）─────────────────────
    float max_abs_err = 0.f;
    float max_rel_err = 0.f;
    float sum_rel_err = 0.f;
    int   over_thresh = 0;

    std::cout << "-- 关键点验证 (input / HW / SW_float):" << std::endl;
    int check_pts[] = {-128, -4, -2, -1, 0, 1, 2, 4, 127};
    for (int x : check_pts) {
        int   idx    = x + 128;
        float sw_f   = gelu_ref((float)x);
        float hw_val = out_hw[idx % TOTAL_ELEMENTS].to_int();
        std::cout << "  gelu(" << x << ") SW=" << sw_f
                  << " HW=" << hw_val << std::endl;
    }

    for (int i = 0; i < TOTAL_ELEMENTS; i++) {
        float x      = in_data[i].to_int();
        float sw_f   = gelu_ref(x);
        float hw_val = out_hw[i].to_int();

        float abs_err = fabsf(hw_val - sw_f);
        float denom   = fabsf(sw_f) + 1.0f;
        float rel_err = abs_err / denom;

        if (abs_err > max_abs_err) max_abs_err = abs_err;
        if (rel_err > max_rel_err) max_rel_err = rel_err;
        sum_rel_err += rel_err;
        if (rel_err > 0.005f) over_thresh++;
    }
    float avg_rel_err = sum_rel_err / TOTAL_ELEMENTS;

    std::cout << "----------------------------------------" << std::endl;
    std::cout << "最大绝对误差: " << max_abs_err << std::endl;
    std::cout << "最大相对误差: " << max_rel_err * 100.f << "%" << std::endl;
    std::cout << "平均相对误差: " << avg_rel_err * 100.f << "%" << std::endl;
    std::cout << "超过 0.5% 的点数: " << over_thresh << " / " << TOTAL_ELEMENTS << std::endl;
    std::cout << "----------------------------------------" << std::endl;

    if (avg_rel_err <= 0.005f) {
        std::cout << ">> M2.4 SUCCESS: 平均相对误差 ≤ 0.5%，达标！" << std::endl;
        return 0;
    } else {
        std::cout << ">> FAIL: 平均误差=" << avg_rel_err * 100.f << "%" << std::endl;
        return 1;
    }
}