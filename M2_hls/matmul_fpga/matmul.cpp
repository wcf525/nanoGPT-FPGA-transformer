// =============================================================
// matmul_fpga_v4.cpp — 三重分块流式版本
// 彻底解决 xsim OOM 问题
//
// 核心改变：K 维度也做分块 TILE_K=8
//   片上缓存从 [TILE][K=384] 缩小到 [TILE_M][TILE_K] + [TILE_N][TILE_K]
//   = 4×8 + 4×8 = 64 个 ap_int<8> = 极小，xsim 轻松处理
//
// 分块尺寸选择（针对 xc7z020）：
//   TILE_M = 4，TILE_N = 4，TILE_K = 8
//   DSP 用量：TILE_M × TILE_N = 16 个并行 MAC
//   local_C [TILE_M][TILE_N] = 16 个 ap_int<32> 寄存器
// =============================================================

#include <ap_int.h>

#define M       256
#define K       256
#define N       384
#define TILE_M    4
#define TILE_N    4
#define TILE_K    8   // K 维度分块，片上只保留 TILE_K 列

extern "C" {
void matmul_fpga(
    const ap_int<8>  A[M * K],
    const ap_int<8>  B[K * N],
    ap_int<32>       C[M * N]
) {
    #pragma HLS INTERFACE m_axi port=A offset=slave bundle=gmem0 depth=98304  \
        max_read_burst_length=16 num_read_outstanding=4
    #pragma HLS INTERFACE m_axi port=B offset=slave bundle=gmem1 depth=147456 \
        max_read_burst_length=16 num_read_outstanding=4
    #pragma HLS INTERFACE m_axi port=C offset=slave bundle=gmem2 depth=98304  \
        max_write_burst_length=16 num_write_outstanding=4
    #pragma HLS INTERFACE s_axilite port=return bundle=control

    // ----------------------------------------------------------
    // 片上缓存：全部完全展开为寄存器（尺寸极小，安全）
    //   local_A [TILE_M][TILE_K] = 4×8  = 32 个 8bit 寄存器
    //   local_B [TILE_N][TILE_K] = 4×8  = 32 个 8bit 寄存器（转置存储）
    //   local_C [TILE_M][TILE_N] = 4×4  = 16 个 32bit 寄存器
    // ----------------------------------------------------------
    ap_int<8>  local_A[TILE_M][TILE_K];
    ap_int<8>  local_B[TILE_N][TILE_K];
    ap_int<32> local_C[TILE_M][TILE_N];

    #pragma HLS ARRAY_PARTITION variable=local_A complete dim=0
    #pragma HLS ARRAY_PARTITION variable=local_B complete dim=0
    #pragma HLS ARRAY_PARTITION variable=local_C complete dim=0

    // ----------------------------------------------------------
    // 三重分块主循环
    // ----------------------------------------------------------
    row_tile: for (int r = 0; r < M; r += TILE_M) {
        col_tile: for (int c = 0; c < N; c += TILE_N) {

            // 初始化累加器
            init_C: for (int i = 0; i < TILE_M; i++) {
                #pragma HLS UNROLL
                for (int j = 0; j < TILE_N; j++) {
                    #pragma HLS UNROLL
                    local_C[i][j] = 0;
                }
            }

            // K 维度分块：每次处理 TILE_K 列
            k_tile: for (int kt = 0; kt < K; kt += TILE_K) {

                // ---- 读 A tile: A[r..r+TILE_M, kt..kt+TILE_K] ----
                // 每行读 TILE_K=8 个连续元素 → burst=8
                read_A: for (int i = 0; i < TILE_M; i++) {
                    #pragma HLS LOOP_FLATTEN off
                    for (int kk = 0; kk < TILE_K; kk++) {
                        #pragma HLS PIPELINE II=1
                        local_A[i][kk] = A[(r + i) * K + (kt + kk)];
                    }
                }

                // ---- 读 B tile（转置存入）: B[kt..kt+TILE_K, c..c+TILE_N] ----
                // 外层 kk 跑 TILE_K，内层 j 跑 TILE_N=4 → burst=4
                read_B: for (int kk = 0; kk < TILE_K; kk++) {
                    #pragma HLS LOOP_FLATTEN off
                    for (int j = 0; j < TILE_N; j++) {
                        #pragma HLS PIPELINE II=1
                        // 转置存储：local_B[j][kk] = B(kt+kk, c+j)
                        local_B[j][kk] = B[(kt + kk) * N + (c + j)];
                    }
                }

                // ---- MAC：pipeline kk，unroll i/j ----
                // 每周期 TILE_M × TILE_N = 16 路并行 MAC
                matmul_kk: for (int kk = 0; kk < TILE_K; kk++) {
                    #pragma HLS PIPELINE II=1
                    mac_i: for (int i = 0; i < TILE_M; i++) {
                        #pragma HLS UNROLL
                        ap_int<8> a_val = local_A[i][kk];
                        mac_j: for (int j = 0; j < TILE_N; j++) {
                            #pragma HLS UNROLL
                            local_C[i][j] += a_val * local_B[j][kk];
                        }
                    }
                }
            } // k_tile

            // ---- 写回 C tile ----
            write_C: for (int i = 0; i < TILE_M; i++) {
                #pragma HLS LOOP_FLATTEN off
                for (int j = 0; j < TILE_N; j++) {
                    #pragma HLS PIPELINE II=1
                    C[(r + i) * N + (c + j)] = local_C[i][j];
                }
            }
        }
    }
}
} // extern "C"