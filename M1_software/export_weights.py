"""
export_weights.py — 放在 nanoGPT 根目录运行：python export_weights.py
功能：
  1. 加载训练好的 ckpt.pt（自动 CPU/GPU）
  2. 测量 FP32 困惑度
  3. 对 6 个权重做 per-channel INT8 量化
  4. 测量 INT8 困惑度，验证回退 ≤ 10%
  5. 导出标准 weight.h（6 权重数组 + 6 scale 数组）
"""

import torch, numpy as np, os, math, copy

# ── 1. 加载模型 ──────────────────────────────────────────────────
out_dir = 'out-shakespeare-char'
device  = 'cuda' if torch.cuda.is_available() else 'cpu'

ckpt    = torch.load(os.path.join(out_dir, 'ckpt.pt'), map_location=device)
from model import GPTConfig, GPT
model   = GPT(GPTConfig(**ckpt['model_args']))
model.load_state_dict(ckpt['model'])
model.eval().to(device)
print(f"[OK] 模型加载完成，参数量: {sum(p.numel() for p in model.parameters())/1e6:.2f}M")

# ── 2. 困惑度计算 ────────────────────────────────────────────────
val_data   = np.memmap(os.path.join('data/shakespeare_char', 'val.bin'),
                       dtype=np.uint16, mode='r')
block_size = ckpt['model_args'].get('block_size', 256)
val_tensor = torch.from_numpy(val_data[:block_size].astype(np.int64)).unsqueeze(0).to(device)

def calc_ppl(mdl):
    with torch.no_grad():
        _, loss = mdl(val_tensor[:, :-1], val_tensor[:, 1:])
    return math.exp(loss.item())

ppl_fp32 = calc_ppl(model)
print(f"[OK] FP32 困惑度: {ppl_fp32:.4f}")

# ── 3. 提取 6 个权重 ─────────────────────────────────────────────
block = model.transformer.h[0]
weight_dict = {
    'attn_c_attn': block.attn.c_attn.weight.detach().cpu().float(),
    'attn_c_proj': block.attn.c_proj.weight.detach().cpu().float(),
    'mlp_c_fc':    block.mlp.c_fc.weight.detach().cpu().float(),
    'mlp_c_proj':  block.mlp.c_proj.weight.detach().cpu().float(),
    'ln_1_weight': block.ln_1.weight.detach().cpu().float(),
}
# ln_1.bias 只在 bias=True 时存在，bias=False 的模型跳过
if block.ln_1.bias is not None:
    weight_dict['ln_1_bias'] = block.ln_1.bias.detach().cpu().float()
else:
    print("  [INFO] 模型 bias=False，ln_1_bias 跳过")

# ── 4. Per-channel INT8 量化 ──────────────────────────────────────
def quantize(w):
    if w.ndim == 2:
        max_abs = w.abs().max(dim=1, keepdim=True).values.clamp(min=1e-8)
        scale   = (max_abs / 127.0).squeeze(1).numpy().astype(np.float32)
        w_int8  = (w / max_abs * 127).round().clamp(-128, 127).to(torch.int8).numpy()
    else:
        max_abs = w.abs().max().clamp(min=1e-8)
        scale   = np.array([max_abs.item() / 127.0], dtype=np.float32)
        w_int8  = (w / max_abs * 127).round().clamp(-128, 127).to(torch.int8).numpy()
    return w_int8, scale

quant = {}
for name, w in weight_dict.items():
    quant[name] = quantize(w)
    print(f"  量化 {name}: shape={quant[name][0].shape}, scale_len={len(quant[name][1])}")

# ── 5. 验证 INT8 困惑度 ───────────────────────────────────────────
def dequant(w_int8, scale):
    w = torch.from_numpy(w_int8).float()
    return w * torch.from_numpy(scale).unsqueeze(1) if w.ndim == 2 else w * scale[0]

model_q = copy.deepcopy(model)
b = model_q.transformer.h[0]
b.attn.c_attn.weight.data = dequant(*quant['attn_c_attn'])
b.attn.c_proj.weight.data = dequant(*quant['attn_c_proj'])
b.mlp.c_fc.weight.data    = dequant(*quant['mlp_c_fc'])
b.mlp.c_proj.weight.data  = dequant(*quant['mlp_c_proj'])
b.ln_1.weight.data        = dequant(*quant['ln_1_weight'])
if 'ln_1_bias' in quant:
    b.ln_1.bias.data      = dequant(*quant['ln_1_bias'])

ppl_int8    = calc_ppl(model_q)
degradation = (ppl_int8 - ppl_fp32) / ppl_fp32 * 100
print(f"[OK] INT8 困惑度: {ppl_int8:.4f}")
print(f"[{'OK' if degradation <= 10 else 'FAIL'}] 困惑度回退: {degradation:.2f}% "
      f"({'≤10% 达标' if degradation <= 10 else '>10% 未达标'})")

# ── 6. 导出 weight.h ──────────────────────────────────────────────
def to_c_array(name, arr, dtype='int8'):
    flat = arr.flatten()
    if dtype == 'int8':
        c_type = 'const int8_t'
        items  = [f'{int(v):4d}' for v in flat]
    else:
        c_type = 'const float'
        items  = [f'{float(v):.8e}f' for v in flat]
    rows = ['  ' + ', '.join(items[i:i+12]) for i in range(0, len(items), 12)]
    body = ',\n'.join(rows)
    return f'{c_type} {name}[{len(flat)}] = {{\n{body}\n}};'

layer_map = [
    ('attn_c_attn', 'transformer_h_0_attn_c_attn_weight', 'transformer_h_0_attn_c_attn_scale'),
    ('attn_c_proj', 'transformer_h_0_attn_c_proj_weight', 'transformer_h_0_attn_c_proj_scale'),
    ('mlp_c_fc',    'transformer_h_0_mlp_c_fc_weight',    'transformer_h_0_mlp_c_fc_scale'),
    ('mlp_c_proj',  'transformer_h_0_mlp_c_proj_weight',  'transformer_h_0_mlp_c_proj_scale'),
    ('ln_1_weight', 'transformer_h_0_ln_1_weight',        'transformer_h_0_ln_1_weight_scale'),
]
# bias=True 时才导出 ln_1_bias
if 'ln_1_bias' in quant:
    layer_map.append(('ln_1_bias', 'transformer_h_0_ln_1_bias', 'transformer_h_0_ln_1_bias_scale'))

lines = [
    '// weight.h — 由 export_weights.py 从真实训练权重导出',
    f'// FP32 困惑度: {ppl_fp32:.4f}  INT8 困惑度: {ppl_int8:.4f}  回退: {degradation:.2f}%',
    '#pragma once',
    '#include <stdint.h>',
    '',
]
for key, w_name, s_name in layer_map:
    w_int8, scale = quant[key]
    lines += [f'// ── {key} ──', to_c_array(w_name, w_int8, 'int8'), '',
              to_c_array(s_name, scale, 'float'), '']

with open('weight.h', 'w') as f:
    f.write('\n'.join(lines))

print(f"\n[OK] weight.h 已导出到 {os.path.abspath('weight.h')}")
print(f"     共 {len(layer_map)} 个权重数组 + {len(layer_map)} 个 scale 数组")
print("完成！截图困惑度数据用于 M1.3 验收。")
