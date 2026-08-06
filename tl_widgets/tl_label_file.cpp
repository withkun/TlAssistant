#include "tl_label_file.h"

#include "mainwindow.h"
#include "common/base64.h"
#include "shape_to_json.h"
#include "spdlog/spdlog.h"

#include <fstream>
#include <filesystem>

#include <QBuffer>
#include <QFileInfo>


QMap<QString, bool> validate_flags(const nlohmann::json &flags) {
    if (flags.is_null())
        return {};
    if (!flags.is_object())
        throw std::invalid_argument("flags must be a JSON object");
    QMap<QString, bool> dict;
    for (auto it = flags.begin(); it != flags.end(); ++it) {
        const std::string &key = it.key();
        const nlohmann::json &value = it.value();
        if (!value.is_boolean())
            throw std::invalid_argument("flags must be dict of str to bool: " + key);
        dict[QString::fromStdString(key)] = value.get<bool>();
    }
    return dict;
}

QString LabelFile::suffix = ".json";

const QString LABEL_FILE_SUFFIX = ".json";

const QSet<QString> RESERVED_TOP_LEVEL_KEYS = {
    "version",
    "imageData",
    "imagePath",
    "shapes",       // polygonal annotations
    "flags",        // image level flags
    "imageHeight",
    "imageWidth",
};

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

    if (!shape_json_obj.contains("label")) {
        std::cerr << "[load shape json] label is required" << std::endl;
        throw std::invalid_argument("label is required: {shape_json_obj}");
    }
    if (!shape_json_obj["label"].is_string()) {
        std::cerr << "[load shape json] label mast be string" << std::endl;
        throw std::invalid_argument("label must be str: {shape_json_obj['label']}");
    }
    QString label = shape_json_obj["label"].get<QString>();

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
    QList<QPointF> points;
    for (const auto &pnt : shape_json_obj["points"].get<std::vector<std::vector<float>>>()) {
        if (pnt.size() != 2) {
            std::cerr << "[load shape json] points must be list of [x, y]: " << shape_json_obj["label"] << std::endl;
            throw std::invalid_argument("points must be list of [x, y]: {shape_json_obj['points']}");
        }
        points.push_back({pnt[0], pnt[1]});
    }

    if (!shape_json_obj.contains("shape_type")) {
        std::cerr << "[load shape json] shape_type is required: " << shape_json_obj["label"] << std::endl;
        throw std::invalid_argument("shape_type is required: {shape_json_obj}");
    }
    if (!shape_json_obj["shape_type"].is_string()) {
        std::cerr << "[load shape json] shape_type mast be string: " << shape_json_obj["label"] << std::endl;
        throw std::invalid_argument("shape_type must be str: {shape_json_obj['shape_type']}");
    }
    QString shape_type = QString::fromStdString(shape_json_obj["shape_type"].get<std::string>());

    QMap<QString, bool> flags; //: dict = {}
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

    QString description;
    if (shape_json_obj.contains("description") && !shape_json_obj["description"].is_null()) {
        if (!shape_json_obj["description"].is_string()) {
            std::cerr << "[load shape json] description mast be string: " << shape_json_obj["label"] << std::endl;
            throw std::invalid_argument("description must be str: {shape_json_obj['description']}");
        }
        description = QString::fromStdString(shape_json_obj["description"]);
    }

    int32_t group_id = None;
    if (shape_json_obj.contains("group_id") && !shape_json_obj["group_id"].is_null()) {
        if (!shape_json_obj["group_id"].is_number()) {
            std::cerr << "[load shape json] group_id mast be integer: " << shape_json_obj["label"] << std::endl;
            throw std::invalid_argument("group_id must be int: {shape_json_obj['group_id']}");
        }
        group_id = shape_json_obj["group_id"];
        if (group_id == -1) { group_id = None; }
    }

    cv::Mat mask;   //: NDArray[np.bool] | None = None
    if (shape_json_obj.contains("mask") && !shape_json_obj["mask"].is_null()) {
        if (!shape_json_obj["mask"].is_string()) {
            std::cerr << "[load shape json] mask must be base64-encoded: " << shape_json_obj["label"] << std::endl;
            throw std::invalid_argument("mask must be base64-encoded PNG: {shape_json_obj['mask']}");
        }
        mask = utils::img_b64_to_arr(shape_json_obj["mask"]);
    }

    QMap<QString, QString> other_data; // = {k: v for k, v in shape_json_obj.items() if k not in SHAPE_KEYS}
    for (const auto &it : shape_json_obj.items()) {
        if (SHAPE_KEYS.contains(it.key())) {
            continue;
        }

        //loaded.other_data[it.key()] = it.value();
    }

    ShapeDict loaded{
        label,
        points,
        shape_type,
        flags,
        description,
        group_id,
        mask,
        other_data,
    };
    //assert set(loaded.keys()) == SHAPE_KEYS | {"other_data"}
    return loaded;
}

nlohmann::ordered_json dump_shape_to_json_obj(const ShapeDict &shape) { // -> dict[str, Any]:
    nlohmann::ordered_json json_obj; //: dict[str, Any] = dict(shape["other_data"])
    //json_obj.update(
    //    label=shape["label"],
    //    points=[list(point) for point in shape["points"]],
    //    group_id=shape["group_id"],
    //    description=shape["description"],
    //    shape_type=shape["shape_type"],
    //    flags=shape["flags"],
    //    mask=None
    //    if shape["mask"] is None
    //    else _utils.img_arr_to_b64(shape["mask"].astype(np.uint8)),
    //)
    return json_obj;
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
    //    if (key not in RESERVED_TOP_LEVEL_KEYS) {
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

bool is_label_file_path(const QString &filename) {
    const std::filesystem::path fs(filename.toStdString());
    const auto extension = QString::fromStdString(fs.extension().string());      // 包含.的后缀, 如: .json
    return extension.toLower() == LabelFile::suffix;
}

QByteArray read_image_file(const QString &filename) {
    //t_start = time.time()
    //image_pil = _imread(filename=filename)
    //
    //oriented: PIL.Image.Image = _utils.apply_exif_orientation(image=image_pil)
    //ext = Path(filename).suffix.lower()
    //if oriented is image_pil and ext in (".jpg", ".jpeg", ".png"):
    //    with open(filename, "rb") as f:
    //        image_data = f.read()
    //else:
    //    with io.BytesIO() as f:
    //        fmt = "PNG" if "A" in oriented.mode else "JPEG"
    //        oriented.save(fp=f, format=fmt, quality=95)
    //        f.seek(0)
    //        image_data = f.read()
    //
    //logger.debug(
    //    "Loaded image file: {!r} in {:.0f}ms", filename, (time.time() - t_start) * 1000
    //)
    //return image_data
    return {};
}

QByteArray load_image_file(const QString &filename) {
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

void check_image_dimensions(
    const QByteArray &image_data,
    int32_t expected_height,
    int32_t expected_width
) {
    if (expected_height == 0 && expected_width == 0)
        return;
    const auto image = QImage::fromData((uchar *)image_data.data(), image_data.size());
    int32_t actual_w = image.width(), actual_h = image.height();
    if (expected_height != 0 && expected_height != actual_h)
        throw std::invalid_argument(
            std::format("imageHeight mismatch: declared={}, actual={}", expected_height, actual_h)
        );
    if (expected_width != 0 && expected_width != actual_w)
        throw std::invalid_argument(
            std::format("imageWidth mismatch: declared={}, actual={}", expected_width, actual_w)
        );
}

AnnotationEx read_label_file(const QString &filename) {
    QString    image_path;
    QByteArray image_data;
    QList<ShapeDict>            shapes;
    QMap<QString, bool>         flags;
    QMap<QString, QByteArray>   other_data;
    try {
        std::ifstream ifs(filename.toLocal8Bit());
        if (!ifs.is_open()) {
            throw OSError("failed to load " + filename.toStdString());
        }
        nlohmann::json raw;
        ifs >> raw;
        ifs.close();

        // Normalize Windows-style backslash paths to POSIX forward slashes
        if (raw.contains("imagePath") && raw["imagePath"].is_string()) {
            image_path = QString::fromStdString(raw["imagePath"].get<std::string>());
        }

        if (raw.contains("imageData") && !raw["imageData"].is_null() && !raw["imageData"].get<std::string>().empty()) {
            image_data = QByteArray::fromStdString(base64::b64decode(raw["imageData"].get<std::string>()));
        } else {
            // relative path from label file to relative path from cwd
            image_data = load_image_file(QFileInfo(filename).absolutePath() + "/" +  image_path);
        }

        check_image_dimensions(
            image_data,
            raw["imageHeight"].get<int32_t>(),
            raw["imageWidth"].get<int32_t>()
        );

        std::ranges::for_each(raw["shapes"].items(), [&](const auto &it) {
            shapes.append(load_shape_json_obj(it.value()));
        });

        flags = validate_flags(raw["flags"]);
    //except (
    //    OSError,
    //    json.JSONDecodeError,
    //    KeyError,
    //    TypeError,
    //    ValueError,
    //    RuntimeError,
    } catch (...) {
        throw LabelFileReadError("failed to load " + filename.toStdString());
    }

    //
    //other_data = {k: v for k, v in raw.items() if k not in RESERVED_TOP_LEVEL_KEYS}
    return AnnotationEx(
        image_path,
        image_data,
        shapes,
        flags,
        other_data
    );
}

void write_label_file(
    const QString &filename,
    const AnnotationEx &annotation,
    int32_t image_height,
    int32_t image_width,
    bool save_image_data
) {
    try {
        std::string image_data_b64;
        if (save_image_data) {
            check_image_dimensions(
                annotation.image_data_,
                image_height,
                image_width
            );
            image_data_b64 = base64::b64encode(annotation.image_data_);
        }
        // JSON keys stay camelCase: changing them would break existing .json files.
        //std::vector<TlShape> pnt_list(annotation.shapes_.begin(), annotation.shapes_.end());
        std::vector<TlShape> shapes;
        std::for_each(annotation.shapes_.begin(), annotation.shapes_.end(), [](auto &shape) { });
        nlohmann::ordered_json payload = {
            {"version", _version_},
            {"flags", nlohmann::json({})},    //dict(annotation.flags) if annotation.flags else {},
            //shapes: [dump_shape_to_json_obj(shape) for shape in annotation.shapes],
            {"imagePath", annotation.image_path_},
            {"imageData", image_data_b64},
            {"imageHeight", image_height},
            {"imageWidth", image_width}
        };
    //    for key, value in annotation.other_data.items():
    //        if key in _RESERVED_TOP_LEVEL_KEYS:
    //            raise ValueError(f"reserved key in other_data: {key!r}")
    //        payload[key] = value
    //    with open(filename, "w", encoding="utf-8") as f:
    //        json.dump(payload, f, ensure_ascii=False, indent=2)
    //} catch (OSError, TypeError, ValueError) as e: {
    //    raise LabelFileWriteError(f"failed to write {filename!r}: {e}") from e
    //}
    } catch (const std::exception &e) {
        throw LabelFileReadError("failed to write " + filename.toStdString() + ": " + e.what());
    }
}