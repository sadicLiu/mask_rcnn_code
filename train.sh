#!/usr/bin/env bash
CUDA_VISIBLE_DEVICES=1 python tools/train_net.py --config-file "configs/e2e_mask_rcnn_R_50_FPN_1x.yaml"  SOLVER.IMS_PER_BATCH 2 SOLVER.BASE_LR 0.0025 SOLVER.MAX_ITER 720000 SOLVER.STEPS "(480000, 640000)" TEST.IMS_PER_BATCH 1
