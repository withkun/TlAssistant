#include "tl_yolo_world.h"
#include "sam_apis.h"
#include "base/tokenizer.h"


AUTO_REGISTER_MODEL(YoloWorld, "yoloworld:latest");

ImageEmbedding YoloWorld::encode_image(const cv::Mat &image) {
    return {image.rows, image.cols};
}

GenerateResponse YoloWorld::generate(GenerateRequest &request) {
    if (request.prompt.texts_.empty()) {
        throw std::runtime_error("prompt.texts is required");
    }

    ImageEmbedding token_embedding = encode_token(sessions_["textual"], request.prompt.texts_);

    ImageEmbedding image_embedding = decode_image(sessions_["yolo"], request.image, token_embedding);

    return non_maximum_suppression(sessions_["nms"], image_embedding, request.prompt);
}

ImageEmbedding YoloWorld::encode_token(InferenceSession &session, const Tokens &texts) {
    std::vector<int64_t> tokens;
    for (const auto &token : tokenize(texts, TOKEN_W_)) {
        tokens.insert(tokens.end(), token.begin(), token.end());
    }
    std::vector<int64_t> token_dims{ static_cast<int64_t>(texts.size()), TOKEN_W_};
    session.input_tensors_[0] = Ort::Value::CreateTensor<int64_t>(memoryInfo_, tokens.data(), tokens.size(), token_dims.data(), token_dims.size());

    session.RunInference();

    return { 0, 0, std::move(session.output_tensors_[0]) };
}

ImageEmbedding YoloWorld::decode_image(InferenceSession &session, const cv::Mat &image, ImageEmbedding &token_Embedding) {
    int32_t offset_y;
    int32_t offset_x;
    cv::Mat blob = transform_image(image, input_size_, offset_y, offset_x);
    const std::vector<int64_t> inputDims0_{1, 3, input_size_, input_size_};
    session.input_tensors_[0] = Ort::Value::CreateTensor<float>(memoryInfo_, blob.ptr<float>(), mult_size(inputDims0_), inputDims0_.data(), inputDims0_.size());

    // ElementType: FLOAT, Shape: [1, 512], elements: 512
    const auto tokenInfo = token_Embedding.embedding.GetTensorTypeAndShapeInfo();
    const auto tokenDims = tokenInfo.GetShape();
    const auto TOKEN_N = static_cast<int32_t>(tokenDims[0]);
    const auto TOKEN_W = static_cast<int32_t>(tokenDims[1]);
    cv::Mat text_features(TOKEN_N, TOKEN_W, CV_32F, token_Embedding.embedding.GetTensorMutableRawData());

    // 计算每行的L2范数
    cv::Mat norms = cv::Mat::zeros(text_features.rows, 1, CV_32F);
    for (int32_t i = 0; i < text_features.rows; ++i) {
        cv::Mat row = text_features.row(i);
        norms.at<float>(i, 0) = cv::norm(row, cv::NORM_L2);
    }
    // 处理零向量（避免除以零）
    cv::Mat mask = norms == 0.0f;
    norms.setTo(1.0f, mask);  // 将零范数设为1
    // 逐行归一化
    for (int32_t i = 0; i < text_features.rows; ++i) {
        text_features.row(i) = text_features.row(i) / norms.at<float>(i, 0);
    }
    const std::vector<int64_t> inputDims1_{1, text_features.rows, TOKEN_W};
    session.input_tensors_[1] = Ort::Value::CreateTensor<float>(memoryInfo_, text_features.ptr<float>(), mult_size(inputDims1_), inputDims1_.data(), inputDims1_.size());
    SPDLOG_INFO("Run decode image text_features: {}", text_features.size);

    toFile("0.input_tensors.hex", session.input_tensors_[0]);
    toFile("0.input_tensors.hex", session.input_tensors_[1]);
    session.RunInference();

    ImageEmbedding image_embedding{image.rows, image.cols, std::move(session.input_tensors_[0])};
    image_embedding.extra_features.resize(2);
    image_embedding.extra_features[0] = std::move(session.output_tensors_[0]);  //scores
    image_embedding.extra_features[1] = std::move(session.output_tensors_[1]);  //boxes
    return std::move(image_embedding);
}

GenerateResponse YoloWorld::non_maximum_suppression(InferenceSession &session, ImageEmbedding &image_embedding, Prompt &prompt) {
    SPDLOG_INFO("Run non maximum suppression: iou_threshold:{:.2f}, score_threshold:{:.2f}", prompt.iou_threshold_, prompt.score_threshold_);
    //scores: ElementType: FLOAT, Shape: [1, 8400, 2], elements: 16800
    const auto scoreInfo = image_embedding.extra_features[0].GetTensorTypeAndShapeInfo();
    const auto scoreDims = scoreInfo.GetShape();
    const auto SCORE_C = static_cast<int32_t>(scoreDims[0]);   // 1
    const auto SCORE_H = static_cast<int32_t>(scoreDims[1]);   // 8400
    const auto SCORE_W = static_cast<int32_t>(scoreDims[2]);   // 2
    cv::Mat scores(SCORE_C*SCORE_H, SCORE_W, CV_32F, image_embedding.extra_features[0].GetTensorMutableRawData());
    //boxes: ElementType: FLOAT, Shape: [1, 8400, 4], elements: 33600
    const auto boxesInfo = image_embedding.extra_features[1].GetTensorTypeAndShapeInfo();
    const auto boxesDims = boxesInfo.GetShape();
    const auto BOXES_C = static_cast<int32_t>(boxesDims[0]);   // 1
    const auto BOXES_H = static_cast<int32_t>(boxesDims[1]);   // 8400
    const auto BOXES_W = static_cast<int32_t>(boxesDims[2]);   // 4
    cv::Mat bboxes(BOXES_C*BOXES_H, BOXES_W, CV_32F, image_embedding.extra_features[1].GetTensorMutableRawData());

    //fromFile("C:/WORK/TlAssistant/PromptYolo/score_8400.hex", scores);
    //fromFile("C:/WORK/TlAssistant/PromptYolo/boxes_8400.hex", bboxes);

    //转置维度: scores.transpose(0,2,1)
    //toFile("score_chw.hex", scores);
    cv::Mat blob = scores.t();
    // 如果转秩后内存不是连续则需要重新创建保证连续.
    if (!blob.isContinuous()) {
        blob.copyTo(blob); // 这实际上会创建一个新的连续矩阵并复制数据
    }
    //toFile("score_final.hex", blob);

    const std::vector<int64_t> scalarDims{ 1 };
    const std::vector<int64_t> scoresDims{ scoreDims[0], scoreDims[2], scoreDims[1] };
    SPDLOG_INFO("Run non maximum suppression scoresDims: {}, scalarDims: {}", scoresDims, scalarDims);
    session.input_tensors_[0] = std::move(image_embedding.extra_features[1]);    // boxes
    session.input_tensors_[1] = Ort::Value::CreateTensor<float>(memoryInfo_, blob.ptr<float>(), mult_size(scoresDims), scoresDims.data(), scoresDims.size());
    session.input_tensors_[2] = Ort::Value::CreateTensor<int64_t>(memoryInfo_, &prompt.max_annotations_, 1, scalarDims.data(), scalarDims.size());
    session.input_tensors_[3] = Ort::Value::CreateTensor<float>(memoryInfo_, &prompt.iou_threshold_, 1, scalarDims.data(), scalarDims.size());
    session.input_tensors_[4] = Ort::Value::CreateTensor<float>(memoryInfo_, &prompt.score_threshold_, 1, scalarDims.data(), scalarDims.size());

    session.RunInference();
    image_embedding.extra_features[1] = std::move(session.input_tensors_[0]);    // boxes


    // selected_indices, Dimensions: [-1, 3], Type: INT64
    const auto selectInfo = session.output_tensors_[0].GetTensorTypeAndShapeInfo();
    const auto selectDims = selectInfo.GetShape();
    const auto SELECT_N = static_cast<int32_t>(selectDims[0]);
    const auto SELECT_W = static_cast<int32_t>(selectDims[1]);

    GenerateResponse response;
    response.annotations.reserve(SELECT_N);
    response.image_embedding = std::move(image_embedding);
    const float scale_xy = 1.0 * input_size_ / std::max(image_embedding.image_h, image_embedding.image_w);

    //bboxes = transform_boxes(bboxes);
    const cv::Mat selected(SELECT_N, SELECT_W, CV_64F, session.output_tensors_[0].GetTensorMutableRawData());
    for (int32_t n = 0; n < SELECT_N; ++n) {
        const int64_t idx     = selected.at<int64_t>(n, 0);
        const int64_t labels  = selected.at<int64_t>(n, 1);
        const int64_t indices = selected.at<int64_t>(n, 2);

        response.annotations.push_back({});
        Annotation &annotation = response.annotations.back();
        annotation.score = scores.at<float>(indices, labels);

        // 边界框格式: xyxy
        const auto b0 = bboxes.at<float>(indices, 0) / scale_xy;
        const auto b1 = bboxes.at<float>(indices, 1) / scale_xy;
        const auto b2 = bboxes.at<float>(indices, 2) / scale_xy;
        const auto b3 = bboxes.at<float>(indices, 3) / scale_xy;
        const auto x1 = static_cast<int32_t>(std::round(std::max(0.0f, std::min(b0, 1.0f * image_embedding.image_w))));
        const auto y1 = static_cast<int32_t>(std::round(std::max(0.0f, std::min(b1, 1.0f * image_embedding.image_h))));
        const auto x2 = static_cast<int32_t>(std::round(std::max(0.0f, std::min(b2, 1.0f * image_embedding.image_w))));
        const auto y2 = static_cast<int32_t>(std::round(std::max(0.0f, std::min(b3, 1.0f * image_embedding.image_h))));
        annotation.bounding_box = {x1, y1, x2, y2};
        SPDLOG_INFO("===> indices: {}, confidence: {},  bbox: ({:.3f}, {:.3f}), ({:.3f}, {:.3f}),  bbox: ({}, {}), ({}, {})",
                 indices, annotation.score, b0, b1, b2, b3, x1, y1, x2, y2);
    }

    return response;
}

cv::Mat YoloWorld::transform_image(const cv::Mat &image, int32_t input_size, int32_t &offset_y_, int32_t &offset_x_) {
    int32_t image_h_ = image.rows;
    int32_t image_w_ = image.cols;
    SPDLOG_INFO("transform image size: {}", image.size);

    // 图像预处理方法: 计算缩放比例, 取其中较小的一侧以保持原图的宽高比.
    float scale_xy_ = std::min(1.0 * input_size / image.rows, 1.0 * input_size / image.cols);
    const auto scaled_h = static_cast<int32_t>(image.rows * scale_xy_);
    const auto scaled_w = static_cast<int32_t>(image.cols * scale_xy_);
    offset_y_ = 1*(input_size - scaled_h);
    offset_x_ = 1*(input_size - scaled_w);
    SPDLOG_INFO("transform scale_xy: {} offset_x: {} offset_y: {}", scale_xy_, offset_x_, offset_y_);

    // cv::INTER_AREA适用于缩小图像, 放大图像可能得到非预期的结果.
    // 如果需要放大图像, 应该使用cv::INTER_LINEAR或cv::INTER_CUBIC.
    cv::Mat scale;
    cv::resize(scale, scale, cv::Size(scaled_w, scaled_h), 0, 0, cv::INTER_LINEAR);
    SPDLOG_INFO("transform scale image size: {}", scale.size);

    // src: 输入图像
    // dst: 输出图像
    // top: 上侧扩展像素数
    // bottom: 下侧扩展像素数
    // left: 左侧扩展像素数
    // right: 右侧扩展像素数
    // borderType: 图像边界扩展模式
    // value: 边缘填充值, 默认值Scalar(0,0,0)
    cv::copyMakeBorder(scale, scale, 0, offset_y_, 0, offset_x_, cv::BORDER_CONSTANT, cv::Scalar(114,114,114));

    // image: 输入图像, 灰度图或三通道图(一般为BGR).
    // blob: 输出4维矩阵, 符合模型输入的NCHW格式. [1, C, H, W]
    // scalefactor: 缩放因子, 图像像素值的缩放比例; 图像像素减去平均值之后, 再进行缩放, 默认值是1.
    // size: 目标尺寸, 模型输入的图片尺寸.
    // mean: 图像要减去均值, 如果需要对BGR图片的三个通道分别减去不同的值, 可以使用3个值; 如果三通道图像只有1个值, 那么三个通道都减去相同的值.
    // swapRB: OpenCV中图片通道顺序是BGR, 但是假设输入顺序是RGB, 处理时可以同步转换为RGB格式, 那么就要使swapRB=true.
    // crop: 是否裁剪, 调整尺寸时是保持比例并裁剪(非拉伸), 如果crop裁剪为true, 则调整输入图像的大小, 使调整大小后的一侧等于相应的尺寸, 另一侧等于或大于, 然后从中心进行裁剪; 如果crop裁剪为false, 则直接调整大小而不进行裁剪并保留纵横比.
    // ddepth: 输出数据类型, 通常为CV_32F或CV_8U.
    cv::Mat blob;
    cv::dnn::blobFromImage(scale, blob, 1.0/255.0, cv::Size(input_size, input_size), cv::Scalar(), false, false, CV_32F);
    SPDLOG_INFO("transform final image size: {}", scale.size);
    return blob;
}

cv::Mat YoloWorld::transform_boxes(const cv::Mat &boxes, int32_t input_size, int32_t image_h, int32_t image_w, int32_t offset_y, int32_t offset_x) {
    if (boxes.empty() || boxes.cols != 4) {
        throw std::runtime_error("输入边界框必须为N行4列的矩阵");
    }

    // 0.创建结果矩阵
    cv::Mat result = boxes.clone();

    // 1.减去边缘填充
    result.col(0) = result.col(0) - offset_x;  // x1
    result.col(1) = result.col(1) - offset_y;  // y1
    result.col(2) = result.col(2) - offset_x;  // x2
    result.col(3) = result.col(3) - offset_y;  // y2

    // 2.缩放回推理图
    const float scale_xy = 1.0 * input_size / std::max(image_h, image_w);
    result = result / scale_xy;

    // 3.裁剪原图范围
    const auto clip = [](cv::Mat src, auto minVal, auto maxVal) {
        cv::min(src, maxVal, src);
        cv::max(src, minVal, src);
    };
    clip(result.col(0), 0, image_w);
    clip(result.col(1), 0, image_h);
    clip(result.col(2), 0, image_w);
    clip(result.col(3), 0, image_h);

    return result;
}