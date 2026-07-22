#include "tl_model_sam1.h"
#include "sam_apis.h"
#include <fstream>


AUTO_REGISTER_MODEL(Sam100m, "sam:100m");
AUTO_REGISTER_MODEL(Sam300m, "sam:300m");
AUTO_REGISTER_MODEL(Sam600m, "sam:latest");

//blob:33] ===> path: C:\Users\njtl007\.cache\osam\models\blobs\sam_vit_h_4b8939.quantized.decoder.onnx
//model:84] Input Name: image_embeddings, Dimensions: [1, 256, 64, 64], Type: FLOAT
//model:84] Input Name: point_coords, Dimensions: [1, -1, 2], Type: FLOAT
//model:84] Input Name: point_labels, Dimensions: [1, -1], Type: FLOAT
//model:84] Input Name: mask_input, Dimensions: [1, 1, 256, 256], Type: FLOAT
//model:84] Input Name: has_mask_input, Dimensions: [1], Type: FLOAT
//model:84] Input Name: orig_im_size, Dimensions: [2], Type: FLOAT
//model:93] Output Name: masks, Dimensions: [-1, -1, -1, -1], Type: FLOAT
//model:93] Output Name: iou_predictions, Dimensions: [-1, 1], Type: FLOAT
//model:93] Output Name: low_res_masks, Dimensions: [-1, 1, -1, -1], Type: FLOAT
//blob:33] ===> path: C:\Users\njtl007\.cache\osam\models\blobs\sam_vit_h_4b8939.quantized.encoder.onnx
//model:84] Input Name: x, Dimensions: [-1, 3, 1024, 1024], Type: FLOAT
//model:93] Output Name: image_embeddings, Dimensions: [-1, 256, 64, 64], Type: FLOAT
//model:25] ===> output: image_embeddings, ElementType: FLOAT, Shape: [1, 256, 64, 64], elements: 1048576
//model:30] Run success, usage: 16509675
//model:25] ===> output: masks, ElementType: FLOAT, Shape: [1, 1, 1600, 2560], elements: 4096000
//model:25] ===> output: iou_predictions, ElementType: FLOAT, Shape: [1, 1], elements: 1
//model:25] ===> output: low_res_masks, ElementType: FLOAT, Shape: [1, 1, 256, 256], elements: 65536
//model:30] Run success, usage: 95882
int32_t Sam1::get_input_size(const InferenceSession &session) const {
    const auto typeInfo = session.session_->GetInputTypeInfo(0);
    const auto inputDims = typeInfo.GetTensorTypeAndShapeInfo().GetShape();
    if (inputDims[2] != inputDims[3]) { // NCHW
        throw std::runtime_error("Input height and width must be equal");
    }
    return inputDims[2];
}

float compute_scale_to_resize_image(int32_t image_h, int32_t image_w, int32_t target_size, int32_t &scaled_h, int32_t &scaled_w) {
    float scale;
    if (image_w > image_h) {
        scale = (1.0 * target_size) / image_w;
        scaled_h = static_cast<int>(round(image_h * scale));
        scaled_w = target_size;
    } else {
        scale = (1.0 * target_size) / image_h;
        scaled_h = target_size;
        scaled_w = static_cast<int>(round(image_w * scale));
    }
    return scale;
}

cv::Mat resize_image(const cv::Mat &image, int32_t target_size, float &scale) {
    int32_t scaled_h = 0;
    int32_t scaled_w = 0;
    scale = compute_scale_to_resize_image(image.rows, image.cols, target_size, scaled_h, scaled_w);

    cv::Mat resized;
    cv::resize(image, resized, cv::Size(scaled_w, scaled_h), 0, 0, cv::INTER_LINEAR);
    return resized;
}

cv::Mat compute_input_from_image(const cv::Mat &image, int32_t input_size) {
    float scale;
    const cv::Mat resized = resize_image(image, input_size, scale);

    cv::Mat padded;
    const int32_t pad_h = input_size - resized.rows;
    const int32_t pad_w = input_size - resized.cols;
    cv::copyMakeBorder(resized, padded, 0, pad_h, 0, pad_w, cv::BORDER_CONSTANT);

    // 执行通道标准化
    std::vector<cv::Mat> channels;
    cv::split(padded, channels);
    channels[0] = (channels[0] - 123.675f) / 58.395f;  // B通道
    channels[1] = (channels[1] - 116.280f) / 57.120f;  // G通道
    channels[2] = (channels[2] - 103.530f) / 57.375f;  // R通道
    cv::merge(channels, padded);

    // 使用blobFromImage进行维度转换（保持已归一化的值）
    cv::Mat blob = cv::dnn::blobFromImage(padded, 1.0, cv::Size(), cv::Scalar(), false, false, CV_32F);
    return blob;

    // BGR顺序
    //const cv::Scalar mean = cv::Scalar(123.675, 116.28, 103.53);  // 每个通道减去对应的均值
    //const cv::Scalar std_inv = cv::Scalar(1/58.395, 1/57.12, 1/57.375);  // 每个通道除以对应的标准差
    //cv::Mat blob = cv::dnn::blobFromImage(padded, 1.0, cv::Size(), mean, true, false, CV_32F);

    //input_: npt.NDArray[np.float32] = (
    //    scaled_image.astype(np.float32) - np.array([123.675, 116.28, 103.53], dtype=np.float32)
    //) / np.array([58.395, 57.12, 57.375], dtype=np.float32)
    //input_ = np.pad(
    //    input_, (
    //        (0, input_size - input_.shape[0]),  # x轴
    //        (0, input_size - input_.shape[1]),  # y轴
    //        (0, 0),                             # z轴
    //    ),
    //)
    //input_ = input_.transpose(2, 0, 1)[None, :, :, :]
    //return input_;
}

ImageEmbedding Sam1::compute_image_embedding_from_image(InferenceSession &session, const cv::Mat &image) {
    //if image.ndim == 2:
    //    raise ValueError("Grayscale images are not supported")
    //if image.ndim == 3 and image.shape[2] == 4:
    //    raise ValueError("RGBA images are not supported")

    const int32_t input_size = get_input_size(session);
    cv::Mat input = compute_input_from_image(image, input_size);
    const auto inputDims_ = std::vector<int64_t>{1, 3, input_size, input_size};   // NCHW
    const auto inputSize_ = mult_size(inputDims_);

    session.input_tensors_[0] = Ort::Value::CreateTensor<float>(memoryInfo_, input.ptr<float>(), inputSize_, inputDims_.data(), inputDims_.size());

    session.RunInference();

    return {image.rows, image.cols, std::move(session.output_tensors_[0]) };
}

cv::Mat Sam1::generate_mask_from_image_embedding(InferenceSession &session, ImageEmbedding &image_embedding, const Prompt &prompt, int32_t input_size) {
    if (prompt.point_coords_.empty() || prompt.point_labels_.empty()) {
        throw std::runtime_error("Prompt must contain points and point_labels");
    }

    auto onnx_coords = prompt.point_coords_;
    auto onnx_labels = prompt.point_labels_;
    onnx_coords.push_back(cv::Point2f(0, 0));
    onnx_labels.push_back(-1);

    int32_t scaled_h = 0;
    int32_t scaled_w = 0;
    std::vector<float> point_coords;
    float scale = compute_scale_to_resize_image(image_embedding.image_h, image_embedding.image_w, input_size, scaled_h, scaled_w);
    const float scale_x = (1.0 * scaled_w / image_embedding.image_w);
    const float scale_y = (1.0 * scaled_h / image_embedding.image_h);
    SPDLOG_INFO("===> scale:{:.3f} image_h:{} image_w:{} scaled_h:{} scaled_w:{} scale_y:{:.3f} scale_x:{:.3f}", scale, image_embedding.image_h, image_embedding.image_w, scaled_h, scaled_w, scale_y, scale_x);
    for (const auto &coord : onnx_coords) {
        point_coords.push_back(coord.x * scale_x);
        point_coords.push_back(coord.y * scale_y);
    }

    toFile("embedding.hex", image_embedding.embedding);
    session.input_tensors_[0] = std::move(image_embedding.embedding);

    const std::vector<int64_t> inputDims1_{1, static_cast<int64_t>(onnx_coords.size()), 2};
    session.input_tensors_[1] = Ort::Value::CreateTensor<float>(memoryInfo_, point_coords.data(), point_coords.size(), inputDims1_.data(), inputDims1_.size());

    const std::vector<int64_t> inputDims2_{1, static_cast<int64_t>(onnx_labels.size())};
    session.input_tensors_[2] = Ort::Value::CreateTensor<float>(memoryInfo_, onnx_labels.data(), onnx_labels.size(), inputDims2_.data(), inputDims2_.size());

    const std::vector<int64_t> inputDims3_{1, 1, 256, 256};
    std::vector<float> onnx_mask_input(mult_size(inputDims3_), 0);
    session.input_tensors_[3] = Ort::Value::CreateTensor<float>(memoryInfo_, onnx_mask_input.data(), onnx_mask_input.size(), inputDims3_.data(), inputDims3_.size());

    const std::vector<int64_t> inputDims4_{1};
    std::vector<float> onnx_has_mask_input{-1};
    session.input_tensors_[4] = Ort::Value::CreateTensor<float>(memoryInfo_, onnx_has_mask_input.data(), onnx_has_mask_input.size(), inputDims4_.data(), inputDims4_.size());

    const std::vector<int64_t> inputDims5_{2};
    std::vector<float> orig_im_size{1.0f*image_embedding.image_h, 1.0f*image_embedding.image_w};
    session.input_tensors_[5] = Ort::Value::CreateTensor<float>(memoryInfo_, orig_im_size.data(), orig_im_size.size(), inputDims5_.data(), inputDims5_.size());

    toFile("1.input_tensors.hex", session.input_tensors_[1]);
    toFile("2.input_tensors.hex", session.input_tensors_[2]);
    toFile("3.input_tensors.hex", session.input_tensors_[3]);
    toFile("4.input_tensors.hex", session.input_tensors_[4]);
    toFile("5.input_tensors.hex", session.input_tensors_[5]);

    session.RunInference();
    // 推理完成恢复输入数据
    image_embedding.embedding = std::move(session.input_tensors_[0]);

    auto &output_masks    = session.output_tensors_[0];   //[1, 1, H, W]
    auto &iou_predictions = session.output_tensors_[1];   //[1, 1]
    auto &low_res_masks   = session.output_tensors_[2];   //[1, 1, 256, 256]
    const auto dims = output_masks.GetTensorTypeAndShapeInfo().GetShape();
    auto *const x_data = output_masks.GetTensorMutableData<float>();
    toFile("output_masks.hex", output_masks);

    const auto image_n = static_cast<int32_t>(dims[0]);
    const auto image_c = static_cast<int32_t>(dims[1]);
    const auto image_h = static_cast<int32_t>(dims[2]);
    const auto image_w = static_cast<int32_t>(dims[3]);

    //(1, 1, 3, H, W) -> (H, W)
    return cv::Mat(image_h, image_w, CV_32FC1, x_data) > 0;
}