// Copyright (c) Facebook, Inc. and its affiliates. All Rights Reserved.
#include "cpu/vision.h"


template <typename scalar_t>
at::Tensor nms_cpu_kernel(const at::Tensor& dets,
                          const at::Tensor& scores,
                          const float threshold) {
  AT_ASSERTM(!dets.type().is_cuda(), "dets must be a CPU tensor");
  AT_ASSERTM(!scores.type().is_cuda(), "scores must be a CPU tensor");
  AT_ASSERTM(dets.type() == scores.type(), "dets should have the same type as scores");

  // numel(): Returns the total number of elements in the input tensor.
  if (dets.numel() == 0) {
    return at::empty({0}, dets.options().dtype(at::kLong).device(at::kCPU));
  }

  // select(dim, index)
  auto x1_t = dets.select(1, 0).contiguous();
  auto y1_t = dets.select(1, 1).contiguous();
  auto x2_t = dets.select(1, 2).contiguous();
  auto y2_t = dets.select(1, 3).contiguous();

  // 各个 box 的面积
  at::Tensor areas_t = (x2_t - x1_t + 1) * (y2_t - y1_t + 1);

  // A tuple of (sorted_tensor, sorted_indices) is returned
  // 取出排好序的 scores 在原 tensor 中的索引值
  // 即: order[0]是scores第一大的box在scores里的索引值, order[1]是scores第二大的box在scores里的索引值
  auto order_t = std::get<1>(scores.sort(0, /* descending=*/true));

  // anchors 的数量
  auto ndets = dets.size(0);
  at::Tensor suppressed_t = at::zeros({ndets}, dets.options().dtype(at::kByte).device(at::kCPU));

  auto suppressed = suppressed_t.data<uint8_t>();
  auto order = order_t.data<int64_t>();
  auto x1 = x1_t.data<scalar_t>();
  auto y1 = y1_t.data<scalar_t>();
  auto x2 = x2_t.data<scalar_t>();
  auto y2 = y2_t.data<scalar_t>();
  auto areas = areas_t.data<scalar_t>();

  for (int64_t _i = 0; _i < ndets; _i++) {
    // 当前 score 最大的 box 的下标
    auto i = order[_i];

    // 如果当前 box 已经在之前的操作中被 suppress 了, 直接跳过
    if (suppressed[i] == 1)
      continue;
    auto ix1 = x1[i];
    auto iy1 = y1[i];
    auto ix2 = x2[i];
    auto iy2 = y2[i];
    auto iarea = areas[i];

    // 分别计算剩余的所有 boxes 和 score 最大的 box 的 iou
    // iou 大于阈值的 boxes 直接移除
    for (int64_t _j = _i + 1; _j < ndets; _j++) {
      auto j = order[_j];

      // 如果当前 box 已经在之前的操作中被 suppress 了, 直接跳过
      if (suppressed[j] == 1)
        continue;

      // 两个 boxes 重合区域的 左上角和右下角 坐标
      auto xx1 = std::max(ix1, x1[j]);
      auto yy1 = std::max(iy1, y1[j]);
      auto xx2 = std::min(ix2, x2[j]);
      auto yy2 = std::min(iy2, y2[j]);

      auto w = std::max(static_cast<scalar_t>(0), xx2 - xx1 + 1);
      auto h = std::max(static_cast<scalar_t>(0), yy2 - yy1 + 1);
      auto inter = w * h;

      // 计算IoU，inter / area1 + area2 - inter
      auto ovr = inter / (iarea + areas[j] - inter);

      // 若 box[j] 与 box[i] 的 IoU 大于阈值, 直接标记第 j 个 box 被 suppress
      if (ovr >= threshold)
        suppressed[j] = 1;
   }
  }

  // nonzero: returns a tensor containing the indices of all non-zero elements
  return at::nonzero(suppressed_t == 0).squeeze(1);
}

// dets: 所有 anchors 的四个坐标值 [H*W*A, 4]
// scores: 所有 anchors 的置信度 [H*W*A, ]
at::Tensor nms_cpu(const at::Tensor& dets,
               const at::Tensor& scores,
               const float threshold) {
  at::Tensor result;

  // ATEN 提供了接口函数 AT_DISPATCH_FLOATING_TYPES, 这个函数接收三个参数
  // 第一个参数是输入数据的源类型
  // 第二个参数是操作的标识符（用于报错显示）
  // 第三个参数是一个匿名函数
  // 在匿名函数运行结束后, AT_DISPATCH_FLOATING_TYPES 会将 Float 数组转化为目标类型（运行中的实际类型）数组
  AT_DISPATCH_FLOATING_TYPES(dets.type(), "nms", [&] {
    result = nms_cpu_kernel<scalar_t>(dets, scores, threshold);
  });

  return result;
}

/* 关于匿名函数

    [](int x, int y) { return x + y; }
    [配置](参数){程序体}

    [&](int x, int y) { return x + y; }
    参数按引用传递

    [=](int x, int y) { return x + y; }
    参数按值传递

*/