// Copyright (C) 2018-2023 Intel Corporation
// SPDX-License-Identifier: Apache-2.0
//

#include "openvino/core/preprocess/input_tensor_info.hpp"
#include "openvino/op/nv12_to_bgr.hpp"
#include "openvino/op/nv12_to_rgb.hpp"
#include "openvino/op/i420_to_bgr.hpp"
#include "openvino/op/i420_to_rgb.hpp"

#include "intel_gpu/plugin/program_builder.hpp"
#include "intel_gpu/plugin/common_utils.hpp"

#include "intel_gpu/primitives/convert_color.hpp"
#include "intel_gpu/primitives/concatenation.hpp"

namespace ov {
namespace intel_gpu {

static void CreateCommonConvertColorOp(ProgramBuilder& p, const std::shared_ptr<ov::Node>& op,
                                       const cldnn::convert_color::color_format from_color,
                                       const cldnn::convert_color::color_format to_color) {
    auto inputs = p.GetInputInfo(op);
    std::string layerName = layer_type_name_ID(op);

    auto outDatatype = cldnn::element_type_to_data_type(op->get_input_element_type(0));
    auto outShape = tensor_from_dims(op->get_output_shape(0));
    outShape = { outShape.sizes()[0], outShape.sizes()[2], outShape.sizes()[3], outShape.sizes()[1] };

    auto out_layout = cldnn::layout(outDatatype, cldnn::format::byxf, outShape);

    auto memory_type = cldnn::convert_color::memory_type::buffer;
    if (op->get_input_node_ptr(0)->output(0).get_rt_info().count(ov::preprocess::TensorInfoMemoryType::get_type_info_static())) {
        std::string mem_type = op->get_input_node_ptr(0)->output(0).get_rt_info().at(ov::preprocess::TensorInfoMemoryType::get_type_info_static())
                                                                                 .as<ov::preprocess::TensorInfoMemoryType>().value;
        if (mem_type.find(ov::intel_gpu::memory_type::surface) != std::string::npos) {
            memory_type = cldnn::convert_color::memory_type::image;
        }
    }

    if (outShape.batch[0] > 1 && memory_type == cldnn::convert_color::memory_type::image) {
        std::vector<cldnn::input_info> convert_color_names;
        for (int b = 0; b < outShape.batch[0]; ++b) {
            cldnn::primitive::input_info_arr batched_inputs;
            for (size_t i = 0; i < inputs.size(); ++i) {
                batched_inputs.emplace_back(cldnn::input_info(inputs[i].pid + "_" + std::to_string(b), inputs[i].idx));
            }
            cldnn::primitive_id batched_prim_id = layerName + "_" + std::to_string(b);
            convert_color_names.emplace_back(cldnn::input_info(batched_prim_id));
            auto new_shape = outShape;
            new_shape.batch[0] = 1;
            out_layout.set_tensor(new_shape);

            p.add_primitive(*op, cldnn::convert_color(batched_prim_id,
                                                      batched_inputs,
                                                      from_color,
                                                      to_color,
                                                      memory_type,
                                                      out_layout));
        }
        p.add_primitive(*op, cldnn::concatenation(layerName, convert_color_names, 0));
    } else {
        p.add_primitive(*op, cldnn::convert_color(layerName,
                                                  inputs,
                                                  from_color,
                                                  to_color,
                                                  memory_type,
                                                  out_layout));
    }
}

static void CreateNV12toRGBOp(ProgramBuilder& p, const std::shared_ptr<ov::op::v8::NV12toRGB>& op) {
    validate_inputs_count(op, {1, 2});
    CreateCommonConvertColorOp(p, op, cldnn::convert_color::color_format::NV12, cldnn::convert_color::color_format::RGB);
}

static void CreateNV12toBGROp(ProgramBuilder& p, const std::shared_ptr<ov::op::v8::NV12toBGR>& op) {
    validate_inputs_count(op, {1, 2});
    CreateCommonConvertColorOp(p, op, cldnn::convert_color::color_format::NV12, cldnn::convert_color::color_format::BGR);
}

static void CreateI420toRGBOp(ProgramBuilder& p, const std::shared_ptr<ov::op::v8::I420toRGB>& op) {
    validate_inputs_count(op, {1, 3});
    CreateCommonConvertColorOp(p, op, cldnn::convert_color::color_format::I420, cldnn::convert_color::color_format::RGB);
}

static void CreateI420toBGROp(ProgramBuilder& p, const std::shared_ptr<ov::op::v8::I420toBGR>& op) {
    validate_inputs_count(op, {1, 3});
    CreateCommonConvertColorOp(p, op, cldnn::convert_color::color_format::I420, cldnn::convert_color::color_format::BGR);
}

REGISTER_FACTORY_IMPL(v8, NV12toRGB);
REGISTER_FACTORY_IMPL(v8, NV12toBGR);
REGISTER_FACTORY_IMPL(v8, I420toRGB);
REGISTER_FACTORY_IMPL(v8, I420toBGR);

}  // namespace intel_gpu
}  // namespace ov
