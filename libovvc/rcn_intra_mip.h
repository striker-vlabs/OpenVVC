#ifndef RCN_INTRA_MIP_H
#define RCN_INTRA_MIP_H

#define MIP_SHIFT 6
#define MIP_OFFSET (1 << (MIP_SHIFT - 1))

struct MIPCtx{
    const uint8_t *mip_matrix;
};

const struct MIPCtx
derive_mip_ctx(int log2_pb_w, int log2_pb_h, uint8_t mip_mode);

int
up_sample(uint16_t *const dst, const int16_t *const src,
          const uint16_t *ref,
          int log2_upsampled_size_src, int log2_opposite_size,
          int src_step, int src_stride,
          int dst_step, int dst_stride,
          int ref_step, int log2_scale);

#endif // RCN_INTRA_MIP_H