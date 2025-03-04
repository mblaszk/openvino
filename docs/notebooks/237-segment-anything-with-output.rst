Object masks from prompts with SAM and OpenVINO
===============================================

.. _top:

**Table of contents**:

- `Background <#background>`__
- `Prerequisites <#prerequisites>`__
- `Convert model to OpenVINO Intermediate Representation <#convert-model-to-openvino-intermediate-representation>`__

  - `Download model checkpoint and create PyTorch model <#download-model-checkpoint-and-create-pytorch-model>`__
  - `Image Encoder <#image-encoder>`__
  - `Mask predictor <#mask-predictor>`__

- `Run OpenVINO model in interactive segmentation mode <#run-openvino-model-in-interactive-segmentation-mode>`__

  - `Example Image <#example-image>`__
  - `Preprocessing and visualization utilities <#preprocessing-and-visualization-utilities>`__
  - `Image encoding <#image-encoding>`__
  - `Example point input <#example-point-input>`__
  - `Example with multiple points <#example-with-multiple-points>`__
  - `Example box and point input with negative label <#example-box-and-point-input-with-negative-label>`__

- `Interactive segmentation <#interactive-segmentation>`__
- `Run OpenVINO model in automatic mask generation mode <#run-openvino-model-in-automatic-mask-generation-mode>`__
- `Optimize encoder using NNCF Post-training Quantization API <#optimize-encoder-using-nncf-post-training-quantization-api>`__

  - `Prepare a calibration dataset <#prepare-a-calibration-dataset>`__
  - `Run quantization and serialize OpenVINO IR model <#run-quantization-and-serialize-openvino-ir-model>`__
  - `Validate Quantized Model Inference <#validate-quantized-model-inference>`__
  - `Compare Performance of the Original and Quantized Models <#compare-performance-of-the-original-and-quantized-models>`__

Segmentation - identifying which image pixels belong to an object - is a
core task in computer vision and is used in a broad array of
applications, from analyzing scientific imagery to editing photos. But
creating an accurate segmentation model for specific tasks typically
requires highly specialized work by technical experts with access to AI
training infrastructure and large volumes of carefully annotated
in-domain data. Reducing the need for task-specific modeling expertise,
training compute, and custom data annotation for image segmentation is
the main goal of the `Segment
Anything <https://arxiv.org/abs/2304.02643>`__ project.

The `Segment Anything Model
(SAM) <https://github.com/facebookresearch/segment-anything>`__ predicts
object masks given prompts that indicate the desired object. SAM has
learned a general notion of what objects are, and it can generate masks
for any object in any image or any video, even including objects and
image types that it had not encountered during training. SAM is general
enough to cover a broad set of use cases and can be used out of the box
on new image “domains” (e.g. underwater photos, MRI or cell microscopy)
without requiring additional training (a capability often referred to as
zero-shot transfer). This notebook shows an example of how to convert
and use Segment Anything Model in OpenVINO format, allowing it to run on
a variety of platforms that support an OpenVINO.

Background `⇑ <#top>`__
###############################################################################################################################


Previously, to solve any kind of segmentation problem, there were two
classes of approaches. The first, interactive segmentation, allowed for
segmenting any class of object but required a person to guide the method
by iterative refining a mask. The second, automatic segmentation,
allowed for segmentation of specific object categories defined ahead of
time (e.g., cats or chairs) but required substantial amounts of manually
annotated objects to train (e.g., thousands or even tens of thousands of
examples of segmented cats), along with the compute resources and
technical expertise to train the segmentation model. Neither approach
provided a general, fully automatic approach to segmentation.

Segment Anything Model is a generalization of these two classes of
approaches. It is a single model that can easily perform both
interactive segmentation and automatic segmentation. The Segment
Anything Model (SAM) produces high quality object masks from input
prompts such as points or boxes, and it can be used to generate masks
for all objects in an image. It has been trained on a
`dataset <https://segment-anything.com/dataset/index.html>`__ of 11
million images and 1.1 billion masks, and has strong zero-shot
performance on a variety of segmentation tasks. The model consists of 3
parts:

-  **Image Encoder** - Vision Transformer model (VIT) pretrained using
   Masked Auto Encoders approach (MAE) for encoding image to embedding
   space. The image encoder runs once per image and can be applied prior
   to prompting the model.
-  **Prompt Encoder** - Encoder for segmentation condition. As a
   condition can be used:

   -  points - set of points related to object which should be
      segmented. Prompt encoder converts points to embedding using
      positional encoding.
   -  boxes - bounding box where object for segmentation is located.
      Similar to points, coordinates of bounding box encoded via
      positional encoding.
   -  segmentation mask - provided by user segmentation mask is embedded
      using convolutions and summed element-wise with the image
      embedding.
   -  text - encoded by CLIP model text representation

-  **Mask Decoder** - The mask decoder efficiently maps the image
   embedding, prompt embeddings, and an output token to a mask.

The diagram below demonstrates the process of mask generation using SAM:
|model_diagram|

The model first converts the image into an image embedding that allows
high quality masks to be efficiently produced from a prompt. The model
returns multiple masks which fit to the provided prompt and its score.
The provided masks can be overlapped areas as it shown on diagram, it is
useful for complicated cases when prompt can be interpreted in different
manner, e.g. segment whole object or only its specific part or when
provided point at the intersection of multiple objects. The model’s
promptable interface allows it to be used in flexible ways that make a
wide range of segmentation tasks possible simply by engineering the
right prompt for the model (clicks, boxes, text, and so on).

More details about approach can be found in the
`paper <https://arxiv.org/abs/2304.02643>`__, original
`repo <https://github.com/facebookresearch/segment-anything>`__ and
`Meta AI blog
post <https://ai.facebook.com/blog/segment-anything-foundation-model-image-segmentation/>`__

.. |model_diagram| image:: https://raw.githubusercontent.com/facebookresearch/segment-anything/main/assets/model_diagram.png

Prerequisites `⇑ <#top>`__
###############################################################################################################################


.. code:: ipython3

    !pip install -q "segment_anything" "gradio>=3.25"


.. parsed-literal::

    
    [notice] A new release of pip is available: 23.1.2 -> 23.2
    [notice] To update, run: pip install --upgrade pip


Convert model to OpenVINO Intermediate Representation `⇑ <#top>`__
###############################################################################################################################


Download model checkpoint and create PyTorch model `⇑ <#top>`__
+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++


There are several Segment Anything Model
`checkpoints <https://github.com/facebookresearch/segment-anything#model-checkpoints>`__
available for downloading In this tutorial we will use model based on
``vit_b``, but the demonstrated approach is very general and applicable
to other SAM models. Set the model URL, path for saving checkpoint and
model type below to a SAM model checkpoint, then load the model using
``sam_model_registry``.

.. code:: ipython3

    import sys
    
    sys.path.append("../utils")
    from notebook_utils import download_file
    
    checkpoint = "sam_vit_b_01ec64.pth"
    model_url = "https://dl.fbaipublicfiles.com/segment_anything/sam_vit_b_01ec64.pth"
    model_type = "vit_b"
    
    download_file(model_url)


.. parsed-literal::

    'sam_vit_b_01ec64.pth' already exists.




.. parsed-literal::

    PosixPath('/home/ea/work/openvino_notebooks/notebooks/237-segment-anything/sam_vit_b_01ec64.pth')



.. code:: ipython3

    from segment_anything import sam_model_registry
    
    sam = sam_model_registry[model_type](checkpoint=checkpoint)

As we already discussed, Image Encoder part can be used once per image,
then changing prompt, prompt encoder and mask decoder can be run
multiple times to retrieve different objects from the same image. Taking
into account this fact, we split model on 2 independent parts:
image_encoder and mask_predictor (combination of Prompt Encoder and Mask
Decoder).

Image Encoder `⇑ <#top>`__
+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++


Image Encoder input is tensor with shape ``1x3x1024x1024`` in ``NCHW``
format, contains image for segmentation. Image Encoder output is image
embeddings, tensor with shape ``1x256x64x64``

.. code:: ipython3

    import warnings
    from pathlib import Path
    import torch
    from openvino.tools import mo
    from openvino.runtime import serialize, Core
    
    core = Core()
    
    ov_encoder_path = Path("sam_image_encoder.xml")
    onnx_encoder_path = ov_encoder_path.with_suffix(".onnx")
    if not ov_encoder_path.exists():
        if not onnx_encoder_path.exists():
            with warnings.catch_warnings():
                warnings.filterwarnings("ignore", category=torch.jit.TracerWarning)
                warnings.filterwarnings("ignore", category=UserWarning)
    
                torch.onnx.export(sam.image_encoder, torch.zeros(1,3,1024,1024), onnx_encoder_path)
    
        ov_encoder_model = mo.convert_model(onnx_encoder_path, compress_to_fp16=True)
        serialize(ov_encoder_model, str(ov_encoder_path))
    else:
        ov_encoder_model = core.read_model(ov_encoder_path)

.. code:: ipython3

    import ipywidgets as widgets
    
    device = widgets.Dropdown(
        options=core.available_devices + ["AUTO"],
        value='AUTO',
        description='Device:',
        disabled=False,
    )
    
    device




.. parsed-literal::

    Dropdown(description='Device:', index=2, options=('CPU', 'GPU', 'AUTO'), value='AUTO')



.. code:: ipython3

    ov_encoder = core.compile_model(ov_encoder_model, device.value)

Mask predictor `⇑ <#top>`__
+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++


This notebook expects the model was exported with the parameter
``return_single_mask=True``. It means that model will only return the
best mask, instead of returning multiple masks. For high resolution
images this can improve runtime when upscaling masks is expensive.

Combined prompt encoder and mask decoder model has following list of
inputs:

-  ``image_embeddings``: The image embedding from ``image_encoder``. Has
   a batch index of length 1.
-  ``point_coords``: Coordinates of sparse input prompts, corresponding
   to both point inputs and box inputs. Boxes are encoded using two
   points, one for the top-left corner and one for the bottom-right
   corner. *Coordinates must already be transformed to long-side 1024.*
   Has a batch index of length 1.
-  ``point_labels``: Labels for the sparse input prompts. 0 is a
   negative input point, 1 is a positive input point, 2 is a top-left
   box corner, 3 is a bottom-right box corner, and -1 is a padding
   point. \*If there is no box input, a single padding point with label
   -1 and coordinates (0.0, 0.0) should be concatenated.

Model outputs:

-  ``masks`` - predicted masks resized to original image size, to obtain
   a binary mask, should be compared with ``threshold`` (usually equal
   0.0).
-  ``iou_predictions`` - intersection over union predictions
-  ``low_res_masks`` - predicted masks before postprocessing, can be
   used as mask input for model.

.. code:: ipython3

    from typing import Tuple
    
    class SamONNXModel(torch.nn.Module):
        def __init__(
            self,
            model,
            return_single_mask: bool,
            use_stability_score: bool = False,
            return_extra_metrics: bool = False,
        ) -> None:
            super().__init__()
            self.mask_decoder = model.mask_decoder
            self.model = model
            self.img_size = model.image_encoder.img_size
            self.return_single_mask = return_single_mask
            self.use_stability_score = use_stability_score
            self.stability_score_offset = 1.0
            self.return_extra_metrics = return_extra_metrics
    
        def _embed_points(self, point_coords: torch.Tensor, point_labels: torch.Tensor) -> torch.Tensor:
            point_coords = point_coords + 0.5
            point_coords = point_coords / self.img_size
            point_embedding = self.model.prompt_encoder.pe_layer._pe_encoding(point_coords)
            point_labels = point_labels.unsqueeze(-1).expand_as(point_embedding)
    
            point_embedding = point_embedding * (point_labels != -1)
            point_embedding = point_embedding + self.model.prompt_encoder.not_a_point_embed.weight * (
                point_labels == -1
            )
    
            for i in range(self.model.prompt_encoder.num_point_embeddings):
                point_embedding = point_embedding + self.model.prompt_encoder.point_embeddings[
                    i
                ].weight * (point_labels == i)
    
            return point_embedding
    
        def t_embed_masks(self, input_mask: torch.Tensor) -> torch.Tensor:
            mask_embedding = self.model.prompt_encoder.mask_downscaling(input_mask)
            return mask_embedding
    
        def mask_postprocessing(self, masks: torch.Tensor) -> torch.Tensor:
            masks = torch.nn.functional.interpolate(
                masks,
                size=(self.img_size, self.img_size),
                mode="bilinear",
                align_corners=False,
            )
            return masks
    
        def select_masks(
            self, masks: torch.Tensor, iou_preds: torch.Tensor, num_points: int
        ) -> Tuple[torch.Tensor, torch.Tensor]:
            # Determine if we should return the multiclick mask or not from the number of points.
            # The reweighting is used to avoid control flow.
            score_reweight = torch.tensor(
                [[1000] + [0] * (self.model.mask_decoder.num_mask_tokens - 1)]
            ).to(iou_preds.device)
            score = iou_preds + (num_points - 2.5) * score_reweight
            best_idx = torch.argmax(score, dim=1)
            masks = masks[torch.arange(masks.shape[0]), best_idx, :, :].unsqueeze(1)
            iou_preds = iou_preds[torch.arange(masks.shape[0]), best_idx].unsqueeze(1)
    
            return masks, iou_preds
    
        @torch.no_grad()
        def forward(
            self,
            image_embeddings: torch.Tensor,
            point_coords: torch.Tensor,
            point_labels: torch.Tensor,
            mask_input: torch.Tensor = None,
        ):
            sparse_embedding = self._embed_points(point_coords, point_labels)
            if mask_input is None:
                dense_embedding = self.model.prompt_encoder.no_mask_embed.weight.reshape(1, -1, 1, 1).expand(
                    point_coords.shape[0], -1, image_embeddings.shape[0], 64
                )
            else:
                dense_embedding = self._embed_masks(mask_input)
    
            masks, scores = self.model.mask_decoder.predict_masks(
                image_embeddings=image_embeddings,
                image_pe=self.model.prompt_encoder.get_dense_pe(),
                sparse_prompt_embeddings=sparse_embedding,
                dense_prompt_embeddings=dense_embedding,
            )
    
            if self.use_stability_score:
                scores = calculate_stability_score(
                    masks, self.model.mask_threshold, self.stability_score_offset
                )
    
            if self.return_single_mask:
                masks, scores = self.select_masks(masks, scores, point_coords.shape[1])
    
            upscaled_masks = self.mask_postprocessing(masks)
    
            if self.return_extra_metrics:
                stability_scores = calculate_stability_score(
                    upscaled_masks, self.model.mask_threshold, self.stability_score_offset
                )
                areas = (upscaled_masks > self.model.mask_threshold).sum(-1).sum(-1)
                return upscaled_masks, scores, stability_scores, areas, masks
    
            return upscaled_masks, scores
    
    ov_model_path = Path("sam_mask_predictor.xml")
    if not ov_model_path.exists():
        onnx_model_path = ov_model_path.with_suffix('.onnx')
        if not onnx_model_path.exists():
            onnx_model = SamONNXModel(sam, return_single_mask=True)
            dynamic_axes = {
                "point_coords": {0: "batch_size", 1: "num_points"},
                "point_labels": {0: "batch_size", 1: "num_points"},
            }
    
            embed_dim = sam.prompt_encoder.embed_dim
            embed_size = sam.prompt_encoder.image_embedding_size
            dummy_inputs = {
                "image_embeddings": torch.randn(1, embed_dim, *embed_size, dtype=torch.float),
                "point_coords": torch.randint(low=0, high=1024, size=(1, 5, 2), dtype=torch.float),
                "point_labels": torch.randint(low=0, high=4, size=(1, 5), dtype=torch.float),
            }
            output_names = ["masks", "iou_predictions"]
    
            with warnings.catch_warnings():
                warnings.filterwarnings("ignore", category=torch.jit.TracerWarning)
                warnings.filterwarnings("ignore", category=UserWarning)
                torch.onnx.export(
                    onnx_model,
                    tuple(dummy_inputs.values()),
                    onnx_model_path,
                    input_names=list(dummy_inputs.keys()),
                    output_names=output_names,
                    dynamic_axes=dynamic_axes,
                )
    
        ov_model = mo.convert_model(onnx_model_path, compress_to_fp16=True)
        serialize(ov_model, str(ov_model_path))
    else:
        ov_model = core.read_model(ov_model_path)

.. code:: ipython3

    device




.. parsed-literal::

    Dropdown(description='Device:', index=2, options=('CPU', 'GPU', 'AUTO'), value='AUTO')



.. code:: ipython3

    ov_predictor = core.compile_model(ov_model, device.value)

Run OpenVINO model in interactive segmentation mode `⇑ <#top>`__
###############################################################################################################################


Example Image `⇑ <#top>`__
+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++


.. code:: ipython3

    import numpy as np
    import cv2
    import matplotlib.pyplot as plt
    
    download_file("https://raw.githubusercontent.com/facebookresearch/segment-anything/main/notebooks/images/truck.jpg")
    image = cv2.imread('truck.jpg')
    image = cv2.cvtColor(image, cv2.COLOR_BGR2RGB)


.. parsed-literal::

    'truck.jpg' already exists.


.. code:: ipython3

    plt.figure(figsize=(10,10))
    plt.imshow(image)
    plt.axis('off')
    plt.show()



.. image:: 237-segment-anything-with-output_files/237-segment-anything-with-output_21_0.png


Preprocessing and visualization utilities `⇑ <#top>`__
+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++


To prepare input for Image Encoder we should:

1. Convert BGR image to RGB
2. Resize image saving aspect ratio where longest size equal to Image
   Encoder input size - 1024.
3. Normalize image subtract mean values (123.675, 116.28, 103.53) and
   divide by std (58.395, 57.12, 57.375)
4. Transpose HWC data layout to CHW and add batch dimension.
5. Add zero padding to input tensor by height or width (depends on
   aspect ratio) according Image Encoder expected input shape.

These steps are applicable to all available models

.. code:: ipython3

    from copy import deepcopy
    from typing import Tuple
    from torchvision.transforms.functional import resize, to_pil_image 
    
    class ResizeLongestSide:
        """
        Resizes images to longest side 'target_length', as well as provides
        methods for resizing coordinates and boxes. Provides methods for
        transforming numpy arrays.
        """
    
        def __init__(self, target_length: int) -> None:
            self.target_length = target_length
    
        def apply_image(self, image: np.ndarray) -> np.ndarray:
            """
            Expects a numpy array with shape HxWxC in uint8 format.
            """
            target_size = self.get_preprocess_shape(image.shape[0], image.shape[1], self.target_length)
            return np.array(resize(to_pil_image(image), target_size))
    
        def apply_coords(self, coords: np.ndarray, original_size: Tuple[int, ...]) -> np.ndarray:
            """
            Expects a numpy array of length 2 in the final dimension. Requires the
            original image size in (H, W) format.
            """
            old_h, old_w = original_size
            new_h, new_w = self.get_preprocess_shape(
                original_size[0], original_size[1], self.target_length
            )
            coords = deepcopy(coords).astype(float)
            coords[..., 0] = coords[..., 0] * (new_w / old_w)
            coords[..., 1] = coords[..., 1] * (new_h / old_h)
            return coords
    
        def apply_boxes(self, boxes: np.ndarray, original_size: Tuple[int, ...]) -> np.ndarray:
            """
            Expects a numpy array shape Bx4. Requires the original image size
            in (H, W) format.
            """
            boxes = self.apply_coords(boxes.reshape(-1, 2, 2), original_size)
            return boxes.reshape(-1, 4)
    
        @staticmethod
        def get_preprocess_shape(oldh: int, oldw: int, long_side_length: int) -> Tuple[int, int]:
            """
            Compute the output size given input size and target long side length.
            """
            scale = long_side_length * 1.0 / max(oldh, oldw)
            newh, neww = oldh * scale, oldw * scale
            neww = int(neww + 0.5)
            newh = int(newh + 0.5)
            return (newh, neww)
    
    
    resizer = ResizeLongestSide(1024)
    
    
    def preprocess_image(image: np.ndarray):
        resized_image = resizer.apply_image(image)
        resized_image = (resized_image.astype(np.float32) - [123.675, 116.28, 103.53]) / [58.395, 57.12, 57.375]
        resized_image = np.expand_dims(np.transpose(resized_image, (2, 0, 1)).astype(np.float32), 0)
    
        # Pad
        h, w = resized_image.shape[-2:]
        padh = 1024 - h
        padw = 1024 - w
        x = np.pad(resized_image, ((0, 0), (0, 0), (0, padh), (0, padw)))
        return x
    
    
    def postprocess_masks(masks: np.ndarray, orig_size):
        size_before_pad = resizer.get_preprocess_shape(orig_size[0], orig_size[1], masks.shape[-1])
        masks = masks[..., :int(size_before_pad[0]), :int(size_before_pad[1])]
        masks = torch.nn.functional.interpolate(torch.from_numpy(masks), size=orig_size, mode="bilinear", align_corners=False).numpy()
        return masks

.. code:: ipython3

    def show_mask(mask, ax):
        color = np.array([30 / 255, 144 / 255, 255 / 255, 0.6])
        h, w = mask.shape[-2:]
        mask_image = mask.reshape(h, w, 1) * color.reshape(1, 1, -1)
        ax.imshow(mask_image)
    
        
    def show_points(coords, labels, ax, marker_size=375):
        pos_points = coords[labels == 1]
        neg_points = coords[labels == 0]
        ax.scatter(pos_points[:, 0], pos_points[:, 1], color='green', marker='*', s=marker_size, edgecolor='white', linewidth=1.25)
        ax.scatter(neg_points[:, 0], neg_points[:, 1], color='red', marker='*', s=marker_size, edgecolor='white', linewidth=1.25)   
    
        
    def show_box(box, ax):
        x0, y0 = box[0], box[1]
        w, h = box[2] - box[0], box[3] - box[1]
        ax.add_patch(plt.Rectangle((x0, y0), w, h, edgecolor='green', facecolor=(0, 0, 0, 0), lw=2))  

Image encoding `⇑ <#top>`__
+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++


To start work with image, we should preprocess it and obtain image
embeddings using ``ov_encoder``. We will use the same image for all
experiments, so it is possible to generate image embedding once and then
reuse them.

.. code:: ipython3

    preprocessed_image = preprocess_image(image)
    encoding_results = ov_encoder(preprocessed_image)
    
    image_embeddings = encoding_results[ov_encoder.output(0)]

Now, we can try to provide different prompts for mask generation

Example point input `⇑ <#top>`__
+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++


In this example we select one point. The green star symbol show its
location on the image below.

.. code:: ipython3

    input_point = np.array([[500, 375]])
    input_label = np.array([1])
    
    plt.figure(figsize=(10,10))
    plt.imshow(image)
    show_points(input_point, input_label, plt.gca())
    plt.axis('off')
    plt.show() 



.. image:: 237-segment-anything-with-output_files/237-segment-anything-with-output_28_0.png


Add a batch index, concatenate a padding point, and transform it to
input tensor coordinate system.

.. code:: ipython3

    coord = np.concatenate([input_point, np.array([[0.0, 0.0]])], axis=0)[None, :, :]
    label = np.concatenate([input_label, np.array([-1])], axis=0)[None, :].astype(np.float32)
    coord = resizer.apply_coords(coord, image.shape[:2]).astype(np.float32)

Package the inputs to run in the mask predictor.

.. code:: ipython3

    inputs = {
        "image_embeddings": image_embeddings,
        "point_coords": coord,
        "point_labels": label,
    }

Predict a mask and threshold it to get binary mask (0 - no object, 1 -
object).

.. code:: ipython3

    results = ov_predictor(inputs)
    
    masks = results[ov_predictor.output(0)]
    masks = postprocess_masks(masks, image.shape[:-1])
    masks = masks > 0.0

.. code:: ipython3

    plt.figure(figsize=(10,10))
    plt.imshow(image)
    show_mask(masks, plt.gca())
    show_points(input_point, input_label, plt.gca())
    plt.axis('off')
    plt.show() 



.. image:: 237-segment-anything-with-output_files/237-segment-anything-with-output_35_0.png


Example with multiple points `⇑ <#top>`__
+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++


in this example, we provide additional point for cover larger object
area.

.. code:: ipython3

    input_point = np.array([[500, 375], [1125, 625], [575, 750], [1405, 575]])
    input_label = np.array([1, 1, 1, 1])

Now, prompt for model looks like represented on this image:

.. code:: ipython3

    plt.figure(figsize=(10,10))
    plt.imshow(image)
    show_points(input_point, input_label, plt.gca())
    plt.axis('off')
    plt.show() 



.. image:: 237-segment-anything-with-output_files/237-segment-anything-with-output_39_0.png


Transform the points as in the previous example.

.. code:: ipython3

    coord = np.concatenate([input_point, np.array([[0.0, 0.0]])], axis=0)[None, :, :]
    label = np.concatenate([input_label, np.array([-1])], axis=0)[None, :].astype(np.float32)
    
    coord = resizer.apply_coords(coord, image.shape[:2]).astype(np.float32)

Package inputs, then predict and threshold the mask.

.. code:: ipython3

    inputs = {
        "image_embeddings": image_embeddings,
        "point_coords": coord,
        "point_labels": label,
    }
    
    results = ov_predictor(inputs)
    
    masks = results[ov_predictor.output(0)]
    masks = postprocess_masks(masks, image.shape[:-1])
    masks = masks > 0.0

.. code:: ipython3

    plt.figure(figsize=(10,10))
    plt.imshow(image)
    show_mask(masks, plt.gca())
    show_points(input_point, input_label, plt.gca())
    plt.axis('off')
    plt.show() 



.. image:: 237-segment-anything-with-output_files/237-segment-anything-with-output_44_0.png


Great! Looks like now, predicted mask cover whole truck.

Example box and point input with negative label `⇑ <#top>`__
+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++


In this example we define input prompt using bounding box and point
inside it.The bounding box represented as set of points of its left
upper corner and right lower corner. Label 0 for point speak that this
point should be excluded from mask.

.. code:: ipython3

    input_box = np.array([425, 600, 700, 875])
    input_point = np.array([[575, 750]])
    input_label = np.array([0])

.. code:: ipython3

    plt.figure(figsize=(10, 10))
    plt.imshow(image)
    show_box(input_box, plt.gca())
    show_points(input_point, input_label, plt.gca())
    plt.axis('off')
    plt.show()



.. image:: 237-segment-anything-with-output_files/237-segment-anything-with-output_48_0.png


Add a batch index, concatenate a box and point inputs, add the
appropriate labels for the box corners, and transform. There is no
padding point since the input includes a box input.

.. code:: ipython3

    box_coords = input_box.reshape(2, 2)
    box_labels = np.array([2,3])
    
    coord = np.concatenate([input_point, box_coords], axis=0)[None, :, :]
    label = np.concatenate([input_label, box_labels], axis=0)[None, :].astype(np.float32)
    
    coord = resizer.apply_coords(coord, image.shape[:2]).astype(np.float32)

Package inputs, then predict and threshold the mask.

.. code:: ipython3

    inputs = {
        "image_embeddings": image_embeddings,
        "point_coords": coord,
        "point_labels": label,
    }
    
    results = ov_predictor(inputs)
    
    masks = results[ov_predictor.output(0)]
    masks = postprocess_masks(masks, image.shape[:-1])
    masks = masks > 0.0

.. code:: ipython3

    plt.figure(figsize=(10, 10))
    plt.imshow(image)
    show_mask(masks[0], plt.gca())
    show_box(input_box, plt.gca())
    show_points(input_point, input_label, plt.gca())
    plt.axis('off')
    plt.show()



.. image:: 237-segment-anything-with-output_files/237-segment-anything-with-output_53_0.png


Interactive segmentation `⇑ <#top>`__
###############################################################################################################################


Now, you can try SAM on own image. Upload image to input window and
click on desired point, model predict segment based on your image and
point.

.. code:: ipython3

    import gradio as gr
    
    class Segmenter:
        def __init__(self, ov_encoder, ov_predictor):
            self.encoder = ov_encoder
            self.predictor = ov_predictor
            self._img_embeddings = None
    
        def set_image(self, img:np.ndarray):
            if self._img_embeddings is not None:
                del self._img_embeddings
            preprocessed_image = preprocess_image(img)
            encoding_results = self.encoder(preprocessed_image)
            image_embeddings = encoding_results[ov_encoder.output(0)]
            self._img_embeddings = image_embeddings
            return img
    
        def get_mask(self, points, img):
            coord = np.array(points)
            coord = np.concatenate([coord, np.array([[0,0]])], axis=0)
            coord = coord[None, :, :]
            label = np.concatenate([np.ones(len(points)), np.array([-1])], axis=0)[None, :].astype(np.float32)
            coord = resizer.apply_coords(coord, img.shape[:2]).astype(np.float32)
            if self._img_embeddings is None:
                self.set_image(img)
            inputs = {
                "image_embeddings": self._img_embeddings,
                "point_coords": coord,
                "point_labels": label,
            }
    
            results = self.predictor(inputs)
            masks = results[ov_predictor.output(0)]
            masks = postprocess_masks(masks, img.shape[:-1])
            
            masks = masks > 0.0
            mask = masks[0]
            mask = np.transpose(mask, (1, 2, 0))
            return mask
            
    segmenter = Segmenter(ov_encoder, ov_predictor)
            
            
    with gr.Blocks() as demo:
        with gr.Row():
            input_img = gr.Image(label="Input", type="numpy").style(height=480, width=480)
            output_img = gr.Image(label="Selected Segment", type="numpy").style(height=480, width=480)
        
        def on_image_change(img):
            segmenter.set_image(img)
            return img
    
        def get_select_coords(img, evt: gr.SelectData):
            pixels_in_queue = set()
            h, w = img.shape[:2]
            pixels_in_queue.add((evt.index[0], evt.index[1]))
            out = img.copy()
            while len(pixels_in_queue) > 0:
                pixels = list(pixels_in_queue)
                pixels_in_queue = set()
                color = np.random.randint(0, 255, size=(1, 1, 3))
                mask = segmenter.get_mask(pixels, img)
                mask_image = out.copy()
                mask_image[mask.squeeze(-1)] = color
                out = cv2.addWeighted(out.astype(np.float32), 0.7, mask_image.astype(np.float32), 0.3, 0.0)
            out = out.astype(np.uint8)
            return out
        
        input_img.select(get_select_coords, [input_img], output_img)
        input_img.upload(on_image_change, [input_img], [input_img])
    
    if __name__ == "__main__":
        try:
            demo.launch()
        except Exception:
            demo.launch(share=True)


.. parsed-literal::

    /tmp/ipykernel_1187339/1907223323.py:46: GradioDeprecationWarning: The `style` method is deprecated. Please set these arguments in the constructor instead.
      input_img = gr.Image(label="Input", type="numpy").style(height=480, width=480)
    /tmp/ipykernel_1187339/1907223323.py:47: GradioDeprecationWarning: The `style` method is deprecated. Please set these arguments in the constructor instead.
      output_img = gr.Image(label="Selected Segment", type="numpy").style(height=480, width=480)


.. parsed-literal::

    Running on local URL:  http://127.0.0.1:7862
    
    To create a public link, set `share=True` in `launch()`.



.. raw:: html

    <div><iframe src="http://127.0.0.1:7862/" width="100%" height="500" allow="autoplay; camera; microphone; clipboard-read; clipboard-write;" frameborder="0" allowfullscreen></iframe></div>


Run OpenVINO model in automatic mask generation mode `⇑ <#top>`__
###############################################################################################################################


Since SAM can efficiently process prompts, masks for the entire image
can be generated by sampling a large number of prompts over an image.
``automatic_mask_generation`` function implements this capability. It
works by sampling single-point input prompts in a grid over the image,
from each of which SAM can predict multiple masks. Then, masks are
filtered for quality and deduplicated using non-maximal suppression.
Additional options allow for further improvement of mask quality and
quantity, such as running prediction on multiple crops of the image or
postprocessing masks to remove small disconnected regions and holes.

.. code:: ipython3

    from segment_anything.utils.amg import (
        MaskData, 
        generate_crop_boxes, 
        uncrop_boxes_xyxy, 
        uncrop_masks, 
        uncrop_points, 
        calculate_stability_score, 
        rle_to_mask, 
        batched_mask_to_box, 
        mask_to_rle_pytorch, 
        is_box_near_crop_edge,
        batch_iterator,
        remove_small_regions,
        build_all_layer_point_grids,
        box_xyxy_to_xywh,
        area_from_rle
    )
    from torchvision.ops.boxes import batched_nms, box_area
    from typing import Tuple, List, Dict, Any

.. code:: ipython3

    def process_batch(
        image_embedding: np.ndarray,
        points: np.ndarray,
        im_size: Tuple[int, ...],
        crop_box: List[int],
        orig_size: Tuple[int, ...],
        iou_thresh,
        mask_threshold,
        stability_score_offset,
        stability_score_thresh
    ) -> MaskData:
        orig_h, orig_w = orig_size
    
        # Run model on this batch
        transformed_points = resizer.apply_coords(points, im_size)
        in_points = transformed_points
        in_labels = np.ones(in_points.shape[0], dtype=int)
    
        inputs = {
            "image_embeddings": image_embedding,
            "point_coords": in_points[:, None, :],
            "point_labels": in_labels[:, None],
        }
        res = ov_predictor(inputs)
        masks = postprocess_masks(res[ov_predictor.output(0)], orig_size)
        masks = torch.from_numpy(masks)
        iou_preds = torch.from_numpy(res[ov_predictor.output(1)])
    
        # Serialize predictions and store in MaskData
        data = MaskData(
            masks=masks.flatten(0, 1),
            iou_preds=iou_preds.flatten(0, 1),
            points=torch.as_tensor(points.repeat(masks.shape[1], axis=0)),
        )
        del masks
    
        # Filter by predicted IoU
        if iou_thresh > 0.0:
            keep_mask = data["iou_preds"] > iou_thresh
            data.filter(keep_mask)
    
        # Calculate stability score
        data["stability_score"] = calculate_stability_score(
            data["masks"], mask_threshold, stability_score_offset
        )
        if stability_score_thresh > 0.0:
            keep_mask = data["stability_score"] >= stability_score_thresh
            data.filter(keep_mask)
    
        # Threshold masks and calculate boxes
        data["masks"] = data["masks"] > mask_threshold
        data["boxes"] = batched_mask_to_box(data["masks"])
    
        # Filter boxes that touch crop boundaries
        keep_mask = ~is_box_near_crop_edge(data["boxes"], crop_box, [0, 0, orig_w, orig_h])
        if not torch.all(keep_mask):
            data.filter(keep_mask)
    
        # Compress to RLE
        data["masks"] = uncrop_masks(data["masks"], crop_box, orig_h, orig_w)
        data["rles"] = mask_to_rle_pytorch(data["masks"])
        del data["masks"]
    
        return data

.. code:: ipython3

    def process_crop(
        image: np.ndarray,
        point_grids,
        crop_box: List[int],
        crop_layer_idx: int,
        orig_size: Tuple[int, ...],
        box_nms_thresh:float = 0.7,
        mask_threshold:float = 0.0,
        points_per_batch: int = 64,
        pred_iou_thresh: float = 0.88,
        stability_score_thresh: float = 0.95,
        stability_score_offset: float = 1.0,
    ) -> MaskData:
        # Crop the image and calculate embeddings
        x0, y0, x1, y1 = crop_box
        cropped_im = image[y0:y1, x0:x1, :]
        cropped_im_size = cropped_im.shape[:2]
        preprocessed_cropped_im = preprocess_image(cropped_im)
        crop_embeddings = ov_encoder(preprocessed_cropped_im)[ov_encoder.output(0)]
    
        # Get points for this crop
        points_scale = np.array(cropped_im_size)[None, ::-1]
        points_for_image = point_grids[crop_layer_idx] * points_scale
    
        # Generate masks for this crop in batches
        data = MaskData()
        for (points,) in batch_iterator(points_per_batch, points_for_image):
            batch_data = process_batch(crop_embeddings, points, cropped_im_size, crop_box, orig_size, pred_iou_thresh, mask_threshold, stability_score_offset, stability_score_thresh)
            data.cat(batch_data)
            del batch_data
    
        # Remove duplicates within this crop.
        keep_by_nms = batched_nms(
            data["boxes"].float(),
            data["iou_preds"],
            torch.zeros(len(data["boxes"])),  # categories
            iou_threshold=box_nms_thresh,
        )
        data.filter(keep_by_nms)
    
        # Return to the original image frame
        data["boxes"] = uncrop_boxes_xyxy(data["boxes"], crop_box)
        data["points"] = uncrop_points(data["points"], crop_box)
        data["crop_boxes"] = torch.tensor([crop_box for _ in range(len(data["rles"]))])
    
        return data

.. code:: ipython3

    def generate_masks(image: np.ndarray, point_grids, crop_n_layers, crop_overlap_ratio, crop_nms_thresh) -> MaskData:
        orig_size = image.shape[:2]
        crop_boxes, layer_idxs = generate_crop_boxes(
            orig_size, crop_n_layers, crop_overlap_ratio
        )
    
        # Iterate over image crops
        data = MaskData()
        for crop_box, layer_idx in zip(crop_boxes, layer_idxs):
            crop_data = process_crop(image, point_grids, crop_box, layer_idx, orig_size)
            data.cat(crop_data)
    
        # Remove duplicate masks between crops
        if len(crop_boxes) > 1:
            # Prefer masks from smaller crops
            scores = 1 / box_area(data["crop_boxes"])
            scores = scores.to(data["boxes"].device)
            keep_by_nms = batched_nms(
                data["boxes"].float(),
                scores,
                torch.zeros(len(data["boxes"])),  # categories
                iou_threshold=crop_nms_thresh,
            )
            data.filter(keep_by_nms)
    
        data.to_numpy()
        return data

.. code:: ipython3

    def postprocess_small_regions(mask_data: MaskData, min_area: int, nms_thresh: float) -> MaskData:
        """
        Removes small disconnected regions and holes in masks, then reruns
        box NMS to remove any new duplicates.
    
        Edits mask_data in place.
    
        Requires open-cv as a dependency.
        """
        if len(mask_data["rles"]) == 0:
            return mask_data
    
        # Filter small disconnected regions and holes
        new_masks = []
        scores = []
        for rle in mask_data["rles"]:
            mask = rle_to_mask(rle)
    
            mask, changed = remove_small_regions(mask, min_area, mode="holes")
            unchanged = not changed
            mask, changed = remove_small_regions(mask, min_area, mode="islands")
            unchanged = unchanged and not changed
    
            new_masks.append(torch.as_tensor(mask).unsqueeze(0))
            # Give score=0 to changed masks and score=1 to unchanged masks
            # so NMS will prefer ones that didn't need postprocessing
            scores.append(float(unchanged))
    
        # Recalculate boxes and remove any new duplicates
        masks = torch.cat(new_masks, dim=0)
        boxes = batched_mask_to_box(masks)
        keep_by_nms = batched_nms(
            boxes.float(),
            torch.as_tensor(scores),
            torch.zeros(len(boxes)),  # categories
            iou_threshold=nms_thresh,
        )
    
        # Only recalculate RLEs for masks that have changed
        for i_mask in keep_by_nms:
            if scores[i_mask] == 0.0:
                mask_torch = masks[i_mask].unsqueeze(0)
                mask_data["rles"][i_mask] = mask_to_rle_pytorch(mask_torch)[0]
                # update res directly
                mask_data["boxes"][i_mask] = boxes[i_mask]
        mask_data.filter(keep_by_nms)
    
        return mask_data

There are several tunable parameters in automatic mask generation that
control how densely points are sampled and what the thresholds are for
removing low quality or duplicate masks. Additionally, generation can be
automatically run on crops of the image to get improved performance on
smaller objects, and post-processing can remove stray pixels and holes

.. code:: ipython3

    def automatic_mask_generation(
        image: np.ndarray, min_mask_region_area: int = 0, points_per_side: int = 32, crop_n_layers: int = 0, crop_n_points_downscale_factor: int = 1, crop_overlap_ratio: float = 512 / 1500, box_nms_thresh: float = 0.7, crop_nms_thresh: float = 0.7
    ) -> List[Dict[str, Any]]:
        """
        Generates masks for the given image.
    
        Arguments:
          image (np.ndarray): The image to generate masks for, in HWC uint8 format.
    
        Returns:
           list(dict(str, any)): A list over records for masks. Each record is
             a dict containing the following keys:
               segmentation (dict(str, any) or np.ndarray): The mask. If
                 output_mode='binary_mask', is an array of shape HW. Otherwise,
                 is a dictionary containing the RLE.
               bbox (list(float)): The box around the mask, in XYWH format.
               area (int): The area in pixels of the mask.
               predicted_iou (float): The model's own prediction of the mask's
                 quality. This is filtered by the pred_iou_thresh parameter.
               point_coords (list(list(float))): The point coordinates input
                 to the model to generate this mask.
               stability_score (float): A measure of the mask's quality. This
                 is filtered on using the stability_score_thresh parameter.
               crop_box (list(float)): The crop of the image used to generate
                 the mask, given in XYWH format.
        """
        point_grids = build_all_layer_point_grids(
            points_per_side,
            crop_n_layers,
            crop_n_points_downscale_factor,
        )
        mask_data = generate_masks(
            image, point_grids, crop_n_layers, crop_overlap_ratio, crop_nms_thresh)
    
        # Filter small disconnected regions and holes in masks
        if min_mask_region_area > 0:
            mask_data = postprocess_small_regions(
                mask_data,
                min_mask_region_area,
                max(box_nms_thresh, crop_nms_thresh),
            )
    
        mask_data["segmentations"] = [
            rle_to_mask(rle) for rle in mask_data["rles"]]
    
        # Write mask records
        curr_anns = []
        for idx in range(len(mask_data["segmentations"])):
            ann = {
                "segmentation": mask_data["segmentations"][idx],
                "area": area_from_rle(mask_data["rles"][idx]),
                "bbox": box_xyxy_to_xywh(mask_data["boxes"][idx]).tolist(),
                "predicted_iou": mask_data["iou_preds"][idx].item(),
                "point_coords": [mask_data["points"][idx].tolist()],
                "stability_score": mask_data["stability_score"][idx].item(),
                "crop_box": box_xyxy_to_xywh(mask_data["crop_boxes"][idx]).tolist(),
            }
            curr_anns.append(ann)
    
        return curr_anns

.. code:: ipython3

    prediction = automatic_mask_generation(image)

``automatic_mask_generation`` returns a list over masks, where each mask
is a dictionary containing various data about the mask. These keys are:

-  ``segmentation`` : the mask
-  ``area`` : the area of the mask in pixels
-  ``bbox`` : the boundary box of the mask in XYWH format
-  ``predicted_iou`` : the model’s own prediction for the quality of the
   mask
-  ``point_coords`` : the sampled input point that generated this mask
-  ``stability_score`` : an additional measure of mask quality
-  ``crop_box`` : the crop of the image used to generate this mask in
   XYWH format

.. code:: ipython3

    print(f"Number of detected masks: {len(prediction)}")
    print(f"Annotation keys: {prediction[0].keys()}")


.. parsed-literal::

    Number of detected masks: 48
    Annotation keys: dict_keys(['segmentation', 'area', 'bbox', 'predicted_iou', 'point_coords', 'stability_score', 'crop_box'])


.. code:: ipython3

    from tqdm.notebook import tqdm
    
    def draw_anns(image, anns):
        if len(anns) == 0:
            return
        segments_image = image.copy()
        sorted_anns = sorted(anns, key=(lambda x: x['area']), reverse=True)
        for ann in tqdm(sorted_anns):
            mask = ann["segmentation"]
            mask_color = np.random.randint(0, 255, size=(1, 1, 3)).astype(np.uint8)
            segments_image[mask] = mask_color
        return cv2.addWeighted(image.astype(np.float32), 0.7, segments_image.astype(np.float32), 0.3, 0.0)

.. code:: ipython3

    import PIL
    
    out = draw_anns(image, prediction)
    cv2.imwrite("result.png", out[:, :, ::-1])
    
    PIL.Image.open("result.png")



.. parsed-literal::

      0%|          | 0/48 [00:00<?, ?it/s]




.. image:: 237-segment-anything-with-output_files/237-segment-anything-with-output_68_1.png



Optimize encoder using NNCF Post-training Quantization API `⇑ <#top>`__
###############################################################################################################################


`NNCF <https://github.com/openvinotoolkit/nncf>`__ provides a suite of
advanced algorithms for Neural Networks inference optimization in
OpenVINO with minimal accuracy drop.

Since encoder costing much more time than other parts in SAM inference
pipeline, we will use 8-bit quantization in post-training mode (without
the fine-tuning pipeline) to optimize encoder of SAM.

The optimization process contains the following steps:

1. Create a Dataset for quantization.
2. Run ``nncf.quantize`` for getting an optimized model.
3. Serialize OpenVINO IR model, using the ``openvino.runtime.serialize``
   function.

Prepare a calibration dataset `⇑ <#top>`__
+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++


Download COCO dataset. Since the dataset is used to calibrate the
model’s parameter instead of fine-tuning it, we don’t need to download
the label files.

.. code:: ipython3

    from zipfile import ZipFile
    
    DATA_URL = "https://ultralytics.com/assets/coco128.zip"
    OUT_DIR = Path('.')
    
    download_file(DATA_URL, directory=OUT_DIR, show_progress=True)
    
    if not (OUT_DIR / "coco128/images/train2017").exists():
        with ZipFile('coco128.zip' , "r") as zip_ref:
            zip_ref.extractall(OUT_DIR)


.. parsed-literal::

    'coco128.zip' already exists.


Create an instance of the ``nncf.Dataset`` class that represents the
calibration dataset. For PyTorch, we can pass an instance of the
``torch.utils.data.DataLoader`` object.

.. code:: ipython3

    import torch.utils.data as data
    
    class COCOLoader(data.Dataset):
        def __init__(self, images_path):
            self.images = list(Path(images_path).iterdir())
    
        def __getitem__(self, index):
            image_path = self.images[index]
            image = cv2.imread(str(image_path))
            image = cv2.cvtColor(image, cv2.COLOR_BGR2RGB)
            return image
        
        def __len__(self):
            return len(self.images)
        
    coco_dataset = COCOLoader(OUT_DIR / 'coco128/images/train2017')
    calibration_loader = torch.utils.data.DataLoader(coco_dataset)

The transformation function is a function that takes a sample from the
dataset and returns data that can be passed to the model for inference.

.. code:: ipython3

    import nncf
    
    def transform_fn(image_data):
        """
        Quantization transform function. Extracts and preprocess input data from dataloader item for quantization.
        Parameters:
            image_data: image data produced by DataLoader during iteration
        Returns:
            input_tensor: input data in Dict format for ONNX model quantization
        """
        image = image_data.numpy()
        processed_image = preprocess_image(np.squeeze(image))
        return processed_image
    
    calibration_dataset = nncf.Dataset(calibration_loader, transform_fn)


.. parsed-literal::

    INFO:nncf:NNCF initialized successfully. Supported frameworks detected: torch, tensorflow, onnx, openvino


Run quantization and serialize OpenVINO IR model `⇑ <#top>`__
+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++


The ``nncf.quantize`` function provides an interface for model
quantization. It requires an instance of the OpenVINO Model and
quantization dataset. It is available for models in the following
frameworks: ``PyTorch``, ``TensorFlow 2.x``, ``ONNX``, and
``OpenVINO IR``.

Optionally, some additional parameters for the configuration
quantization process (number of samples for quantization, preset, model
type, etc.) can be provided. ``model_type`` can be used to specify
quantization scheme required for specific type of the model. For
example, Transformer models such as SAM require a special quantization
scheme to preserve accuracy after quantization. To achieve a better
result, we will use a ``mixed`` quantization preset. It provides
symmetric quantization of weights and asymmetric quantization of
activations.

.. note::

   Model post-training quantization is time-consuming process.
   Be patient, it can take several minutes depending on your hardware.

.. code:: ipython3

    # Load FP32 ONNX model
    model = core.read_model(onnx_encoder_path)
    quantized_model = nncf.quantize(model,
                                    calibration_dataset,
                                    model_type=nncf.parameters.ModelType.TRANSFORMER,
                                    preset=nncf.common.quantization.structs.QuantizationPreset.MIXED, subset_size=128)
    print("model quantization finished")


.. parsed-literal::

    INFO:nncf:709 ignored nodes was found by types in the NNCFGraph
    INFO:nncf:24 ignored nodes was found by name in the NNCFGraph
    INFO:nncf:Not adding activation input quantizer for operation: 6 /Add
    INFO:nncf:Not adding activation input quantizer for operation: 9 /blocks.0/norm1/ReduceMean
    INFO:nncf:Not adding activation input quantizer for operation: 10 /blocks.0/norm1/Sub
    INFO:nncf:Not adding activation input quantizer for operation: 16 /blocks.0/norm1/Pow
    INFO:nncf:Not adding activation input quantizer for operation: 24 /blocks.0/norm1/ReduceMean_1
    INFO:nncf:Not adding activation input quantizer for operation: 34 /blocks.0/norm1/Add
    INFO:nncf:Not adding activation input quantizer for operation: 45 /blocks.0/norm1/Sqrt
    INFO:nncf:Not adding activation input quantizer for operation: 15 /blocks.0/norm1/Div
    INFO:nncf:Not adding activation input quantizer for operation: 23 /blocks.0/norm1/Mul
    33 /blocks.0/norm1/Add_1
    
    INFO:nncf:Not adding activation input quantizer for operation: 556 /blocks.0/attn/Squeeze
    INFO:nncf:Not adding activation input quantizer for operation: 557 /blocks.0/attn/Squeeze_1
    INFO:nncf:Not adding activation input quantizer for operation: 558 /blocks.0/attn/Squeeze_2
    INFO:nncf:Not adding activation input quantizer for operation: 633 /blocks.0/attn/Mul_2
    INFO:nncf:Not adding activation input quantizer for operation: 472 /blocks.0/attn/Add_2
    INFO:nncf:Not adding activation input quantizer for operation: 552 /blocks.0/attn/Add_3
    INFO:nncf:Not adding activation input quantizer for operation: 551 /blocks.0/attn/Softmax
    INFO:nncf:Not adding activation input quantizer for operation: 631 /blocks.0/attn/MatMul_1
    INFO:nncf:Not adding activation input quantizer for operation: 8 /blocks.0/Add_2
    INFO:nncf:Not adding activation input quantizer for operation: 13 /blocks.0/norm2/ReduceMean
    INFO:nncf:Not adding activation input quantizer for operation: 14 /blocks.0/norm2/Sub
    INFO:nncf:Not adding activation input quantizer for operation: 22 /blocks.0/norm2/Pow
    INFO:nncf:Not adding activation input quantizer for operation: 32 /blocks.0/norm2/ReduceMean_1
    INFO:nncf:Not adding activation input quantizer for operation: 43 /blocks.0/norm2/Add
    INFO:nncf:Not adding activation input quantizer for operation: 56 /blocks.0/norm2/Sqrt
    INFO:nncf:Not adding activation input quantizer for operation: 21 /blocks.0/norm2/Div
    INFO:nncf:Not adding activation input quantizer for operation: 31 /blocks.0/norm2/Mul
    42 /blocks.0/norm2/Add_1
    
    INFO:nncf:Not adding activation input quantizer for operation: 91 /blocks.0/mlp/act/Div
    INFO:nncf:Not adding activation input quantizer for operation: 154 /blocks.0/mlp/act/Add
    INFO:nncf:Not adding activation input quantizer for operation: 92 /blocks.0/mlp/act/Mul
    INFO:nncf:Not adding activation input quantizer for operation: 120 /blocks.0/mlp/act/Mul_1
    INFO:nncf:Not adding activation input quantizer for operation: 12 /blocks.0/Add_3
    INFO:nncf:Not adding activation input quantizer for operation: 19 /blocks.1/norm1/ReduceMean
    INFO:nncf:Not adding activation input quantizer for operation: 20 /blocks.1/norm1/Sub
    INFO:nncf:Not adding activation input quantizer for operation: 30 /blocks.1/norm1/Pow
    INFO:nncf:Not adding activation input quantizer for operation: 41 /blocks.1/norm1/ReduceMean_1
    INFO:nncf:Not adding activation input quantizer for operation: 54 /blocks.1/norm1/Add
    INFO:nncf:Not adding activation input quantizer for operation: 72 /blocks.1/norm1/Sqrt
    INFO:nncf:Not adding activation input quantizer for operation: 29 /blocks.1/norm1/Div
    INFO:nncf:Not adding activation input quantizer for operation: 40 /blocks.1/norm1/Mul
    53 /blocks.1/norm1/Add_1
    
    INFO:nncf:Not adding activation input quantizer for operation: 731 /blocks.1/attn/Squeeze
    INFO:nncf:Not adding activation input quantizer for operation: 732 /blocks.1/attn/Squeeze_1
    INFO:nncf:Not adding activation input quantizer for operation: 733 /blocks.1/attn/Squeeze_2
    INFO:nncf:Not adding activation input quantizer for operation: 820 /blocks.1/attn/Mul_2
    INFO:nncf:Not adding activation input quantizer for operation: 616 /blocks.1/attn/Add_2
    INFO:nncf:Not adding activation input quantizer for operation: 727 /blocks.1/attn/Add_3
    INFO:nncf:Not adding activation input quantizer for operation: 726 /blocks.1/attn/Softmax
    INFO:nncf:Not adding activation input quantizer for operation: 818 /blocks.1/attn/MatMul_1
    INFO:nncf:Not adding activation input quantizer for operation: 18 /blocks.1/Add_2
    INFO:nncf:Not adding activation input quantizer for operation: 27 /blocks.1/norm2/ReduceMean
    INFO:nncf:Not adding activation input quantizer for operation: 28 /blocks.1/norm2/Sub
    INFO:nncf:Not adding activation input quantizer for operation: 39 /blocks.1/norm2/Pow
    INFO:nncf:Not adding activation input quantizer for operation: 52 /blocks.1/norm2/ReduceMean_1
    INFO:nncf:Not adding activation input quantizer for operation: 66 /blocks.1/norm2/Add
    INFO:nncf:Not adding activation input quantizer for operation: 85 /blocks.1/norm2/Sqrt
    INFO:nncf:Not adding activation input quantizer for operation: 38 /blocks.1/norm2/Div
    INFO:nncf:Not adding activation input quantizer for operation: 51 /blocks.1/norm2/Mul
    65 /blocks.1/norm2/Add_1
    
    INFO:nncf:Not adding activation input quantizer for operation: 140 /blocks.1/mlp/act/Div
    INFO:nncf:Not adding activation input quantizer for operation: 272 /blocks.1/mlp/act/Add
    INFO:nncf:Not adding activation input quantizer for operation: 141 /blocks.1/mlp/act/Mul
    INFO:nncf:Not adding activation input quantizer for operation: 201 /blocks.1/mlp/act/Mul_1
    INFO:nncf:Not adding activation input quantizer for operation: 26 /blocks.1/Add_3
    INFO:nncf:Not adding activation input quantizer for operation: 36 /blocks.2/norm1/ReduceMean
    INFO:nncf:Not adding activation input quantizer for operation: 37 /blocks.2/norm1/Sub
    INFO:nncf:Not adding activation input quantizer for operation: 50 /blocks.2/norm1/Pow
    INFO:nncf:Not adding activation input quantizer for operation: 64 /blocks.2/norm1/ReduceMean_1
    INFO:nncf:Not adding activation input quantizer for operation: 83 /blocks.2/norm1/Add
    INFO:nncf:Not adding activation input quantizer for operation: 107 /blocks.2/norm1/Sqrt
    INFO:nncf:Not adding activation input quantizer for operation: 49 /blocks.2/norm1/Div
    INFO:nncf:Not adding activation input quantizer for operation: 63 /blocks.2/norm1/Mul
    82 /blocks.2/norm1/Add_1
    
    INFO:nncf:Not adding activation input quantizer for operation: 525 /blocks.2/attn/Squeeze
    INFO:nncf:Not adding activation input quantizer for operation: 526 /blocks.2/attn/Squeeze_1
    INFO:nncf:Not adding activation input quantizer for operation: 527 /blocks.2/attn/Squeeze_2
    INFO:nncf:Not adding activation input quantizer for operation: 605 /blocks.2/attn/Mul_2
    INFO:nncf:Not adding activation input quantizer for operation: 436 /blocks.2/attn/Add_2
    INFO:nncf:Not adding activation input quantizer for operation: 521 /blocks.2/attn/Add_3
    INFO:nncf:Not adding activation input quantizer for operation: 520 /blocks.2/attn/Softmax
    INFO:nncf:Not adding activation input quantizer for operation: 603 /blocks.2/attn/MatMul_1
    INFO:nncf:Not adding activation input quantizer for operation: 35 /blocks.2/Add
    INFO:nncf:Not adding activation input quantizer for operation: 47 /blocks.2/norm2/ReduceMean
    INFO:nncf:Not adding activation input quantizer for operation: 48 /blocks.2/norm2/Sub
    INFO:nncf:Not adding activation input quantizer for operation: 62 /blocks.2/norm2/Pow
    INFO:nncf:Not adding activation input quantizer for operation: 81 /blocks.2/norm2/ReduceMean_1
    INFO:nncf:Not adding activation input quantizer for operation: 102 /blocks.2/norm2/Add
    INFO:nncf:Not adding activation input quantizer for operation: 135 /blocks.2/norm2/Sqrt
    INFO:nncf:Not adding activation input quantizer for operation: 61 /blocks.2/norm2/Div
    INFO:nncf:Not adding activation input quantizer for operation: 80 /blocks.2/norm2/Mul
    101 /blocks.2/norm2/Add_1
    
    INFO:nncf:Not adding activation input quantizer for operation: 253 /blocks.2/mlp/act/Div
    INFO:nncf:Not adding activation input quantizer for operation: 427 /blocks.2/mlp/act/Add
    INFO:nncf:Not adding activation input quantizer for operation: 254 /blocks.2/mlp/act/Mul
    INFO:nncf:Not adding activation input quantizer for operation: 330 /blocks.2/mlp/act/Mul_1
    INFO:nncf:Not adding activation input quantizer for operation: 46 /blocks.2/Add_1
    INFO:nncf:Not adding activation input quantizer for operation: 59 /blocks.3/norm1/ReduceMean
    INFO:nncf:Not adding activation input quantizer for operation: 60 /blocks.3/norm1/Sub
    INFO:nncf:Not adding activation input quantizer for operation: 79 /blocks.3/norm1/Pow
    INFO:nncf:Not adding activation input quantizer for operation: 100 /blocks.3/norm1/ReduceMean_1
    INFO:nncf:Not adding activation input quantizer for operation: 133 /blocks.3/norm1/Add
    INFO:nncf:Not adding activation input quantizer for operation: 174 /blocks.3/norm1/Sqrt
    INFO:nncf:Not adding activation input quantizer for operation: 78 /blocks.3/norm1/Div
    INFO:nncf:Not adding activation input quantizer for operation: 99 /blocks.3/norm1/Mul
    132 /blocks.3/norm1/Add_1
    
    INFO:nncf:Not adding activation input quantizer for operation: 1110 /blocks.3/attn/Squeeze
    INFO:nncf:Not adding activation input quantizer for operation: 1111 /blocks.3/attn/Squeeze_1
    INFO:nncf:Not adding activation input quantizer for operation: 1112 /blocks.3/attn/Squeeze_2
    INFO:nncf:Not adding activation input quantizer for operation: 1192 /blocks.3/attn/Mul_2
    INFO:nncf:Not adding activation input quantizer for operation: 1013 /blocks.3/attn/Add_2
    INFO:nncf:Not adding activation input quantizer for operation: 1106 /blocks.3/attn/Add_3
    INFO:nncf:Not adding activation input quantizer for operation: 1105 /blocks.3/attn/Softmax
    INFO:nncf:Not adding activation input quantizer for operation: 1190 /blocks.3/attn/MatMul_1
    INFO:nncf:Not adding activation input quantizer for operation: 58 /blocks.3/Add_2
    INFO:nncf:Not adding activation input quantizer for operation: 76 /blocks.3/norm2/ReduceMean
    INFO:nncf:Not adding activation input quantizer for operation: 77 /blocks.3/norm2/Sub
    INFO:nncf:Not adding activation input quantizer for operation: 98 /blocks.3/norm2/Pow
    INFO:nncf:Not adding activation input quantizer for operation: 131 /blocks.3/norm2/ReduceMean_1
    INFO:nncf:Not adding activation input quantizer for operation: 168 /blocks.3/norm2/Add
    INFO:nncf:Not adding activation input quantizer for operation: 247 /blocks.3/norm2/Sqrt
    INFO:nncf:Not adding activation input quantizer for operation: 97 /blocks.3/norm2/Div
    INFO:nncf:Not adding activation input quantizer for operation: 130 /blocks.3/norm2/Mul
    167 /blocks.3/norm2/Add_1
    
    INFO:nncf:Not adding activation input quantizer for operation: 413 /blocks.3/mlp/act/Div
    INFO:nncf:Not adding activation input quantizer for operation: 588 /blocks.3/mlp/act/Add
    INFO:nncf:Not adding activation input quantizer for operation: 414 /blocks.3/mlp/act/Mul
    INFO:nncf:Not adding activation input quantizer for operation: 506 /blocks.3/mlp/act/Mul_1
    INFO:nncf:Not adding activation input quantizer for operation: 75 /blocks.3/Add_3
    INFO:nncf:Not adding activation input quantizer for operation: 95 /blocks.4/norm1/ReduceMean
    INFO:nncf:Not adding activation input quantizer for operation: 96 /blocks.4/norm1/Sub
    INFO:nncf:Not adding activation input quantizer for operation: 129 /blocks.4/norm1/Pow
    INFO:nncf:Not adding activation input quantizer for operation: 166 /blocks.4/norm1/ReduceMean_1
    INFO:nncf:Not adding activation input quantizer for operation: 245 /blocks.4/norm1/Add
    INFO:nncf:Not adding activation input quantizer for operation: 317 /blocks.4/norm1/Sqrt
    INFO:nncf:Not adding activation input quantizer for operation: 128 /blocks.4/norm1/Div
    INFO:nncf:Not adding activation input quantizer for operation: 165 /blocks.4/norm1/Mul
    244 /blocks.4/norm1/Add_1
    
    INFO:nncf:Not adding activation input quantizer for operation: 1294 /blocks.4/attn/Squeeze
    INFO:nncf:Not adding activation input quantizer for operation: 1295 /blocks.4/attn/Squeeze_1
    INFO:nncf:Not adding activation input quantizer for operation: 1296 /blocks.4/attn/Squeeze_2
    INFO:nncf:Not adding activation input quantizer for operation: 1384 /blocks.4/attn/Mul_2
    INFO:nncf:Not adding activation input quantizer for operation: 1176 /blocks.4/attn/Add_2
    INFO:nncf:Not adding activation input quantizer for operation: 1290 /blocks.4/attn/Add_3
    INFO:nncf:Not adding activation input quantizer for operation: 1289 /blocks.4/attn/Softmax
    INFO:nncf:Not adding activation input quantizer for operation: 1382 /blocks.4/attn/MatMul_1
    INFO:nncf:Not adding activation input quantizer for operation: 94 /blocks.4/Add_2
    INFO:nncf:Not adding activation input quantizer for operation: 126 /blocks.4/norm2/ReduceMean
    INFO:nncf:Not adding activation input quantizer for operation: 127 /blocks.4/norm2/Sub
    INFO:nncf:Not adding activation input quantizer for operation: 164 /blocks.4/norm2/Pow
    INFO:nncf:Not adding activation input quantizer for operation: 243 /blocks.4/norm2/ReduceMean_1
    INFO:nncf:Not adding activation input quantizer for operation: 311 /blocks.4/norm2/Add
    INFO:nncf:Not adding activation input quantizer for operation: 407 /blocks.4/norm2/Sqrt
    INFO:nncf:Not adding activation input quantizer for operation: 163 /blocks.4/norm2/Div
    INFO:nncf:Not adding activation input quantizer for operation: 242 /blocks.4/norm2/Mul
    310 /blocks.4/norm2/Add_1
    
    INFO:nncf:Not adding activation input quantizer for operation: 574 /blocks.4/mlp/act/Div
    INFO:nncf:Not adding activation input quantizer for operation: 777 /blocks.4/mlp/act/Add
    INFO:nncf:Not adding activation input quantizer for operation: 575 /blocks.4/mlp/act/Mul
    INFO:nncf:Not adding activation input quantizer for operation: 678 /blocks.4/mlp/act/Mul_1
    INFO:nncf:Not adding activation input quantizer for operation: 125 /blocks.4/Add_3
    INFO:nncf:Not adding activation input quantizer for operation: 161 /blocks.5/norm1/ReduceMean
    INFO:nncf:Not adding activation input quantizer for operation: 162 /blocks.5/norm1/Sub
    INFO:nncf:Not adding activation input quantizer for operation: 241 /blocks.5/norm1/Pow
    INFO:nncf:Not adding activation input quantizer for operation: 309 /blocks.5/norm1/ReduceMean_1
    INFO:nncf:Not adding activation input quantizer for operation: 405 /blocks.5/norm1/Add
    INFO:nncf:Not adding activation input quantizer for operation: 493 /blocks.5/norm1/Sqrt
    INFO:nncf:Not adding activation input quantizer for operation: 240 /blocks.5/norm1/Div
    INFO:nncf:Not adding activation input quantizer for operation: 308 /blocks.5/norm1/Mul
    404 /blocks.5/norm1/Add_1
    
    INFO:nncf:Not adding activation input quantizer for operation: 1079 /blocks.5/attn/Squeeze
    INFO:nncf:Not adding activation input quantizer for operation: 1080 /blocks.5/attn/Squeeze_1
    INFO:nncf:Not adding activation input quantizer for operation: 1081 /blocks.5/attn/Squeeze_2
    INFO:nncf:Not adding activation input quantizer for operation: 1165 /blocks.5/attn/Mul_2
    INFO:nncf:Not adding activation input quantizer for operation: 977 /blocks.5/attn/Add_2
    INFO:nncf:Not adding activation input quantizer for operation: 1075 /blocks.5/attn/Add_3
    INFO:nncf:Not adding activation input quantizer for operation: 1074 /blocks.5/attn/Softmax
    INFO:nncf:Not adding activation input quantizer for operation: 1163 /blocks.5/attn/MatMul_1
    INFO:nncf:Not adding activation input quantizer for operation: 160 /blocks.5/Add
    INFO:nncf:Not adding activation input quantizer for operation: 238 /blocks.5/norm2/ReduceMean
    INFO:nncf:Not adding activation input quantizer for operation: 239 /blocks.5/norm2/Sub
    INFO:nncf:Not adding activation input quantizer for operation: 307 /blocks.5/norm2/Pow
    INFO:nncf:Not adding activation input quantizer for operation: 403 /blocks.5/norm2/ReduceMean_1
    INFO:nncf:Not adding activation input quantizer for operation: 488 /blocks.5/norm2/Add
    INFO:nncf:Not adding activation input quantizer for operation: 569 /blocks.5/norm2/Sqrt
    INFO:nncf:Not adding activation input quantizer for operation: 306 /blocks.5/norm2/Div
    INFO:nncf:Not adding activation input quantizer for operation: 402 /blocks.5/norm2/Mul
    487 /blocks.5/norm2/Add_1
    
    INFO:nncf:Not adding activation input quantizer for operation: 758 /blocks.5/mlp/act/Div
    INFO:nncf:Not adding activation input quantizer for operation: 968 /blocks.5/mlp/act/Add
    INFO:nncf:Not adding activation input quantizer for operation: 759 /blocks.5/mlp/act/Mul
    INFO:nncf:Not adding activation input quantizer for operation: 859 /blocks.5/mlp/act/Mul_1
    INFO:nncf:Not adding activation input quantizer for operation: 237 /blocks.5/Add_1
    INFO:nncf:Not adding activation input quantizer for operation: 304 /blocks.6/norm1/ReduceMean
    INFO:nncf:Not adding activation input quantizer for operation: 305 /blocks.6/norm1/Sub
    INFO:nncf:Not adding activation input quantizer for operation: 401 /blocks.6/norm1/Pow
    INFO:nncf:Not adding activation input quantizer for operation: 486 /blocks.6/norm1/ReduceMean_1
    INFO:nncf:Not adding activation input quantizer for operation: 567 /blocks.6/norm1/Add
    INFO:nncf:Not adding activation input quantizer for operation: 651 /blocks.6/norm1/Sqrt
    INFO:nncf:Not adding activation input quantizer for operation: 400 /blocks.6/norm1/Div
    INFO:nncf:Not adding activation input quantizer for operation: 485 /blocks.6/norm1/Mul
    566 /blocks.6/norm1/Add_1
    
    INFO:nncf:Not adding activation input quantizer for operation: 1661 /blocks.6/attn/Squeeze
    INFO:nncf:Not adding activation input quantizer for operation: 1662 /blocks.6/attn/Squeeze_1
    INFO:nncf:Not adding activation input quantizer for operation: 1663 /blocks.6/attn/Squeeze_2
    INFO:nncf:Not adding activation input quantizer for operation: 1734 /blocks.6/attn/Mul_2
    INFO:nncf:Not adding activation input quantizer for operation: 1571 /blocks.6/attn/Add_2
    INFO:nncf:Not adding activation input quantizer for operation: 1657 /blocks.6/attn/Add_3
    INFO:nncf:Not adding activation input quantizer for operation: 1656 /blocks.6/attn/Softmax
    INFO:nncf:Not adding activation input quantizer for operation: 1732 /blocks.6/attn/MatMul_1
    INFO:nncf:Not adding activation input quantizer for operation: 303 /blocks.6/Add_2
    INFO:nncf:Not adding activation input quantizer for operation: 398 /blocks.6/norm2/ReduceMean
    INFO:nncf:Not adding activation input quantizer for operation: 399 /blocks.6/norm2/Sub
    INFO:nncf:Not adding activation input quantizer for operation: 484 /blocks.6/norm2/Pow
    INFO:nncf:Not adding activation input quantizer for operation: 565 /blocks.6/norm2/ReduceMean_1
    INFO:nncf:Not adding activation input quantizer for operation: 645 /blocks.6/norm2/Add
    INFO:nncf:Not adding activation input quantizer for operation: 752 /blocks.6/norm2/Sqrt
    INFO:nncf:Not adding activation input quantizer for operation: 483 /blocks.6/norm2/Div
    INFO:nncf:Not adding activation input quantizer for operation: 564 /blocks.6/norm2/Mul
    644 /blocks.6/norm2/Add_1
    
    INFO:nncf:Not adding activation input quantizer for operation: 954 /blocks.6/mlp/act/Div
    INFO:nncf:Not adding activation input quantizer for operation: 1148 /blocks.6/mlp/act/Add
    INFO:nncf:Not adding activation input quantizer for operation: 955 /blocks.6/mlp/act/Mul
    INFO:nncf:Not adding activation input quantizer for operation: 1060 /blocks.6/mlp/act/Mul_1
    INFO:nncf:Not adding activation input quantizer for operation: 397 /blocks.6/Add_3
    INFO:nncf:Not adding activation input quantizer for operation: 481 /blocks.7/norm1/ReduceMean
    INFO:nncf:Not adding activation input quantizer for operation: 482 /blocks.7/norm1/Sub
    INFO:nncf:Not adding activation input quantizer for operation: 563 /blocks.7/norm1/Pow
    INFO:nncf:Not adding activation input quantizer for operation: 643 /blocks.7/norm1/ReduceMean_1
    INFO:nncf:Not adding activation input quantizer for operation: 750 /blocks.7/norm1/Add
    INFO:nncf:Not adding activation input quantizer for operation: 846 /blocks.7/norm1/Sqrt
    INFO:nncf:Not adding activation input quantizer for operation: 562 /blocks.7/norm1/Div
    INFO:nncf:Not adding activation input quantizer for operation: 642 /blocks.7/norm1/Mul
    749 /blocks.7/norm1/Add_1
    
    INFO:nncf:Not adding activation input quantizer for operation: 1821 /blocks.7/attn/Squeeze
    INFO:nncf:Not adding activation input quantizer for operation: 1822 /blocks.7/attn/Squeeze_1
    INFO:nncf:Not adding activation input quantizer for operation: 1823 /blocks.7/attn/Squeeze_2
    INFO:nncf:Not adding activation input quantizer for operation: 1897 /blocks.7/attn/Mul_2
    INFO:nncf:Not adding activation input quantizer for operation: 1718 /blocks.7/attn/Add_2
    INFO:nncf:Not adding activation input quantizer for operation: 1817 /blocks.7/attn/Add_3
    INFO:nncf:Not adding activation input quantizer for operation: 1816 /blocks.7/attn/Softmax
    INFO:nncf:Not adding activation input quantizer for operation: 1895 /blocks.7/attn/MatMul_1
    INFO:nncf:Not adding activation input quantizer for operation: 480 /blocks.7/Add_2
    INFO:nncf:Not adding activation input quantizer for operation: 560 /blocks.7/norm2/ReduceMean
    INFO:nncf:Not adding activation input quantizer for operation: 561 /blocks.7/norm2/Sub
    INFO:nncf:Not adding activation input quantizer for operation: 641 /blocks.7/norm2/Pow
    INFO:nncf:Not adding activation input quantizer for operation: 748 /blocks.7/norm2/ReduceMean_1
    INFO:nncf:Not adding activation input quantizer for operation: 840 /blocks.7/norm2/Add
    INFO:nncf:Not adding activation input quantizer for operation: 948 /blocks.7/norm2/Sqrt
    INFO:nncf:Not adding activation input quantizer for operation: 640 /blocks.7/norm2/Div
    INFO:nncf:Not adding activation input quantizer for operation: 747 /blocks.7/norm2/Mul
    839 /blocks.7/norm2/Add_1
    
    INFO:nncf:Not adding activation input quantizer for operation: 1134 /blocks.7/mlp/act/Div
    INFO:nncf:Not adding activation input quantizer for operation: 1341 /blocks.7/mlp/act/Add
    INFO:nncf:Not adding activation input quantizer for operation: 1135 /blocks.7/mlp/act/Mul
    INFO:nncf:Not adding activation input quantizer for operation: 1241 /blocks.7/mlp/act/Mul_1
    INFO:nncf:Not adding activation input quantizer for operation: 559 /blocks.7/Add_3
    INFO:nncf:Not adding activation input quantizer for operation: 638 /blocks.8/norm1/ReduceMean
    INFO:nncf:Not adding activation input quantizer for operation: 639 /blocks.8/norm1/Sub
    INFO:nncf:Not adding activation input quantizer for operation: 746 /blocks.8/norm1/Pow
    INFO:nncf:Not adding activation input quantizer for operation: 838 /blocks.8/norm1/ReduceMean_1
    INFO:nncf:Not adding activation input quantizer for operation: 946 /blocks.8/norm1/Add
    INFO:nncf:Not adding activation input quantizer for operation: 1047 /blocks.8/norm1/Sqrt
    INFO:nncf:Not adding activation input quantizer for operation: 745 /blocks.8/norm1/Div
    INFO:nncf:Not adding activation input quantizer for operation: 837 /blocks.8/norm1/Mul
    945 /blocks.8/norm1/Add_1
    
    INFO:nncf:Not adding activation input quantizer for operation: 1630 /blocks.8/attn/Squeeze
    INFO:nncf:Not adding activation input quantizer for operation: 1631 /blocks.8/attn/Squeeze_1
    INFO:nncf:Not adding activation input quantizer for operation: 1632 /blocks.8/attn/Squeeze_2
    INFO:nncf:Not adding activation input quantizer for operation: 1707 /blocks.8/attn/Mul_2
    INFO:nncf:Not adding activation input quantizer for operation: 1535 /blocks.8/attn/Add_2
    INFO:nncf:Not adding activation input quantizer for operation: 1626 /blocks.8/attn/Add_3
    INFO:nncf:Not adding activation input quantizer for operation: 1625 /blocks.8/attn/Softmax
    INFO:nncf:Not adding activation input quantizer for operation: 1705 /blocks.8/attn/MatMul_1
    INFO:nncf:Not adding activation input quantizer for operation: 637 /blocks.8/Add
    INFO:nncf:Not adding activation input quantizer for operation: 743 /blocks.8/norm2/ReduceMean
    INFO:nncf:Not adding activation input quantizer for operation: 744 /blocks.8/norm2/Sub
    INFO:nncf:Not adding activation input quantizer for operation: 836 /blocks.8/norm2/Pow
    INFO:nncf:Not adding activation input quantizer for operation: 944 /blocks.8/norm2/ReduceMean_1
    INFO:nncf:Not adding activation input quantizer for operation: 1042 /blocks.8/norm2/Add
    INFO:nncf:Not adding activation input quantizer for operation: 1129 /blocks.8/norm2/Sqrt
    INFO:nncf:Not adding activation input quantizer for operation: 835 /blocks.8/norm2/Div
    INFO:nncf:Not adding activation input quantizer for operation: 943 /blocks.8/norm2/Mul
    1041 /blocks.8/norm2/Add_1
    
    INFO:nncf:Not adding activation input quantizer for operation: 1322 /blocks.8/mlp/act/Div
    INFO:nncf:Not adding activation input quantizer for operation: 1526 /blocks.8/mlp/act/Add
    INFO:nncf:Not adding activation input quantizer for operation: 1323 /blocks.8/mlp/act/Mul
    INFO:nncf:Not adding activation input quantizer for operation: 1422 /blocks.8/mlp/act/Mul_1
    INFO:nncf:Not adding activation input quantizer for operation: 742 /blocks.8/Add_1
    INFO:nncf:Not adding activation input quantizer for operation: 833 /blocks.9/norm1/ReduceMean
    INFO:nncf:Not adding activation input quantizer for operation: 834 /blocks.9/norm1/Sub
    INFO:nncf:Not adding activation input quantizer for operation: 942 /blocks.9/norm1/Pow
    INFO:nncf:Not adding activation input quantizer for operation: 1040 /blocks.9/norm1/ReduceMean_1
    INFO:nncf:Not adding activation input quantizer for operation: 1127 /blocks.9/norm1/Add
    INFO:nncf:Not adding activation input quantizer for operation: 1214 /blocks.9/norm1/Sqrt
    INFO:nncf:Not adding activation input quantizer for operation: 941 /blocks.9/norm1/Div
    INFO:nncf:Not adding activation input quantizer for operation: 1039 /blocks.9/norm1/Mul
    1126 /blocks.9/norm1/Add_1
    
    INFO:nncf:Not adding activation input quantizer for operation: 2098 /blocks.9/attn/Squeeze
    INFO:nncf:Not adding activation input quantizer for operation: 2099 /blocks.9/attn/Squeeze_1
    INFO:nncf:Not adding activation input quantizer for operation: 2100 /blocks.9/attn/Squeeze_2
    INFO:nncf:Not adding activation input quantizer for operation: 2137 /blocks.9/attn/Mul_2
    INFO:nncf:Not adding activation input quantizer for operation: 2038 /blocks.9/attn/Add_2
    INFO:nncf:Not adding activation input quantizer for operation: 2094 /blocks.9/attn/Add_3
    INFO:nncf:Not adding activation input quantizer for operation: 2093 /blocks.9/attn/Softmax
    INFO:nncf:Not adding activation input quantizer for operation: 2135 /blocks.9/attn/MatMul_1
    INFO:nncf:Not adding activation input quantizer for operation: 832 /blocks.9/Add_2
    INFO:nncf:Not adding activation input quantizer for operation: 939 /blocks.9/norm2/ReduceMean
    INFO:nncf:Not adding activation input quantizer for operation: 940 /blocks.9/norm2/Sub
    INFO:nncf:Not adding activation input quantizer for operation: 1038 /blocks.9/norm2/Pow
    INFO:nncf:Not adding activation input quantizer for operation: 1125 /blocks.9/norm2/ReduceMean_1
    INFO:nncf:Not adding activation input quantizer for operation: 1208 /blocks.9/norm2/Add
    INFO:nncf:Not adding activation input quantizer for operation: 1316 /blocks.9/norm2/Sqrt
    INFO:nncf:Not adding activation input quantizer for operation: 1037 /blocks.9/norm2/Div
    INFO:nncf:Not adding activation input quantizer for operation: 1124 /blocks.9/norm2/Mul
    1207 /blocks.9/norm2/Add_1
    
    INFO:nncf:Not adding activation input quantizer for operation: 1512 /blocks.9/mlp/act/Div
    INFO:nncf:Not adding activation input quantizer for operation: 1690 /blocks.9/mlp/act/Add
    INFO:nncf:Not adding activation input quantizer for operation: 1513 /blocks.9/mlp/act/Mul
    INFO:nncf:Not adding activation input quantizer for operation: 1611 /blocks.9/mlp/act/Mul_1
    INFO:nncf:Not adding activation input quantizer for operation: 938 /blocks.9/Add_3
    INFO:nncf:Not adding activation input quantizer for operation: 1035 /blocks.10/norm1/ReduceMean
    INFO:nncf:Not adding activation input quantizer for operation: 1036 /blocks.10/norm1/Sub
    INFO:nncf:Not adding activation input quantizer for operation: 1123 /blocks.10/norm1/Pow
    INFO:nncf:Not adding activation input quantizer for operation: 1206 /blocks.10/norm1/ReduceMean_1
    INFO:nncf:Not adding activation input quantizer for operation: 1314 /blocks.10/norm1/Add
    INFO:nncf:Not adding activation input quantizer for operation: 1409 /blocks.10/norm1/Sqrt
    INFO:nncf:Not adding activation input quantizer for operation: 1122 /blocks.10/norm1/Div
    INFO:nncf:Not adding activation input quantizer for operation: 1205 /blocks.10/norm1/Mul
    1313 /blocks.10/norm1/Add_1
    
    INFO:nncf:Not adding activation input quantizer for operation: 2155 /blocks.10/attn/Squeeze
    INFO:nncf:Not adding activation input quantizer for operation: 2156 /blocks.10/attn/Squeeze_1
    INFO:nncf:Not adding activation input quantizer for operation: 2157 /blocks.10/attn/Squeeze_2
    INFO:nncf:Not adding activation input quantizer for operation: 2177 /blocks.10/attn/Mul_2
    INFO:nncf:Not adding activation input quantizer for operation: 2121 /blocks.10/attn/Add_2
    INFO:nncf:Not adding activation input quantizer for operation: 2151 /blocks.10/attn/Add_3
    INFO:nncf:Not adding activation input quantizer for operation: 2150 /blocks.10/attn/Softmax
    INFO:nncf:Not adding activation input quantizer for operation: 2175 /blocks.10/attn/MatMul_1
    INFO:nncf:Not adding activation input quantizer for operation: 1034 /blocks.10/Add_2
    INFO:nncf:Not adding activation input quantizer for operation: 1120 /blocks.10/norm2/ReduceMean
    INFO:nncf:Not adding activation input quantizer for operation: 1121 /blocks.10/norm2/Sub
    INFO:nncf:Not adding activation input quantizer for operation: 1204 /blocks.10/norm2/Pow
    INFO:nncf:Not adding activation input quantizer for operation: 1312 /blocks.10/norm2/ReduceMean_1
    INFO:nncf:Not adding activation input quantizer for operation: 1403 /blocks.10/norm2/Add
    INFO:nncf:Not adding activation input quantizer for operation: 1506 /blocks.10/norm2/Sqrt
    INFO:nncf:Not adding activation input quantizer for operation: 1203 /blocks.10/norm2/Div
    INFO:nncf:Not adding activation input quantizer for operation: 1311 /blocks.10/norm2/Mul
    1402 /blocks.10/norm2/Add_1
    
    INFO:nncf:Not adding activation input quantizer for operation: 1676 /blocks.10/mlp/act/Div
    INFO:nncf:Not adding activation input quantizer for operation: 1854 /blocks.10/mlp/act/Add
    INFO:nncf:Not adding activation input quantizer for operation: 1677 /blocks.10/mlp/act/Mul
    INFO:nncf:Not adding activation input quantizer for operation: 1768 /blocks.10/mlp/act/Mul_1
    INFO:nncf:Not adding activation input quantizer for operation: 1119 /blocks.10/Add_3
    INFO:nncf:Not adding activation input quantizer for operation: 1201 /blocks.11/norm1/ReduceMean
    INFO:nncf:Not adding activation input quantizer for operation: 1202 /blocks.11/norm1/Sub
    INFO:nncf:Not adding activation input quantizer for operation: 1310 /blocks.11/norm1/Pow
    INFO:nncf:Not adding activation input quantizer for operation: 1401 /blocks.11/norm1/ReduceMean_1
    INFO:nncf:Not adding activation input quantizer for operation: 1504 /blocks.11/norm1/Add
    INFO:nncf:Not adding activation input quantizer for operation: 1598 /blocks.11/norm1/Sqrt
    INFO:nncf:Not adding activation input quantizer for operation: 1309 /blocks.11/norm1/Div
    INFO:nncf:Not adding activation input quantizer for operation: 1400 /blocks.11/norm1/Mul
    1503 /blocks.11/norm1/Add_1
    
    INFO:nncf:Not adding activation input quantizer for operation: 2067 /blocks.11/attn/Squeeze
    INFO:nncf:Not adding activation input quantizer for operation: 2068 /blocks.11/attn/Squeeze_1
    INFO:nncf:Not adding activation input quantizer for operation: 2069 /blocks.11/attn/Squeeze_2
    INFO:nncf:Not adding activation input quantizer for operation: 2110 /blocks.11/attn/Mul_2
    INFO:nncf:Not adding activation input quantizer for operation: 2002 /blocks.11/attn/Add_2
    INFO:nncf:Not adding activation input quantizer for operation: 2063 /blocks.11/attn/Add_3
    INFO:nncf:Not adding activation input quantizer for operation: 2062 /blocks.11/attn/Softmax
    INFO:nncf:Not adding activation input quantizer for operation: 2108 /blocks.11/attn/MatMul_1
    INFO:nncf:Not adding activation input quantizer for operation: 1200 /blocks.11/Add
    INFO:nncf:Not adding activation input quantizer for operation: 1307 /blocks.11/norm2/ReduceMean
    INFO:nncf:Not adding activation input quantizer for operation: 1308 /blocks.11/norm2/Sub
    INFO:nncf:Not adding activation input quantizer for operation: 1399 /blocks.11/norm2/Pow
    INFO:nncf:Not adding activation input quantizer for operation: 1502 /blocks.11/norm2/ReduceMean_1
    INFO:nncf:Not adding activation input quantizer for operation: 1593 /blocks.11/norm2/Add
    INFO:nncf:Not adding activation input quantizer for operation: 1671 /blocks.11/norm2/Sqrt
    INFO:nncf:Not adding activation input quantizer for operation: 1398 /blocks.11/norm2/Div
    INFO:nncf:Not adding activation input quantizer for operation: 1501 /blocks.11/norm2/Mul
    1592 /blocks.11/norm2/Add_1
    
    INFO:nncf:Not adding activation input quantizer for operation: 1835 /blocks.11/mlp/act/Div
    INFO:nncf:Not adding activation input quantizer for operation: 1993 /blocks.11/mlp/act/Add
    INFO:nncf:Not adding activation input quantizer for operation: 1836 /blocks.11/mlp/act/Mul
    INFO:nncf:Not adding activation input quantizer for operation: 1913 /blocks.11/mlp/act/Mul_1
    INFO:nncf:Not adding activation input quantizer for operation: 1306 /blocks.11/Add_1
    INFO:nncf:Not adding activation input quantizer for operation: 1590 /neck/neck.1/ReduceMean
    INFO:nncf:Not adding activation input quantizer for operation: 1591 /neck/neck.1/Sub
    INFO:nncf:Not adding activation input quantizer for operation: 1669 /neck/neck.1/Pow
    INFO:nncf:Not adding activation input quantizer for operation: 1741 /neck/neck.1/ReduceMean_1
    INFO:nncf:Not adding activation input quantizer for operation: 1834 /neck/neck.1/Add
    INFO:nncf:Not adding activation input quantizer for operation: 1911 /neck/neck.1/Sqrt
    INFO:nncf:Not adding activation input quantizer for operation: 1668 /neck/neck.1/Div
    INFO:nncf:Not adding activation input quantizer for operation: 1740 /neck/neck.1/Mul
    1833 /neck/neck.1/Add_1
    
    INFO:nncf:Not adding activation input quantizer for operation: 1991 /neck/neck.3/ReduceMean
    INFO:nncf:Not adding activation input quantizer for operation: 1992 /neck/neck.3/Sub
    INFO:nncf:Not adding activation input quantizer for operation: 2058 /neck/neck.3/Pow
    INFO:nncf:Not adding activation input quantizer for operation: 2106 /neck/neck.3/ReduceMean_1
    INFO:nncf:Not adding activation input quantizer for operation: 2144 /neck/neck.3/Add
    INFO:nncf:Not adding activation input quantizer for operation: 2168 /neck/neck.3/Sqrt
    INFO:nncf:Not adding activation input quantizer for operation: 2057 /neck/neck.3/Div
    INFO:nncf:Not adding activation input quantizer for operation: 2105 /neck/neck.3/Mul
    2143 4017
    


.. parsed-literal::

    Statistics collection: 100%|████████████████████████████████████████████████████████████████████████████████████████████████████████████████████████████████████████████████████████████████████████████████████████████████████████████████████████| 128/128 [05:14<00:00,  2.45s/it]
    Biases correction: 100%|██████████████████████████████████████████████████████████████████████████████████████████████████████████████████████████████████████████████████████████████████████████████████████████████████████████████████████████████| 48/48 [06:34<00:00,  8.21s/it]

.. parsed-literal::

    model quantization finished


.. code:: ipython3

    ov_encoder_path_int8 = "sam_image_encoder_int8.xml"
    serialize(quantized_model, ov_encoder_path_int8)

Validate Quantized Model Inference `⇑ <#top>`__
+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++


We can reuse the previous code to validate the output of ``INT8`` model.

.. code:: ipython3

    # Load INT8 model and run pipeline again
    ov_encoder_model_int8 = core.read_model(ov_encoder_path_int8)
    ov_encoder_int8 = core.compile_model(ov_encoder_model_int8, device.value)
    encoding_results = ov_encoder_int8(preprocessed_image)
    image_embeddings = encoding_results[ov_encoder_int8.output(0)]
    
    input_point = np.array([[500, 375]])
    input_label = np.array([1])
    coord = np.concatenate([input_point, np.array([[0.0, 0.0]])], axis=0)[None, :, :]
    label = np.concatenate([input_label, np.array([-1])], axis=0)[None, :].astype(np.float32)
    
    coord = resizer.apply_coords(coord, image.shape[:2]).astype(np.float32)
    inputs = {
        "image_embeddings": image_embeddings,
        "point_coords": coord,
        "point_labels": label,
    }
    results = ov_predictor(inputs)
    
    masks = results[ov_predictor.output(0)]
    masks = postprocess_masks(masks, image.shape[:-1])
    masks = masks > 0.0
    plt.figure(figsize=(10,10))
    plt.imshow(image)
    show_mask(masks, plt.gca())
    show_points(input_point, input_label, plt.gca())
    plt.axis('off')
    plt.show() 



.. image:: 237-segment-anything-with-output_files/237-segment-anything-with-output_80_0.png


Run ``INT8`` model in automatic mask generation mode

.. code:: ipython3

    ov_encoder = ov_encoder_int8
    prediction = automatic_mask_generation(image)
    out = draw_anns(image, prediction)
    cv2.imwrite("result_int8.png", out[:, :, ::-1])
    PIL.Image.open("result_int8.png")



.. parsed-literal::

      0%|          | 0/48 [00:00<?, ?it/s]




.. image:: 237-segment-anything-with-output_files/237-segment-anything-with-output_82_1.png



Compare Performance of the Original and Quantized Models `⇑ <#top>`__
+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

Finally, use the OpenVINO `Benchmark
Tool <https://docs.openvino.ai/2023.0/openvino_inference_engine_tools_benchmark_tool_README.html>`__
to measure the inference performance of the ``FP32`` and ``INT8``
models.

.. code:: ipython3

    # Inference FP32 model (OpenVINO IR)
    !benchmark_app -m $ov_encoder_path -d $device.value


.. parsed-literal::

    [Step 1/11] Parsing and validating input arguments
    [ INFO ] Parsing input parameters
    [Step 2/11] Loading OpenVINO Runtime
    [ WARNING ] Default duration 120 seconds is used for unknown device AUTO
    [ INFO ] OpenVINO:
    [ INFO ] Build ................................. 2023.0.1-11005-fa1c41994f3-releases/2023/0
    [ INFO ] 
    [ INFO ] Device info:
    [ INFO ] AUTO
    [ INFO ] Build ................................. 2023.0.1-11005-fa1c41994f3-releases/2023/0
    [ INFO ] 
    [ INFO ] 
    [Step 3/11] Setting device configuration
    [ WARNING ] Performance hint was not explicitly specified in command line. Device(AUTO) performance hint will be set to PerformanceMode.THROUGHPUT.
    [Step 4/11] Reading model files
    [ INFO ] Loading model files
    [ INFO ] Read model took 69.37 ms
    [ INFO ] Original model I/O parameters:
    [ INFO ] Model inputs:
    [ INFO ]     input.1 (node: input.1) : f32 / [...] / [1,3,1024,1024]
    [ INFO ] Model outputs:
    [ INFO ]     4017 (node: 4017) : f32 / [...] / [1,256,64,64]
    [Step 5/11] Resizing model to match image sizes and given batch
    [ INFO ] Model batch size: 1
    [Step 6/11] Configuring input of the model
    [ INFO ] Model inputs:
    [ INFO ]     input.1 (node: input.1) : u8 / [N,C,H,W] / [1,3,1024,1024]
    [ INFO ] Model outputs:
    [ INFO ]     4017 (node: 4017) : f32 / [...] / [1,256,64,64]
    [Step 7/11] Loading the model to the device
    [ INFO ] Compile model took 1196.87 ms
    [Step 8/11] Querying optimal runtime parameters
    [ INFO ] Model:
    [ INFO ]   PERFORMANCE_HINT: PerformanceMode.THROUGHPUT
    [ INFO ]   NETWORK_NAME: torch_jit
    [ INFO ]   OPTIMAL_NUMBER_OF_INFER_REQUESTS: 12
    [ INFO ]   MODEL_PRIORITY: Priority.MEDIUM
    [ INFO ]   MULTI_DEVICE_PRIORITIES: CPU
    [ INFO ]   CPU:
    [ INFO ]     CPU_BIND_THREAD: YES
    [ INFO ]     CPU_THREADS_NUM: 0
    [ INFO ]     CPU_THROUGHPUT_STREAMS: 12
    [ INFO ]     DEVICE_ID: 
    [ INFO ]     DUMP_EXEC_GRAPH_AS_DOT: 
    [ INFO ]     DYN_BATCH_ENABLED: NO
    [ INFO ]     DYN_BATCH_LIMIT: 0
    [ INFO ]     ENFORCE_BF16: NO
    [ INFO ]     EXCLUSIVE_ASYNC_REQUESTS: NO
    [ INFO ]     NETWORK_NAME: torch_jit
    [ INFO ]     OPTIMAL_NUMBER_OF_INFER_REQUESTS: 12
    [ INFO ]     PERFORMANCE_HINT: THROUGHPUT
    [ INFO ]     PERFORMANCE_HINT_NUM_REQUESTS: 0
    [ INFO ]     PERF_COUNT: NO
    [ INFO ]   EXECUTION_DEVICES: ['CPU']
    [Step 9/11] Creating infer requests and preparing input tensors
    [ WARNING ] No input files were given for input 'input.1'!. This input will be filled with random values!
    [ INFO ] Fill input 'input.1' with random values 
    [Step 10/11] Measuring performance (Start inference asynchronously, 12 inference requests, limits: 120000 ms duration)
    [ INFO ] Benchmarking in inference only mode (inputs filling are not included in measurement loop).
    [ INFO ] First inference took 4043.51 ms
    [Step 11/11] Dumping statistics report
    [ INFO ] Execution Devices:['CPU']
    [ INFO ] Count:            108 iterations
    [ INFO ] Duration:         135037.41 ms
    [ INFO ] Latency:
    [ INFO ]    Median:        14646.89 ms
    [ INFO ]    Average:       14615.54 ms
    [ INFO ]    Min:           6295.79 ms
    [ INFO ]    Max:           19356.55 ms
    [ INFO ] Throughput:   0.80 FPS


.. code:: ipython3

    # Inference INT8 model (OpenVINO IR)
    !benchmark_app -m $ov_encoder_path_int8 -d $device.value


.. parsed-literal::

    [Step 1/11] Parsing and validating input arguments
    [ INFO ] Parsing input parameters
    [Step 2/11] Loading OpenVINO Runtime
    [ WARNING ] Default duration 120 seconds is used for unknown device AUTO
    [ INFO ] OpenVINO:
    [ INFO ] Build ................................. 2023.0.1-11005-fa1c41994f3-releases/2023/0
    [ INFO ] 
    [ INFO ] Device info:
    [ INFO ] AUTO
    [ INFO ] Build ................................. 2023.0.1-11005-fa1c41994f3-releases/2023/0
    [ INFO ] 
    [ INFO ] 
    [Step 3/11] Setting device configuration
    [ WARNING ] Performance hint was not explicitly specified in command line. Device(AUTO) performance hint will be set to PerformanceMode.THROUGHPUT.
    [Step 4/11] Reading model files
    [ INFO ] Loading model files
    [ INFO ] Read model took 104.31 ms
    [ INFO ] Original model I/O parameters:
    [ INFO ] Model inputs:
    [ INFO ]     input.1 (node: input.1) : f32 / [...] / [1,3,1024,1024]
    [ INFO ] Model outputs:
    [ INFO ]     4017 (node: 4017) : f32 / [...] / [1,256,64,64]
    [Step 5/11] Resizing model to match image sizes and given batch
    [ INFO ] Model batch size: 1
    [Step 6/11] Configuring input of the model
    [ INFO ] Model inputs:
    [ INFO ]     input.1 (node: input.1) : u8 / [N,C,H,W] / [1,3,1024,1024]
    [ INFO ] Model outputs:
    [ INFO ]     4017 (node: 4017) : f32 / [...] / [1,256,64,64]
    [Step 7/11] Loading the model to the device
    [ INFO ] Compile model took 1414.62 ms
    [Step 8/11] Querying optimal runtime parameters
    [ INFO ] Model:
    [ INFO ]   PERFORMANCE_HINT: PerformanceMode.THROUGHPUT
    [ INFO ]   NETWORK_NAME: torch_jit
    [ INFO ]   OPTIMAL_NUMBER_OF_INFER_REQUESTS: 12
    [ INFO ]   MODEL_PRIORITY: Priority.MEDIUM
    [ INFO ]   MULTI_DEVICE_PRIORITIES: CPU
    [ INFO ]   CPU:
    [ INFO ]     CPU_BIND_THREAD: YES
    [ INFO ]     CPU_THREADS_NUM: 0
    [ INFO ]     CPU_THROUGHPUT_STREAMS: 12
    [ INFO ]     DEVICE_ID: 
    [ INFO ]     DUMP_EXEC_GRAPH_AS_DOT: 
    [ INFO ]     DYN_BATCH_ENABLED: NO
    [ INFO ]     DYN_BATCH_LIMIT: 0
    [ INFO ]     ENFORCE_BF16: NO
    [ INFO ]     EXCLUSIVE_ASYNC_REQUESTS: NO
    [ INFO ]     NETWORK_NAME: torch_jit
    [ INFO ]     OPTIMAL_NUMBER_OF_INFER_REQUESTS: 12
    [ INFO ]     PERFORMANCE_HINT: THROUGHPUT
    [ INFO ]     PERFORMANCE_HINT_NUM_REQUESTS: 0
    [ INFO ]     PERF_COUNT: NO
    [ INFO ]   EXECUTION_DEVICES: ['CPU']
    [Step 9/11] Creating infer requests and preparing input tensors
    [ WARNING ] No input files were given for input 'input.1'!. This input will be filled with random values!
    [ INFO ] Fill input 'input.1' with random values 
    [Step 10/11] Measuring performance (Start inference asynchronously, 12 inference requests, limits: 120000 ms duration)
    [ INFO ] Benchmarking in inference only mode (inputs filling are not included in measurement loop).
    [ INFO ] First inference took 2694.03 ms
    [Step 11/11] Dumping statistics report
    [ INFO ] Execution Devices:['CPU']
    [ INFO ] Count:            132 iterations
    [ INFO ] Duration:         129404.57 ms
    [ INFO ] Latency:
    [ INFO ]    Median:        11651.20 ms
    [ INFO ]    Average:       11526.49 ms
    [ INFO ]    Min:           5003.59 ms
    [ INFO ]    Max:           13329.53 ms
    [ INFO ] Throughput:   1.02 FPS

