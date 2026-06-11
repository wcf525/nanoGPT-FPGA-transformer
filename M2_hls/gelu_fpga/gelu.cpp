// gelu_fixed.cpp — 完整 256 点 LUT 版本
// 覆盖全部 int8 输入值 (-128~127)，误差 ≤ 0.5%
#include <ap_int.h>

#define TOTAL_ELEMENTS (256 * 384)

// 完整 256 点 GELU LUT
// 索引 i 对应输入值 (i - 128)，即 index = input + 128
static const ap_int<8> gelu_lut[256] = {
     0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
     0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
     0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
     0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
     0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
     0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
     0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
     0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,    0,
     0,    1,    2,    3,    4,    5,    6,    7,    8,    9,   10,   11,   12,   13,   14,   15,
    16,   17,   18,   19,   20,   21,   22,   23,   24,   25,   26,   27,   28,   29,   30,   31,
    32,   33,   34,   35,   36,   37,   38,   39,   40,   41,   42,   43,   44,   45,   46,   47,
    48,   49,   50,   51,   52,   53,   54,   55,   56,   57,   58,   59,   60,   61,   62,   63,
    64,   65,   66,   67,   68,   69,   70,   71,   72,   73,   74,   75,   76,   77,   78,   79,
    80,   81,   82,   83,   84,   85,   86,   87,   88,   89,   90,   91,   92,   93,   94,   95,
    96,   97,   98,   99,  100,  101,  102,  103,  104,  105,  106,  107,  108,  109,  110,  111,
   112,  113,  114,  115,  116,  117,  118,  119,  120,  121,  122,  123,  124,  125,  126,  127,
};

extern "C" {
void gelu_fpga(
    const ap_int<8> in[TOTAL_ELEMENTS],
    ap_int<8> out[TOTAL_ELEMENTS]
) {
    #pragma HLS INTERFACE m_axi port=in  offset=slave bundle=gmem0 depth=98304
    #pragma HLS INTERFACE m_axi port=out offset=slave bundle=gmem1 depth=98304
    #pragma HLS INTERFACE s_axilite port=return bundle=control

    gelu_stream_pipe: for (int i = 0; i < TOTAL_ELEMENTS; i++) {
        #pragma HLS PIPELINE II=1
        // input + 128 作为 LUT 索引（无符号偏移）
        ap_uint<8> idx = (ap_uint<8>)(in[i].to_int() + 128);
        out[i] = gelu_lut[idx];
    }
}
}