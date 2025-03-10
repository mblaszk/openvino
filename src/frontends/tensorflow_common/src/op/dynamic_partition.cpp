// Copyright (C) 2018-2023 Intel Corporation
// SPDX-License-Identifier: Apache-2.0
//

#include <limits>

#include "common_op_table.hpp"
#include "openvino/opsets/opset10.hpp"
#include "utils.hpp"

using namespace std;
using namespace ov;
using namespace ov::opset10;

namespace ov {
namespace frontend {
namespace tensorflow {
namespace op {
OutputVector translate_dynamic_partition_op(const NodeContext& node) {
    default_op_checks(node, 2, {"DynamicPartition"});
    auto data = node.get_input(0);
    auto partitions = node.get_input(1);

    // normalize partitions input since it can be a scalar or 1D tensor
    auto new_parts_shape = make_shared<Constant>(element::i64, Shape{1}, -1);
    auto norm_partitions = make_shared<Reshape>(partitions, new_parts_shape, true);

    // retrieve num_partitions attribute
    auto num_partitions = node.get_attribute<int64_t>("num_partitions");

    // compute how many slices are collected for each partition
    // 1. initially assume that we collect zero slices for each partition
    auto const_zero = make_shared<Constant>(element::i64, Shape{}, 0);
    auto target_shape = make_shared<Constant>(element::i64, Shape{1}, num_partitions);
    Output<Node> split_legths = make_shared<Broadcast>(const_zero, target_shape);
    // 2. compute unique partition indices and their occurrences
    auto axis = make_shared<Constant>(element::i32, Shape{1}, 0);
    auto unique_partition_inds = make_shared<Unique>(partitions);
    // 3. update split_lengths with a number of occurrences by each partition index
    split_legths = make_shared<ScatterUpdate>(split_legths,
                                              unique_partition_inds->output(0),
                                              unique_partition_inds->output(3),
                                              axis);

    // for stable sorting using TopK operation, we have to re-scale partition indices by the formula:
    // partition = partition * scale + partition_ind, where delta = max_int / num_partitions
    auto squeeze_axis = make_shared<Constant>(element::i64, Shape{1}, 0);
    Output<Node> norm_partitions_shape = make_shared<ShapeOf>(partitions, element::i32);
    norm_partitions_shape = make_shared<ConvertLike>(norm_partitions_shape, partitions);
    auto partitions_length = make_shared<Squeeze>(norm_partitions_shape, squeeze_axis);
    auto start2 = create_same_type_const_scalar<int32_t>(partitions, 0);
    auto step2 = create_same_type_const_scalar<int32_t>(partitions, 1);
    Output<Node> range_part_length = make_shared<Range>(start2, partitions_length, step2, element::i32);
    range_part_length = make_shared<ConvertLike>(range_part_length, partitions);
    auto scale = create_same_type_const_scalar<int32_t>(
        partitions,
        std::numeric_limits<int32_t>::max() / static_cast<int32_t>(num_partitions));
    auto term = make_shared<Multiply>(norm_partitions, scale);
    auto rescaled_partitions = make_shared<Add>(term, range_part_length);

    // sort partition indices so that they are ascending
    // and sort slices of data in the same order
    auto sorted_partitions = make_shared<TopK>(rescaled_partitions,
                                               partitions_length,
                                               0,
                                               TopK::Mode::MIN,
                                               TopK::SortType::SORT_VALUES,
                                               element::i64);
    auto gather_axis = make_shared<Constant>(element::i64, Shape{1}, 0);
    auto sorted_data = make_shared<Gather>(data, sorted_partitions->output(1), gather_axis);

    // when the data is sorted appropriately we are ready to split it
    auto split_axis = make_shared<Constant>(element::i64, Shape{1}, 0);
    auto result = make_shared<VariadicSplit>(sorted_data, split_axis, split_legths);
    set_node_name(node.get_name(), result);
    return result->outputs();
}
}  // namespace op
}  // namespace tensorflow
}  // namespace frontend
}  // namespace ov
