// layernorm_fixed.cpp — 修复精度版本
// 1. epsilon 从 1.0 改为 1（对 int8 输入范围合理）
// 2. inv_std_scaled 精度从 1024 提升到 32768（15位）
// 3. 右移从 10 改为 15
#include <ap_int.h>
#include <hls_math.h>

#define M      256
#define N_EMBD 384

extern "C" {
void layernorm_fpga(
    const ap_int<8> in[M * N_EMBD],
    ap_int<8> out[M * N_EMBD]
) {
    #pragma HLS INTERFACE m_axi port=in  offset=slave bundle=gmem0 depth=98304
    #pragma HLS INTERFACE m_axi port=out offset=slave bundle=gmem1 depth=98304
    #pragma HLS INTERFACE s_axilite port=return bundle=control

    ap_int<8> row_buf[N_EMBD];

    token_row_loop: for (int i = 0; i < M; i++) {

        // ── A: 读入 + 求和 ────────────────────────────────────
        int sum = 0;
        read_and_sum: for (int j = 0; j < N_EMBD; j++) {
            #pragma HLS PIPELINE II=1
            ap_int<8> val = in[i * N_EMBD + j];
            row_buf[j] = val;
            sum += val.to_int();
        }
        int mean = sum / N_EMBD;  // 整数均值

        // ── B: 求方差 ─────────────────────────────────────────
        int var_sum = 0;
        calc_variance: for (int j = 0; j < N_EMBD; j++) {
            #pragma HLS PIPELINE II=1
            int diff = row_buf[j].to_int() - mean;
            var_sum += diff * diff;
        }
        int variance = var_sum / N_EMBD;

        // ── C: 计算 1/sqrt(var + eps) ─────────────────────────
        // epsilon=1：对 int8 数据（方差通常 100~2000）影响极小
        // 精度提升：× 32768（15位）代替 × 1024（10位）
        float var_f      = (float)variance + 1.0f;
        float inv_std_f  = 1.0f / hls::sqrt(var_f);
        int   inv_std_sc = (int)(inv_std_f * 32768.0f);  // 15位精度

        // ── D: 归一化写回 ─────────────────────────────────────
        write_back: for (int j = 0; j < N_EMBD; j++) {
            #pragma HLS PIPELINE II=1
            int norm_val  = (row_buf[j].to_int() - mean) * inv_std_sc;
            int final_val = norm_val >> 15;  // 右移 15 位还原
            // 饱和截断到 int8
            if (final_val >  127) final_val =  127;
            if (final_val < -128) final_val = -128;
            out[i * N_EMBD + j] = (ap_int<8>)final_val;
        }
    }
}
}