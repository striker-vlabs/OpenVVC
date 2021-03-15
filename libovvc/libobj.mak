LIB_VERSION_HEADER:=ovversion.h

LIB_SRC:=																																 \
					ctudec.c                                                       \
					data_rcn_angular.c                                           	 \
					data_rcn_mip.c                                           			 \
					data_rcn_transform.c                                           \
					data_scan_lut.c                                                \
					dec_init.c                                                     \
					dpb.c                                                          \
					dpb_internal.c                                                 \
       		mempool.c                                                      \
       		mvpool.c                                                       \
					nvcl.c                                                         \
					nvcl_dpb.c                                                     \
					nvcl_nal_ph.c                                                  \
					nvcl_nal_aps.c                                                 \
					nvcl_nal_pps.c                                                 \
					nvcl_nal_sps.c                                                 \
					nvcl_ptl.c                                                     \
					nvcl_rpl.c                                                     \
					ovdec.c                                                        \
					ovutils.c                                                      \
					ovmem.c                                                        \
					overror.c																										   \
					ovdmx.c                                                        \
					ovio.c                                                         \
					ovannexb.c                                                     \
					ovunits.c                                                      \
					ovframe.c                                                      \
					ovthreads.c                                                    \
					drv_lines.c                                                    \
					drv_intra.c                                                    \
					drv_mvp.c                                                      \
					rcn.c																													 \
					rcn_ctu.c                                                      \
					rcn_inter.c                                                    \
					rcn_fill_ref.c                                                 \
					rcn_intra_dc_planar.c                                          \
					rcn_intra_mip.c																								 \
					rcn_transform.c                                                \
					rcn_residuals.c                                                \
					rcn_intra_angular.c                                            \
					rcn_intra_cclm.c                                               \
					rcn_lfnst.c                                                    \
					rcn_mc.c                                                       \
					rcn_df.c                                                       \
					slicedec.c                                                     \
					vcl_alf.c                                                       \
					vcl_sao.c                                                       \
					vcl_sh.c                                                       \
					vcl_coding_unit.c                                              \
					vcl_coding_tree.c                                              \
					vcl_transform_unit.c                                           \
					vcl_residual_coding.c                                          \
					vcl_cabac.c																										 \

LIB_HEADER:=ovversion.h   \
						ovdefs.h       \
						ovunits.h      \
						ovdec.h        \
						ovframe.h