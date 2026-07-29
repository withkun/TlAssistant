#include "tl_label_file.h"

#include "common/base64.h"
#include "shape_to_json.h"
#include "spdlog/spdlog.h"

#include <fstream>
#include <filesystem>

#include <QBuffer>
#include <QFileInfo>


QString LabelFile::suffix = ".json";

ShapeDict load_shape_json_obj(const nlohmann::json &shape_json_obj) {
    std::set<std::string> SHAPE_KEYS = {
        "label",
        "points",
        "group_id",
        "shape_type",
        "flags",
        "description",
        "mask",
    };

    ShapeDict loaded;
    if (!shape_json_obj.contains("label")) {
        std::cerr << "[load shape json] label is required" << std::endl;
        throw std::invalid_argument("label is required: {shape_json_obj}");
    }
    if (!shape_json_obj["label"].is_string()) {
        std::cerr << "[load shape json] label mast be string" << std::endl;
        throw std::invalid_argument("label must be str: {shape_json_obj['label']}");
    }
    loaded.label = shape_json_obj["label"].get<QString>();

    if (!shape_json_obj.contains("points")) {
        std::cerr << "[load shape json] points is required: " << shape_json_obj["label"] << std::endl;
        throw std::invalid_argument("points is required: {shape_json_obj}");
    }
    if (!shape_json_obj["points"].is_array()) {
        std::cerr << "[load shape json] points must be list: " << shape_json_obj["label"] << std::endl;
        throw std::invalid_argument("points must be list: {shape_json_obj['points']}");
    }
    if (shape_json_obj["points"].empty()) {
        std::cerr << "[load shape json] points must be non-empty: " << shape_json_obj["label"] << std::endl;
        throw std::invalid_argument("points must be non-empty: {shape_json_obj}");
    }
    for (const auto &pnt : shape_json_obj["points"].get<std::vector<std::vector<float>>>()) {
        if (pnt.size() != 2) {
            std::cerr << "[load shape json] points must be list of [x, y]: " << shape_json_obj["label"] << std::endl;
            throw std::invalid_argument("points must be list of [x, y]: {shape_json_obj['points']}");
        }
        loaded.points.push_back({pnt[0], pnt[1]});
    }

    if (!shape_json_obj.contains("shape_type")) {
        std::cerr << "[load shape json] shape_type is required: " << shape_json_obj["label"] << std::endl;
        throw std::invalid_argument("shape_type is required: {shape_json_obj}");
    }
    if (!shape_json_obj["shape_type"].is_string()) {
        std::cerr << "[load shape json] shape_type mast be string: " << shape_json_obj["label"] << std::endl;
        throw std::invalid_argument("shape_type must be str: {shape_json_obj['shape_type']}");
    }
    loaded.shape_type = QString::fromStdString(shape_json_obj["shape_type"].get<std::string>());

    //flags: dict = {}
    //if (shape_json_obj.contains("flags") && !shape_json_obj["flags"].is_null()) {
    //    assert isinstance(shape_json_obj["flags"], dict), (
    //        f"flags must be dict: {shape_json_obj['flags']}"
    //    )
    //    assert all(
    //        isinstance(k, str) and isinstance(v, bool)
    //        for k, v in shape_json_obj["flags"].items()
    //    ), f"flags must be dict of str to bool: {shape_json_obj['flags']}"
    //    flags = shape_json_obj["flags"]
    //}

    std::string description;
    if (shape_json_obj.contains("description") && !shape_json_obj["description"].is_null()) {
        if (!shape_json_obj["description"].is_string()) {
            std::cerr << "[load shape json] description mast be string: " << shape_json_obj["label"] << std::endl;
            throw std::invalid_argument("description must be str: {shape_json_obj['description']}");
        }
        loaded.description = QString::fromStdString(shape_json_obj["description"]);
    }

    loaded.group_id = None;
    if (shape_json_obj.contains("group_id") && !shape_json_obj["group_id"].is_null()) {
        if (!shape_json_obj["group_id"].is_number()) {
            std::cerr << "[load shape json] group_id mast be integer: " << shape_json_obj["label"] << std::endl;
            throw std::invalid_argument("group_id must be int: {shape_json_obj['group_id']}");
        }
        loaded.group_id = shape_json_obj["group_id"];
        if (loaded.group_id == -1) { loaded.group_id = None; }
    }

    //mask: NDArray[np.bool] | None = None
    if (shape_json_obj.contains("mask") && !shape_json_obj["mask"].is_null()) {
        if (!shape_json_obj["mask"].is_string()) {
            std::cerr << "[load shape json] mask must be base64-encoded: " << shape_json_obj["label"] << std::endl;
            throw std::invalid_argument("mask must be base64-encoded PNG: {shape_json_obj['mask']}");
        }
        loaded.mask = utils::img_b64_to_arr(shape_json_obj["mask"]);
    }

    //other_data = {k: v for k, v in shape_json_obj.items() if k not in SHAPE_KEYS}
    for (const auto &it : shape_json_obj.items()) {
        if (SHAPE_KEYS.contains(it.key())) {
            continue;
        }

        //loaded.other_data[it.key()] = it.value();
    }

    //loaded : ShapeDict = ShapeDict(
    //    label=label,
    //    points=points,
    //    shape_type=shape_type,
    //    flags=flags,
    //    description=description,
    //    group_id=group_id,
    //    mask=mask,
    //    other_data=other_data,
    //);
    //assert set(loaded.keys()) == SHAPE_KEYS | {"other_data"}
    return loaded;
}


LabelFile::LabelFile(const QString &filename) {
    shapes_ = {};
    imagePath_ = {};
    imageData_ = {};
    if (!filename.isEmpty()) {
        load(filename);
    }
    filename_ = filename;
}

//@staticmethod
QByteArray LabelFile::load_image_file(const QString &filename) {
    QByteArray imageData;
    try {
        QImage image;
        image.load(filename);

        std::string format("PNG");
        QFileInfo fileInfo(filename);
        if ((fileInfo.filesystemPath().extension() == ".jpg") || (fileInfo.filesystemPath().extension() == ".jpeg")) {
            format = "JPEG";
        }
        QBuffer buffer(&imageData);
        buffer.open(QIODevice::WriteOnly);
        image.save(&buffer, format.data());
    } catch (...) {
        SPDLOG_ERROR("Failed opening image file: {}", filename);
    }

    return imageData;
}

void LabelFile::load(const QString &filename) {
    QSet<QString> keys = {
        "version",
        "imageData",
        "imagePath",
        "shapes",  // polygonal annotations
        "flags",  // image level flags
        "imageHeight",
        "imageWidth",
    };
    QSet<QString> shape_keys = {
        "label",
        "points",
        "group_id",
        "shape_type",
        "flags",
        "description",
        "mask",
    };

    QString    imagePath;
    QByteArray imageData;
    try {
        std::ifstream ifs(filename.toLocal8Bit());
        if (!ifs.is_open()) {
            return;
        }
        nlohmann::json data;
        ifs >> data;
        ifs.close();

        // Normalize Windows-style backslash paths to POSIX forward slashes
        if (data.contains("imagePath") && data["imagePath"].is_string()) {
            imagePath = QString::fromStdString(data["imagePath"].get<std::string>());
        }

        if (data.contains("imageData") && !data["imageData"].is_null() && !data["imageData"].get<std::string>().empty()) {
            imageData = QByteArray::fromStdString(base64::b64decode(data["imageData"].get<std::string>()));
        } else {
            // relative path from label file to relative path from cwd
            imageData = load_image_file(QFileInfo(filename).absolutePath() + "/" +  imagePath);
        }

        std::string flags;
        if (data.contains("flags")) {
            //flags = data["flags"];  // {}对象类型, 暂时未知.
        }

        check_image_height_and_width(
            imageData,
            data["imageHeight"].get<int32_t>(),
            data["imageWidth"].get<int32_t>()
        );

        for (const auto &item : data["shapes"].items()) {
            const auto &s = item.value();
            try {
                auto shape = s.get<TlShape>();
                shapes_.push_back(shape);
            } catch (const std::exception &e) {
                std::cerr << e.what() << std::endl;
                throw;
            }

            try {
                shapes1_.push_back(load_shape_json_obj(item.value()));
            } catch (const std::exception &e) {
                std::cerr << e.what() << std::endl;
                throw;
            }
        }
    } catch (const std::exception &e) {
        throw LabelFileError();
    }

    //otherData = {}
    //for (key, value in data.items()) {
    //    if (key not in keys) {
    //        otherData[key] = value;
    //    }
    //}
    // Only replace data after everything is loaded.
    //flags_ = flags;
    //shapes_ = shapes;
    imagePath_ = imagePath;
    imageData_ = imageData;
    filename_  = filename;
    //otherData_ = otherData;
}

//@staticmethod
std::pair<int32_t, int32_t> LabelFile::check_image_height_and_width(const QByteArray &imageData, int32_t imageHeight, int32_t imageWidth) {
    const auto image = QImage::fromData((uchar *)imageData.data(), imageData.size());
    int32_t actual_w = image.width(), actual_h = image.height();
    if (imageHeight != -1 &&  actual_h != imageHeight) {
        SPDLOG_ERROR(
            "imageHeight does not match with imageData or imagePath, "
            "so getting imageHeight from actual image."
        );
        imageHeight = actual_h;
    }
    if (imageWidth != -1 && actual_w != imageWidth) {
        SPDLOG_ERROR(
            "imageWidth does not match with imageData or imagePath, "
            "so getting imageWidth from actual image."
        );
        imageWidth = actual_w;
    }
    return {imageHeight, imageWidth};
}

void LabelFile::save(const QString &filename,
                     const QList<TlShape> &shapes,
                     const QString &imagePath,
                     const QByteArray &imageData,
                     int32_t imageHeight,
                     int32_t imageWidth,
                     const QString &otherData,
                     const QMap<QString, bool> &flags) {
    std::string imageBase64;
    if (!imageData.isEmpty()) {
        imageBase64 = base64::b64encode(imageData);
        auto [fst, snd] = check_image_height_and_width(imageData, imageHeight, imageWidth);
        imageHeight = fst;
        imageWidth = snd;
    }
    //if (otherData is None) {
    //    otherData = {};
    //}
    //if (flags is None) {
    //    flags = {};
    //}

    std::vector<TlShape> pnt_list(shapes.begin(), shapes.end());

    nlohmann::ordered_json data;
    data["version"]     = "5.11.4";

    data["flags"]       = nlohmann::json({}); //flags;
    data["shapes"]      = pnt_list;
    data["imagePath"]   = imagePath;
    if (imageBase64.empty()) {
        data["imageData"] = std::nullptr_t();
    } else {
        data["imageData"] = imageBase64;
    }
    data["imageHeight"] = imageHeight;
    data["imageWidth"]  = imageWidth;

    //for (key, value in otherData.items()) {
    //    assert key not in data;
    //    data[key] = value;
    //}
    try {
        std::ofstream ofs(filename.toLocal8Bit());
        if (ofs.is_open()) {
            ofs.width(2);
            ofs << data;
            ofs.close();
        }
        filename_ = filename;
    } catch (const std::exception &e) {
        throw LabelFileError();
    }
}

bool LabelFile::is_label_file(const QString &filename) {
    const std::filesystem::path fs(filename.toStdString());
    const auto extension = QString::fromStdString(fs.extension().string());      // 包含.的后缀, 如: .json
    return extension.toLower() == LabelFile::suffix;
}