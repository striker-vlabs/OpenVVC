#include <string.h>
#include "ovdpb.h"
#include "dec_structures.h"
#include "ovdefs.h"
#include "ovutils.h"
#include "ctudec.h"
#include "rcn_structures.h"

#define MAX_PB_SIZE 128
#define BIT_DEPTH 10

#define EPEL_EXTRA_BEFORE 1
#define EPEL_EXTRA_AFTER  2
#define EPEL_EXTRA EPEL_EXTRA_BEFORE + EPEL_EXTRA_AFTER

#define QPEL_EXTRA_BEFORE 3
#define QPEL_EXTRA_AFTER  4
#define QPEL_EXTRA QPEL_EXTRA_BEFORE + QPEL_EXTRA_AFTER

#define POS_IN_CTB(x,y) ((x) + (y) * RCN_CTB_STRIDE)
#define POS_IN_CTB_C(x,y) ((x) + (y) * VVC_CTB_STRIDE_CHROMA)

struct RefBuffY{
    const uint16_t *y;
    uint16_t stride;
};

struct RefBuffC{
    const uint16_t *cb;
    const uint16_t *cr;
    uint16_t stride;
};

static OVMV
clip_mv(uint8_t log2_min_cb_s, int pos_x, int pos_y,
        int pic_w, int pic_h, int cu_w, int cu_h, OVMV mv){
    int x_max  = (pic_w + 2 - pos_x ) << 4;
    int y_max  = (pic_h + 2 - pos_y ) << 4;
    int x_min  = (-cu_w - 3 - pos_x) << 4;
    int y_min  = (-cu_w - 3 - pos_y) << 4;
    mv.x = ov_clip(mv.x, x_min, x_max);
    mv.y = ov_clip(mv.y, y_min, y_max);
    return mv;
}

static void
emulate_block_border(uint16_t *buf, const uint16_t *src,
                    ptrdiff_t buf_linesize,
                    ptrdiff_t src_linesize,
                    int block_w, int block_h,
                    int src_x, int src_y, int w, int h)
{
    int x, y;
    int start_y, start_x, end_y, end_x;

    if (!w || !h)
        return;

    if (src_y >= h) {
        src -= src_y * src_linesize;
        src += (h - 1) * src_linesize;
        src_y = h - 1;
    } else if (src_y <= -block_h) {
        src -= src_y * src_linesize;
        src += (1 - block_h) * src_linesize;
        src_y = 1 - block_h;
    }
    if (src_x >= w) {
        src  += (w - 1 - src_x);
        src_x = w - 1;
    } else if (src_x <= -block_w) {
        src  += (1 - block_w - src_x);
        src_x = 1 - block_w;
    }

    start_y = OVMAX(0, -src_y);
    start_x = OVMAX(0, -src_x);
    end_y = OVMIN(block_h, h-src_y);
    end_x = OVMIN(block_w, w-src_x);

    w    = end_x - start_x;
    src += start_y * src_linesize + start_x;
    buf += start_x;

    // top
    for (y = 0; y < start_y; y++) {
        memcpy(buf, src, w * sizeof(uint16_t));
        buf += buf_linesize;
    }

    // copy existing part
    for (; y < end_y; y++) {
        memcpy(buf, src, w * sizeof(uint16_t));
        src += src_linesize;
        buf += buf_linesize;
    }

    // bottom
    src -= src_linesize;
    for (; y < block_h; y++) {
        memcpy(buf, src, w * sizeof(uint16_t));
        buf += buf_linesize;
    }

    buf -= block_h * buf_linesize + start_x;
    while (block_h--) {
        uint16_t *bufp = (uint16_t *) buf;

        // left
        for(x = 0; x < start_x; x++) {
            bufp[x] = bufp[start_x];
        }

        // right
        for (x = end_x; x < block_w; x++) {
            bufp[x] = bufp[end_x - 1];
        }
        buf += buf_linesize;
    }
}


static uint8_t
test_for_edge_emulation_c(int ref_pos_x, int ref_pos_y, int pic_w, int pic_h,
                          int pu_w, int pu_h)
{
    uint8_t emulate_edge = 0;
    emulate_edge =       ref_pos_x - EPEL_EXTRA_BEFORE < 0;
    emulate_edge |= 2 * (ref_pos_y - EPEL_EXTRA_BEFORE < 0);
    emulate_edge |= 4 * (ref_pos_x >= pic_w);
    emulate_edge |= 8 * (ref_pos_y >= pic_h);
    emulate_edge |= 4 * ((ref_pos_x + pu_w + EPEL_EXTRA_AFTER) >= pic_w);
    emulate_edge |= 8 * ((ref_pos_y + pu_h + EPEL_EXTRA_AFTER) >= pic_h);
    return emulate_edge;
}

static struct RefBuffC
derive_ref_buf_c(const OVPicture *const ref_pic, OVMV mv, int pos_x, int pos_y,
                 uint16_t *edge_buff0, uint16_t *edge_buff1,
                 int log2_pu_w, int log2_pu_h, int log2_ctu_s)
{
    struct RefBuffC ref_buff;
    const uint16_t *const ref_cb  = (uint16_t *) ref_pic->frame->data[1];
    const uint16_t *const ref_cr  = (uint16_t *) ref_pic->frame->data[2];

    int src_stride = ref_pic->frame->linesize[1] >> 1;

    /*FIXME check buff side derivation */
    int ref_pos_x = pos_x + (mv.x >> 5);
    int ref_pos_y = pos_y + (mv.y >> 5);

    const int pu_w = (1 << log2_pu_w) >> 1;
    const int pu_h = (1 << log2_pu_h) >> 1;

    const int pic_w = ref_pic->frame->width[0]  >> 1;
    const int pic_h = ref_pic->frame->height[0] >> 1;

    uint8_t emulate_edge = test_for_edge_emulation_c(ref_pos_x, ref_pos_y, pic_w, pic_h,
                                                     pu_w, pu_h);;

    if (emulate_edge){
        const uint16_t *src_cb  = &ref_cb[ref_pos_x + ref_pos_y * src_stride];
        const uint16_t *src_cr  = &ref_cr[ref_pos_x + ref_pos_y * src_stride];
        int src_off  = EPEL_EXTRA_BEFORE * (src_stride) + (EPEL_EXTRA_BEFORE);
        int buff_off = EPEL_EXTRA_BEFORE * (RCN_CTB_STRIDE) + (EPEL_EXTRA_BEFORE);
        int cpy_w = pu_w + EPEL_EXTRA;
        int cpy_h = pu_h + EPEL_EXTRA;
        /*FIXME clip to frame?*/
        int start_pos_x = ref_pos_x - EPEL_EXTRA_BEFORE;
        int start_pos_y = ref_pos_y - EPEL_EXTRA_BEFORE;
        emulate_block_border(edge_buff0, (src_cb - src_off),
                            RCN_CTB_STRIDE, src_stride,
                            cpy_w, cpy_h, start_pos_x, start_pos_y,
                            pic_w, pic_h);
        emulate_block_border(edge_buff1, (src_cr - src_off),
                            RCN_CTB_STRIDE, src_stride,
                            cpy_w, cpy_h, start_pos_x, start_pos_y,
                            pic_w, pic_h);
        ref_buff.cb = edge_buff0 + buff_off;
        ref_buff.cr = edge_buff1 + buff_off;
        ref_buff.stride = RCN_CTB_STRIDE;
    } else {
        ref_buff.cb = &ref_cb[ref_pos_x + ref_pos_y * src_stride];
        ref_buff.cr = &ref_cr[ref_pos_x + ref_pos_y * src_stride];
        ref_buff.stride = src_stride;
    }
    return ref_buff;
}

static uint8_t
test_for_edge_emulation(int ref_pos_x, int ref_pos_y, int pic_w, int pic_h,
                        int pu_w, int pu_h)
{
    /* FIXME thi could be simplified */
    uint8_t emulate_edge = 0;
    emulate_edge =       ref_pos_x - QPEL_EXTRA_BEFORE < 0;
    emulate_edge |= 2 * (ref_pos_y - QPEL_EXTRA_BEFORE < 0);
    emulate_edge |= 4 * (ref_pos_x >= pic_w);
    emulate_edge |= 8 * (ref_pos_y >= pic_h);
    emulate_edge |= 4 * ((ref_pos_x + pu_w + QPEL_EXTRA_AFTER) >= pic_w);
    emulate_edge |= 8 * ((ref_pos_y + pu_h + QPEL_EXTRA_AFTER) >= pic_h);
    return emulate_edge;
}

static struct RefBuffY
derive_ref_buf_y(const OVPicture *const ref_pic, OVMV mv, int pos_x, int pos_y,
                uint16_t *edge_buff, int log2_pu_w, int log2_pu_h, int log2_ctu_s)
{
    struct RefBuffY ref_buff;
    const uint16_t *const ref_y  = (uint16_t *) ref_pic->frame->data[0];

    int src_stride = ref_pic->frame->linesize[0] >> 1;

    int ref_pos_x = pos_x + (mv.x >> 4);
    int ref_pos_y = pos_y + (mv.y >> 4);

    const int pu_w = 1 << log2_pu_w;
    const int pu_h = 1 << log2_pu_h;

    const int pic_w = ref_pic->frame->width;
    const int pic_h = ref_pic->frame->height;

    uint8_t emulate_edge = test_for_edge_emulation(ref_pos_x, ref_pos_y, pic_w, pic_h,
                                                   pu_w, pu_h);;

    /* FIXME Frame thread synchronization here to ensure data is available
     */

    if (emulate_edge){
        const uint16_t *src_y  = &ref_y[ref_pos_x + ref_pos_y * src_stride];
        int src_off  = QPEL_EXTRA_BEFORE * (src_stride) + (QPEL_EXTRA_BEFORE);
        int buff_off = QPEL_EXTRA_BEFORE * (RCN_CTB_STRIDE) + (QPEL_EXTRA_BEFORE);
        int cpy_w = pu_w + QPEL_EXTRA;
        int cpy_h = pu_h + QPEL_EXTRA;

        /*FIXME clip to frame?*/

        int start_pos_x = ref_pos_x - QPEL_EXTRA_BEFORE;
        int start_pos_y = ref_pos_y - QPEL_EXTRA_BEFORE;

        emulate_block_border(edge_buff, (src_y - src_off),
                             RCN_CTB_STRIDE, src_stride,
                             cpy_w, cpy_h, start_pos_x, start_pos_y,
                             pic_w, pic_h);
        ref_buff.y  = edge_buff + buff_off;
        ref_buff.stride = RCN_CTB_STRIDE;

    } else {

        ref_buff.y  = &ref_y[ref_pos_x + ref_pos_y * src_stride];
        ref_buff.stride = src_stride;
    }
    return ref_buff;
}


void
vvc_motion_compensation(OVCTUDec *const ctudec,
                        int x0, int y0, int log2_pu_w, int log2_pu_h,
                        OVMV mv, uint8_t type )
{
     struct OVRCNCtx    *const rcn_ctx   = &ctudec->rcn_ctx;
     struct InterDRVCtx *const inter_ctx = &ctudec->drv_ctx.inter_ctx;

     struct MCFunctions *mc_l = &rcn_ctx->rcn_funcs.mc_l;
     struct MCFunctions *mc_c = &rcn_ctx->rcn_funcs.mc_c;

     struct OVBuffInfo dst = rcn_ctx->ctu_buff;

    uint8_t ref_idx_0 = 0;
    uint8_t ref_idx_1 = 0;

    OVPicture *ref0 = inter_ctx->rpl0[ref_idx_0];
    OVPicture *ref1 = inter_ctx->rpl1[ref_idx_1];

     dst.y += x0 + y0 * dst.stride;
     dst.cb += (x0 >> 1) + (y0 >> 1) * dst.stride_c;
     dst.cr += (x0 >> 1) + (y0 >> 1) * dst.stride_c;

     uint8_t emulate_edge = 0;

     uint16_t ref_data [RCN_CTB_SIZE];

     const OVFrame *const frame0 =  type ? ref1->frame : ref0->frame;

     const uint16_t *const ref0_y  = (uint16_t *) frame0->data[0];
     const uint16_t *const ref0_cb = (uint16_t *) frame0->data[1];
     const uint16_t *const ref0_cr = (uint16_t *) frame0->data[2];

     int src_stride   = frame0->linesize[0] >> 1;
     int src_stride_c = frame0->linesize[1] >> 1;

     uint8_t log2_ctb_s = ctudec->part_ctx->log2_ctu_s;
     uint8_t log2_min_cb_s = ctudec->part_ctx->log2_min_cb_s;
     int pos_x = (ctudec->ctb_x << log2_ctb_s) + x0;
     int pos_y = (ctudec->ctb_y << log2_ctb_s) + y0;

     const int pu_w = 1 << log2_pu_w;
     const int pu_h = 1 << log2_pu_h;

     const int pic_w = frame0->width[0];
     const int pic_h = frame0->height[0];

     mv = clip_mv(log2_min_cb_s, pos_x, pos_y, pic_w,
                  pic_h, pu_w, pu_h, mv);

     int ref_x = pos_x + (mv.x >> 4);
     int ref_y = pos_y + (mv.y >> 4);

     int off_left = 0;
     int off_above = 0;
     #if 0
     int cut_right = 0;
     int cut_bottom = 0;
     #endif

     #if 0
     int cp_w = pu_w + QPEL_EXTRA;
     int cp_h = pu_h + QPEL_EXTRA;
     #endif
     int prec_x = (mv.x) & 0xF;
     int prec_y = (mv.y) & 0xF;
     int prec_x_c = (mv.x) & 0x1F;
     int prec_y_c = (mv.y) & 0x1F;

     int prec_mc_type = ((prec_x)>0) + (((prec_y)>0) << 1);
     int prec_c_mc_type = (prec_x_c>0) + ((prec_y_c>0) << 1);

     if (ref_x - QPEL_EXTRA_BEFORE < 0){
        emulate_edge = 1;
     }

     if (ref_y - QPEL_EXTRA_BEFORE < 0){
        emulate_edge |= 2;
     }

     if (ref_x>= pic_w){
        emulate_edge |= 4;
     }

     if (ref_y >= pic_h){
        emulate_edge |= 8;
     } 

     if ((ref_x + pu_w + QPEL_EXTRA_AFTER) >= pic_w){
        emulate_edge |= 4;
     }

     if ((ref_y + pu_h + QPEL_EXTRA_AFTER) >= pic_h){
         emulate_edge |= 8;
     }

     const uint16_t *src_y  = &ref0_y [ ref_x       + ref_y        * src_stride];
     const uint16_t *src_cb = &ref0_cb[(ref_x >> 1) + (ref_y >> 1) * src_stride_c];
     const uint16_t *src_cr = &ref0_cr[(ref_x >> 1) + (ref_y >> 1) * src_stride_c];

    /* FIXME
     * Thread synchronization to ensure data is available before usage
     */

     if (emulate_edge){
         int src_off  = QPEL_EXTRA_BEFORE * (src_stride) + (QPEL_EXTRA_BEFORE);
         int buff_off = QPEL_EXTRA_BEFORE * (RCN_CTB_STRIDE) + (QPEL_EXTRA_BEFORE);
         emulate_block_border(ref_data, (src_y - src_off),
                              RCN_CTB_STRIDE, src_stride,
                              pu_w + QPEL_EXTRA, pu_h + QPEL_EXTRA,
                              ref_x - QPEL_EXTRA_BEFORE, ref_y - QPEL_EXTRA_BEFORE,
                              pic_w, pic_h);
         src_y = ref_data + buff_off;
         src_stride = RCN_CTB_STRIDE;
     }

     #if 0
     call(vvc_mc_uni,
          (QPEL,prec_mc_type,log2_pu_w),
          (_dst_y, RCN_CTB_STRIDE, src_y, src_stride, pu_h, prec_x, prec_y, pu_w)
     );
     #else
     mc_l->unidir[prec_mc_type][log2_pu_w](dst.y, RCN_CTB_STRIDE,
                                           src_y, src_stride, pu_h,
                                           prec_x, prec_y, pu_w);
     #endif

     emulate_edge = 0;
     if ((ref_x >> 1) - EPEL_EXTRA_BEFORE < 0){
        emulate_edge = 1;
     }

     if ((ref_y >> 1) - EPEL_EXTRA_BEFORE < 0){
        off_above = OVMIN(pu_h, -ref_y);
        emulate_edge |= 2;
     }

     if (((ref_x >> 1) + (pu_w >> 1) + EPEL_EXTRA_AFTER) >= (pic_w >> 1)){
        emulate_edge |= 4;
     }

     if (((ref_y >> 1) + (pu_h >> 1) + EPEL_EXTRA_AFTER) >= (pic_h >> 1)){
         emulate_edge |= 8;
     }

     if (emulate_edge){
         int src_off  = EPEL_EXTRA_BEFORE * (src_stride_c) + (EPEL_EXTRA_BEFORE);
         int buff_off = EPEL_EXTRA_BEFORE * (RCN_CTB_STRIDE) + (EPEL_EXTRA_BEFORE);
         emulate_block_border(ref_data, (src_cb - src_off),
                 RCN_CTB_STRIDE, src_stride_c,
                 (pu_w >> 1)  + EPEL_EXTRA, (pu_h >> 1) + EPEL_EXTRA,
                 (pos_x >> 1) + (mv.x >> 5) - EPEL_EXTRA_BEFORE, (pos_y >> 1) + (mv.y >> 5) - EPEL_EXTRA_BEFORE,
                 (pic_w >> 1), (pic_h >> 1));
         src_cb = ref_data + buff_off;
         src_stride_c = RCN_CTB_STRIDE;
     }


     #if 0
     call(vvc_mc_uni,
          (EPEL,prec_c_mc_type,log2_pu_w-1),
          (_dst_cb, RCN_CTB_STRIDE, src_cb, src_stride_c, pu_h >> 1, prec_x_c, prec_y_c, pu_w >> 1)
     );
     #else
     mc_c->unidir[prec_c_mc_type][log2_pu_w - 1](dst.cb, RCN_CTB_STRIDE,
                                                 src_cb, src_stride_c,
                                                 pu_h >> 1, prec_x_c, prec_y_c, pu_w >> 1);
     #endif

     if (emulate_edge){
         int src_off  = EPEL_EXTRA_BEFORE * (frame0->linesize[1] >> 1) + (EPEL_EXTRA_BEFORE);
         int buff_off = EPEL_EXTRA_BEFORE * (RCN_CTB_STRIDE) + (EPEL_EXTRA_BEFORE);
         emulate_block_border(ref_data, (src_cr - src_off),
                 RCN_CTB_STRIDE, frame0->linesize[1] >> 1,
                 (pu_w >> 1) + EPEL_EXTRA, (pu_h >> 1) + EPEL_EXTRA,
                 (pos_x >> 1) + (mv.x >> 5) - EPEL_EXTRA_BEFORE, (pos_y >> 1) + (mv.y >> 5) - EPEL_EXTRA_BEFORE,
                 (pic_w >> 1), (pic_h >> 1));
         src_cr = ref_data + buff_off;
         src_stride_c = RCN_CTB_STRIDE;
     }

     #if 0
     call(vvc_mc_uni,
             (EPEL,prec_c_mc_type,log2_pu_w-1),
             (_dst_cr, RCN_CTB_STRIDE, src_cr, src_stride_c, pu_h >> 1, prec_x_c, prec_y_c, pu_w >> 1)
     );
     #else
     mc_c->unidir[prec_c_mc_type][log2_pu_w - 1](dst.cr, RCN_CTB_STRIDE,
                                                 src_cr, src_stride_c,
                                                 pu_h >> 1, prec_x_c, prec_y_c, pu_w >> 1);
     #endif
}

void
rcn_motion_compensation_b(OVCTUDec *const ctudec,
                          uint8_t x0, uint8_t y0, uint8_t log2_pu_w, uint8_t log2_pu_h,
                          OVMV mv0, OVMV mv1)
{
    const struct InterDRVCtx *const inter_ctx = &ctudec->drv_ctx.inter_ctx;
    /* FIXME derive ref_idx */
    uint8_t ref_idx_0 = 0;
    uint8_t ref_idx_1 = 0;

    OVPicture *ref0 = inter_ctx->rpl0[ref_idx_0];
    OVPicture *ref1 = inter_ctx->rpl1[ref_idx_1];

    struct OVBuffInfo dst = ctudec->rcn_ctx.ctu_buff;

    dst.y  += x0 + y0 * dst.stride;
    dst.cb += (x0 >> 1) + (y0 >> 1) * dst.stride_c;
    dst.cr += (x0 >> 1) + (y0 >> 1) * dst.stride_c;
   
    /* TMP buffers for edge emulation
     * FIXME use tmp buffers in local contexts
     */
    uint16_t edge_buff0[RCN_CTB_SIZE];
    uint16_t edge_buff1[RCN_CTB_SIZE];
    uint16_t edge_buff0_1[RCN_CTB_SIZE];
    uint16_t edge_buff1_1[RCN_CTB_SIZE];
    uint16_t ref_data[RCN_CTB_SIZE];

    /*FIXME we suppose here both refs possess the same size*/

    const int log2_ctb_s = ctudec->part_ctx->log2_ctu_s;

    /* FIXME we should not need ctb_x/y
     * it could be retrieved from position in frame buff
     */
    int pos_x = (ctudec->ctb_x << log2_ctb_s) + x0;
    int pos_y = (ctudec->ctb_y << log2_ctb_s) + y0;

    uint8_t log2_min_cb_s = ctudec->part_ctx->log2_min_cb_s;

    mv0 = clip_mv(log2_min_cb_s, pos_x, pos_y, ref0->frame->width,
                  ref0->frame->height, 1 << log2_pu_w, 1 << log2_pu_h, mv0);

    mv1 = clip_mv(log2_min_cb_s, pos_x, pos_y, ref1->frame->width,
                  ref1->frame->height, 1 << log2_pu_w, 1 << log2_pu_h, mv1);


    #if 0
    const struct RefBuffY ref0 = derive_ref_buf_y(ref0, mv0, pos_x, pos_y, edge_buff0,
                                                  log2_pu_w, log2_pu_h, log2_ctb_s);

    const struct RefBuffY ref1 = derive_ref_buf_y(ref1, mv1, pos_x, pos_y, edge_buff1,
                                                  log2_pu_w, log2_pu_h, log2_ctb_s);
    #endif

    const int pu_w = 1 << log2_pu_w;
    const int pu_h = 1 << log2_pu_h;

    uint8_t prec_x0 = (mv0.x) & 0xF;
    uint8_t prec_y0 = (mv0.y) & 0xF;

    uint8_t prec_x1 = (mv1.x) & 0xF;
    uint8_t prec_y1 = (mv1.y) & 0xF;

    uint8_t prec_0_mc_type = (prec_x0 > 0) + ((prec_y0 > 0) << 1);
    uint8_t prec_1_mc_type = (prec_x1 > 0) + ((prec_y1 > 0) << 1);


    #if 0
    call(vvc_mc_bi0,
         (QPEL,prec_0_mc_type,log2_pu_w),
         (ref_data, ref0.ref_y, ref0.stride, pu_h, prec_x0, prec_y0, pu_w)
    );

    call(vvc_mc_bi1,
         (QPEL,prec_1_mc_type,log2_pu_w),
         (dst_y, RCN_CTB_STRIDE, ref1.ref_y, ref1.stride, ref_data, pu_h, prec_x1, prec_y1, pu_w)
    );
    #endif

#if 0
     const struct RefBuffC ref0_c = derive_ref_buf_c(ref0, mv0,
                                                     pos_x >> 1, pos_y >> 1,
                                                     edge_buff0, edge_buff0_1,
                                                     log2_pu_w, log2_pu_h, log2_ctu_s);

     const struct RefBuffC ref1_c = derive_ref_buf_c(ref1, mv1,
                                                     pos_x >> 1, pos_y >> 1,
                                                     edge_buff1, edge_buff1_1,
                                                     log2_pu_w, log2_pu_h, log2_ctu_s);
#endif
    prec_x0 = (mv0.x) & 0x1F;
    prec_y0 = (mv0.y) & 0x1F;
    prec_x1 = (mv1.x) & 0x1F;
    prec_y1 = (mv1.y) & 0x1F;

    prec_0_mc_type = (prec_x0>0) + ((prec_y0>0) << 1);
    prec_1_mc_type = (prec_x1>0) + ((prec_y1>0) << 1);

    uint16_t* ref_data0 = ref_data;
    uint16_t* ref_data1 = ref_data+MAX_PB_SIZE/2;

    #if 0
    call(vvc_mc_bi0,
         (EPEL,prec_0_mc_type,log2_pu_w-1),
         (ref_data0, ref0_c.cb, ref0_c.stride, pu_h >> 1, prec_x0, prec_y0, pu_w >> 1)
    );
    call(vvc_mc_bi0,
         (EPEL,prec_0_mc_type,log2_pu_w-1),
         (ref_data1, ref0_c.cr, ref0_c.stride, pu_h >> 1, prec_x0, prec_y0, pu_w >> 1)
    );

    call(vvc_mc_bi1,
         (EPEL,prec_1_mc_type,log2_pu_w-1),
         (dst_cb, RCN_CTB_STRIDE, ref1_c.cb, ref1_c.stride, ref_data0, pu_h >> 1, prec_x1, prec_y1, pu_w >> 1)
    );
    call(vvc_mc_bi1,
         (EPEL,prec_1_mc_type,log2_pu_w-1),
         (dst_cr, RCN_CTB_STRIDE, ref1_c.cr, ref1_c.stride, ref_data1, pu_h >> 1, prec_x1, prec_y1, pu_w >> 1)
    );
    #endif
}