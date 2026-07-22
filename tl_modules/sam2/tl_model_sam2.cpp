#include "tl_model_sam2.h"
#include "sam_apis.h"


AUTO_REGISTER_MODEL(Sam2Tiny, "sam2:tiny");
AUTO_REGISTER_MODEL(Sam2Small, "sam2:small");
AUTO_REGISTER_MODEL(Sam2BasePlus, "sam2:latest");
AUTO_REGISTER_MODEL(Sam2Large, "sam2:large");

//blob:33] ===> path: C:\Users\njtl007\.cache\osam\models\blobs\sam2.1_base_plus_preprocess.onnx
//model:84] Input Name: input, Dimensions: [1, 3, 1024, 1024], Type: FLOAT
//model:93] Output Name: image_embeddings, Dimensions: [1, 256, 64, 64], Type: FLOAT
//model:93] Output Name: high_res_features1, Dimensions: [1, 32, 256, 256], Type: FLOAT
//model:93] Output Name: high_res_features2, Dimensions: [1, 64, 128, 128], Type: FLOAT
//blob:33] ===> path: C:\Users\njtl007\.cache\osam\models\blobs\sam2.1_base_plus.onnx
//model:84] Input Name: image_embeddings, Dimensions: [1, 256, 64, 64], Type: FLOAT
//model:84] Input Name: high_res_features1, Dimensions: [1, 32, 256, 256], Type: FLOAT
//model:84] Input Name: high_res_features2, Dimensions: [1, 64, 128, 128], Type: FLOAT
//model:84] Input Name: point_coords, Dimensions: [-1, -1, 2], Type: FLOAT
//model:84] Input Name: point_labels, Dimensions: [-1, -1], Type: FLOAT
//model:84] Input Name: mask_input, Dimensions: [-1, 1, 256, 256], Type: FLOAT
//model:84] Input Name: has_mask_input, Dimensions: [-1], Type: FLOAT
//model:84] Input Name: orig_im_size, Dimensions: [2], Type: INT64
//model:93] Output Name: masks, Dimensions: [-1, -1, -1, -1], Type: FLOAT
//model:93] Output Name: iou_predictions, Dimensions: [-1, 4], Type: FLOAT
//model:93] Output Name: low_res_masks, Dimensions: [-1, -1, -1, -1], Type: FLOAT
//model:25] ===> output: image_embeddings, ElementType: FLOAT, Shape: [1, 256, 64, 64], elements: 1048576
//model:25] ===> output: high_res_features1, ElementType: FLOAT, Shape: [1, 32, 256, 256], elements: 2097152
//model:25] ===> output: high_res_features2, ElementType: FLOAT, Shape: [1, 64, 128, 128], elements: 1048576
//model:30] Run success, usage: 4437621
//model:25] ===> output: masks, ElementType: FLOAT, Shape: [1, 4, 1600, 2560], elements: 16384000
//model:25] ===> output: iou_predictions, ElementType: FLOAT, Shape: [1, 4], elements: 4
//model:25] ===> output: low_res_masks, ElementType: FLOAT, Shape: [1, 4, 256, 256], elements: 262144
//model:30] Run success, usage: 83435
int32_t Sam2::get_input_size(const InferenceSession &session) const {
    const auto typeInfo = session.session_->GetInputTypeInfo(0);
    const auto inputDims = typeInfo.GetTensorTypeAndShapeInfo().GetShape();
    if (inputDims[2] != inputDims[3]) { // NCHW
        throw std::runtime_error("Input height and width must be equal");
    }
    return inputDims[2];
}

ImageEmbedding Sam2::compute_image_embedding_from_image(InferenceSession &session, const cv::Mat &image) {
    //if image.ndim == 2:
    //    raise ValueError("Grayscale images are not supported")
    //if image.ndim == 3 and image.shape[2] == 4:
    //    raise ValueError("RGBA images are not supported")

    //input_height: int
    //input_width: int
    //input_height, input_width = encoder_session.get_inputs()[0].shape[2:]
    const auto typeInfo = session.session_->GetInputTypeInfo(0);
    const auto inputDims_ = typeInfo.GetTensorTypeAndShapeInfo().GetShape();
    const int32_t input_h = static_cast<int32_t>(inputDims_[2]);
    const int32_t input_w = static_cast<int32_t>(inputDims_[3]);

    //input_: npt.NDArray[np.float32]
    //input_ = (
    //    imgviz.resize(image, width=input_width, height=input_height).astype(np.float32)
    //    / 255
    //)
    cv::Mat resized;
    cv::resize(image, resized, cv::Size(input_w, input_h), 0, 0, cv::INTER_LINEAR);
    resized.convertTo(resized, CV_32FC3);

    //input_ = input_ - np.array([0.485, 0.456, 0.406], dtype=np.float32) / np.array(
    //    [0.229, 0.224, 0.225], dtype=np.float32
    //)
    // 执行通道标准化
    std::vector<cv::Mat> channels;
    cv::split(resized, channels);
    channels[0] = (channels[0] / 255.0 - 0.485f) / 0.229f;  // B通道
    channels[1] = (channels[1] / 255.0 - 0.456f) / 0.224f;  // G通道
    channels[2] = (channels[2] / 255.0 - 0.406f) / 0.225f;  // R通道
    cv::merge(channels, resized);

    //input_ = input_.transpose(2, 0, 1)[None]
    // 使用blobFromImage进行维度转换（保持已归一化的值）
    cv::Mat blob = cv::dnn::blobFromImage(resized, 1.0, cv::Size(), cv::Scalar(), false, false, CV_32F);
    session.input_tensors_[0] = Ort::Value::CreateTensor<float>(memoryInfo_, blob.ptr<float>(), mult_size(inputDims_), inputDims_.data(), inputDims_.size());
    toFile("0.image_input.hex", session.input_tensors_[0]);

    //std::vector<float> inputData0_(mult_size(inputDims_), 0);
    //fromFile("C:/WORK/TlAssistant/PromptSam2/0.image_input.hex", inputData0_);
    //session.input_tensors_[0] = Ort::Value::CreateTensor<float>(memoryInfo_, inputData0_.data(), inputData0_.size(), inputDims_.data(), inputDims_.size());

    session.RunInference();
    //image_embedding    = cast(npt.NDArray[np.float32], outputs[0])
    //high_res_features1 = cast(npt.NDArray[np.float32], outputs[1])
    //high_res_features2 = cast(npt.NDArray[np.float32], outputs[2])

    ImageEmbedding image_embedding{image.rows, image.cols, std::move(session.output_tensors_[0])};
    image_embedding.extra_features.resize(2);
    image_embedding.extra_features[0] = std::move(session.output_tensors_[1]);
    image_embedding.extra_features[1] = std::move(session.output_tensors_[2]);
    return image_embedding;


    //return {image.rows, image.cols, std::move(session.output_tensors_[0]), std::vector<Ort::Value>{ std::move(session.output_tensors_[1]), std::move(session.output_tensors_[2])} };

    //return ImageEmbedding(
    //    original_height=image.shape[0],
    //    original_width=image.shape[1],
    //    embedding=image_embedding[0],
    //    extra_features=[high_res_features1[0], high_res_features2[0]],
    //)
}

cv::Mat Sam2::generate_mask_from_image_embedding(InferenceSession &session, ImageEmbedding &image_embedding, const Prompt &prompt, int32_t input_size) {
    //input_point: npt.NDArray[np.float32] = np.array(prompt.points, dtype=np.float32)
    //input_point = input_point / np.array(
    //    [
    //        image_embedding.original_width / input_size,
    //        image_embedding.original_height / input_size,
    //    ],
    //    dtype=np.float32,
    //)
    std::vector<float> input_points;
    const float scale_x = (1.0 * image_embedding.image_w / input_size);
    const float scale_y = (1.0 * image_embedding.image_h / input_size);
    //LOG_INFO("===> scale:{:.3f} image_h:{} image_w:{} scaled_h:{} scaled_w:{} scale_y:{:.3f} scale_x:{:.3f}", scale, image_embedding.image_h, image_embedding.image_w, scaled_h, scaled_w, scale_y, scale_x);
    for (const auto &coord : prompt.point_coords_) {
        input_points.push_back(coord.x / scale_x);
        input_points.push_back(coord.y / scale_y);
    }

    auto input_label = prompt.point_labels_;
    //input_label: npt.NDArray[np.float32] = np.array(
    //    prompt.point_labels, dtype=np.float32
    //)

    //decoder_inputs = {
    //    "image_embeddings": image_embedding.embedding[None],
    //    "high_res_features1": image_embedding.extra_features[0][None],
    //    "high_res_features2": image_embedding.extra_features[1][None],
    //    "point_coords": input_point[None],
    //    "point_labels": input_label[None],
    //    "mask_input": np.zeros((1, 1, 256, 256), dtype=np.float32),
    //    "has_mask_input": np.array([0], dtype=np.float32),
    //    "orig_im_size": np.array(
    //        (image_embedding.original_height, image_embedding.original_width),
    //        dtype=np.int64,
    //    ),
    //}
    session.input_tensors_[0] = std::move(image_embedding.embedding);
    session.input_tensors_[1] = std::move(image_embedding.extra_features[0]);
    session.input_tensors_[2] = std::move(image_embedding.extra_features[1]);


    //std::vector<int64_t> inputDims0_ = image_embedding.embedding.GetTensorTypeAndShapeInfo().GetShape();
    //std::vector<float> inputData0_(mult_size(inputDims0_), 0);
    //fromFile("C:/WORK/TlAssistant/PromptSam2/1.image_embedding.hex", inputData0_);
    //session.input_tensors_[0] = Ort::Value::CreateTensor<float>(memoryInfo_, inputData0_.data(), inputData0_.size(), inputDims0_.data(), inputDims0_.size());
    //
    //std::vector<int64_t> inputDims1_ = image_embedding.extra_features[0].GetTensorTypeAndShapeInfo().GetShape();
    //std::vector<float> inputData1_(mult_size(inputDims1_), 0);
    //fromFile("C:/WORK/TlAssistant/PromptSam2/2.high_res_features1.hex", inputData1_);
    //session.input_tensors_[1] = Ort::Value::CreateTensor<float>(memoryInfo_, inputData1_.data(), inputData1_.size(), inputDims1_.data(), inputDims1_.size());
    //
    //std::vector<int64_t> inputDims2_ = image_embedding.extra_features[1].GetTensorTypeAndShapeInfo().GetShape();
    //std::vector<float> inputData2_(mult_size(inputDims2_), 0);
    //fromFile("C:/WORK/TlAssistant/PromptSam2/3.high_res_features2.hex", inputData2_);
    //session.input_tensors_[2] = Ort::Value::CreateTensor<float>(memoryInfo_, inputData2_.data(), inputData2_.size(), inputDims2_.data(), inputDims2_.size());

    const std::vector<int64_t> inputDims3_{1, static_cast<int64_t>(prompt.point_coords_.size()), 2};
    session.input_tensors_[3] = Ort::Value::CreateTensor<float>(memoryInfo_, input_points.data(), input_points.size(), inputDims3_.data(), inputDims3_.size());

    const std::vector<int64_t> inputDims4_{1, static_cast<int64_t>(input_label.size())};
    session.input_tensors_[4] = Ort::Value::CreateTensor<float>(memoryInfo_, input_label.data(), input_label.size(), inputDims4_.data(), inputDims4_.size());

    const std::vector<int64_t> inputDims5_{1, 1, 256, 256};
    std::vector<float> mask_input(mult_size(inputDims5_), 0);
    session.input_tensors_[5] = Ort::Value::CreateTensor<float>(memoryInfo_, mask_input.data(), mask_input.size(), inputDims5_.data(), inputDims5_.size());

    const std::vector<int64_t> inputDims6_{1};
    std::vector<float> has_mask_input{-1};
    session.input_tensors_[6] = Ort::Value::CreateTensor<float>(memoryInfo_, has_mask_input.data(), has_mask_input.size(), inputDims6_.data(), inputDims6_.size());

    const std::vector<int64_t> inputDims7_{2};
    std::vector<int64_t> orig_im_size{image_embedding.image_h, image_embedding.image_w};
    session.input_tensors_[7] = Ort::Value::CreateTensor<int64_t>(memoryInfo_, orig_im_size.data(), orig_im_size.size(), inputDims7_.data(), inputDims7_.size());

    for (auto i = 0; i < session.input_tensors_.size(); ++i) {
        toFile(std::format("{}.input_tensors.hex", i), session.input_tensors_[i]);
    }

    session.RunInference();
    // 推理完成恢复输入数据
    image_embedding.embedding         = std::move(session.input_tensors_[0]);
    image_embedding.extra_features[0] = std::move(session.input_tensors_[1]);
    image_embedding.extra_features[1] = std::move(session.input_tensors_[2]);

    //masks, scores, _low_res_mask = decoder_session.run(None, decoder_inputs)
    //masks = cast(npt.NDArray[np.float32], masks)
    //scores = cast(npt.NDArray[np.float32], scores)
    auto &masks   = session.output_tensors_[0];   //[1, 3, H, W]
    auto &scores  = session.output_tensors_[1];   //[1, 1, 3]
    auto &low_res_mask = session.output_tensors_[2];   //[1, 3, 256, 256]
    const auto dims = masks.GetTensorTypeAndShapeInfo().GetShape();
    auto *const x_data = masks.GetTensorMutableData<float>();

    //mask: npt.NDArray[np.bool_] = (
    //    masks[0, np.argmax(scores)] > 0.0
    //)  # (1, N, H, W) -> (H, W)
    //return mask

    const auto image_n = static_cast<int32_t>(dims[0]);
    const auto image_c = static_cast<int32_t>(dims[1]);
    const auto image_h = static_cast<int32_t>(dims[2]);
    const auto image_w = static_cast<int32_t>(dims[3]);
    //(1, 3, H, W) -> (H, W)
    return cv::Mat(image_h, image_w, CV_32FC1, x_data) > 0.0;
}