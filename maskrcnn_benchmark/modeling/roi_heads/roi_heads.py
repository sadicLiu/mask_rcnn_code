import torch

from .box_head.box_head import build_roi_box_head
from .keypoint_head.keypoint_head import build_roi_keypoint_head
from .mask_head.mask_head import build_roi_mask_head


class CombinedROIHeads(torch.nn.ModuleDict):
    """
    Combines a set of individual heads (for box prediction or masks) into a single
    head.
    """

    def __init__(self, cfg, heads):
        super(CombinedROIHeads, self).__init__(heads)
        self.cfg = cfg.clone()
        if cfg.MODEL.MASK_ON and cfg.MODEL.ROI_MASK_HEAD.SHARE_BOX_FEATURE_EXTRACTOR:
            self.mask.feature_extractor = self.box.feature_extractor
        if cfg.MODEL.KEYPOINT_ON and cfg.MODEL.ROI_KEYPOINT_HEAD.SHARE_BOX_FEATURE_EXTRACTOR:
            self.keypoint.feature_extractor = self.box.feature_extractor

    def forward(self, features, proposals, targets=None):
        """
        :param features: list[Tensor], 各个level的特征图
        :param proposals: list[Boxlist], 每张图片上保留的post_nms_top_n个anchor
        :param targets: list[Boxlist], 每张图片上的gt_box
        """

        losses = {}

        # x: [num_rois, 1024]
        # detections: (list[BoxList]) 训练阶段, 返回的是每张图片所有roi降采样之后保留的
        #   roi. 测试阶段, 返回的是每张图片上所有roi经过过滤之后保留的roi, coco中规定是100个.
        # losses: (dict[Tensor]) 训练阶段, 返回box head的loss. 测试阶段为空
        x, detections, loss_box = self.box(features, proposals, targets)
        losses.update(loss_box)

        if self.cfg.MODEL.MASK_ON:
            # 默认情况下使用ResNet backbone提取的特征
            mask_features = features
            if (self.training and self.cfg.MODEL.ROI_MASK_HEAD.SHARE_BOX_FEATURE_EXTRACTOR):
                mask_features = x

            # x: [num_pos_roi, 256, 14, 14]
            # detections: 训练阶段直接返回proposals, 测试阶段是筛选之后的roi
            x, detections, loss_mask = self.mask(mask_features, detections, targets)
            losses.update(loss_mask)

        # TODO: 暂时略过关键点检测
        if self.cfg.MODEL.KEYPOINT_ON:
            # 默认情况下使用ResNet backbone提取的特征
            keypoint_features = features
            # 可以设置keypoint head使用box head提取到的特征作为输入
            if (self.training and self.cfg.MODEL.ROI_KEYPOINT_HEAD.SHARE_BOX_FEATURE_EXTRACTOR):
                keypoint_features = x

            x, detections, loss_keypoint = self.keypoint(keypoint_features, detections, targets)
            losses.update(loss_keypoint)

        return x, detections, losses


def build_roi_heads(cfg, in_channels):
    """
    :param in_channels: FPN中是256, 即backbone输出的特征图的通道数
    """

    roi_heads = []
    if cfg.MODEL.RETINANET_ON:  # 默认为False
        return []

    if not cfg.MODEL.RPN_ONLY:
        roi_heads.append(("box", build_roi_box_head(cfg, in_channels)))
    if cfg.MODEL.MASK_ON:
        roi_heads.append(("mask", build_roi_mask_head(cfg, in_channels)))
    if cfg.MODEL.KEYPOINT_ON:
        roi_heads.append(("keypoint", build_roi_keypoint_head(cfg, in_channels)))

    if roi_heads:
        roi_heads = CombinedROIHeads(cfg, roi_heads)

    return roi_heads
