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

void validate_shape_semantics(
    const QString &shape_type,
    const QList<QPointF> &points,
    const cv::Mat &mask
) {
    // Only invariants the GUI needs before Shape construction are checked.
    // Degenerate geometry (zero-extent rectangle, coincident points, 2-point
    // polygons) and mask/bbox extent drift can be produced by vertex edits and
    // fractional drags in released versions, so rejecting them would make
    // legitimately saved files unopenable.
    if (!ShapeType.contains(shape_type))
        throw std::invalid_argument("shape_type is unsupported: {shape_type!r}");
    bool coordinates_are_finite = false;
    try {
        coordinates_are_finite = std::ranges::all_of(points, [](const auto &p) {
            return std::isfinite(p.x()) && std::isfinite(p.y());
        });
    } catch (...) {
        coordinates_are_finite = false;
    }
    if (!coordinates_are_finite)
        throw std::invalid_argument("points must contain finite coordinates: {points}");

    const QMap<QString, int> EXACT_POINT_COUNT_BY_SHAPE_TYPE {
        { "point",  1},
        { "rectangle",  2},
        { "line",  2},
        { "circle",  2},
        { "mask",  2},
        { "oriented_rectangle",  4},
    };
    const auto expected_point_count = EXACT_POINT_COUNT_BY_SHAPE_TYPE.value(shape_type, None);
    if (expected_point_count != None && points.size() != expected_point_count) {
        const auto noun = expected_point_count == 1 ? "point" : "points";
        throw std::invalid_argument(
            "points must contain exactly {expected_point_count} {noun} for "
            "shape_type={shape_type!r}: {points}"
        );
    }

    if (shape_type != "mask") {
        if (!mask.empty())
            throw std::invalid_argument(
                "mask is only supported for shape_type='mask', got {shape_type!r}"
            );
        return;
    }
    if (mask.empty())
        throw std::invalid_argument("mask is required for shape_type='mask'");
    if (mask.type() != CV_8UC1)
        throw std::invalid_argument("mask must decode to a 2D image, got shape {mask.shape}");
}

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

    QMap<QString, bool> flags = validate_flags(shape_json_obj["flags"]);

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

    QMap<QString, QByteArray> other_data; // = {k: v for k, v in shape_json_obj.items() if k not in SHAPE_KEYS}
    for (const auto &it : shape_json_obj.items()) {
        if (SHAPE_KEYS.contains(it.key())) {
            continue;
        }
        //loaded.other_data[it.key()] = it.value();
    }

    ShapeDict loaded{
        .label_=label,
        .points_=points,
        .shape_type_=shape_type,
        .flags_=flags,
        .description_=description,
        .group_id_=group_id,
        .mask_=mask,
        .other_data_=other_data,
    };
    //if set(loaded.keys()) != SHAPE_KEYS | {"other_data"}:
    //    raise RuntimeError(
    //        f"unexpected keys: {set(loaded.keys())} != {SHAPE_KEYS | {'other_data'}}"
    //    )
    return loaded;
}

//def _dump_shape_to_json_obj(shape: ShapeDict) -> dict[str, Any]:
//    json_obj: dict[str, Any] = dict(shape["other_data"])
//    json_obj.update(
//        label=shape["label"],
//        points=[list(point) for point in shape["points"]],
//        group_id=shape["group_id"],
//        description=shape["description"],
//        shape_type=shape["shape_type"],
//        flags=shape["flags"],
//        mask=None
//        if shape["mask"] is None
//        else _utils.img_arr_to_b64(shape["mask"].astype(np.uint8)),
//    )
//    return json_obj
//
//
//class LabelFileError(Exception):
//    """Base for read/write failures of labelme JSON annotation files."""
//
//
//class LabelFileReadError(LabelFileError):
//    """Wraps an underlying parse or image-decode failure during load."""
//
//
//class LabelFileWriteError(LabelFileError):
//    """Wraps an underlying I/O failure during save."""
//
//
//@dataclass(frozen=True)
//class Annotation:
//    image_path: str
//    image_data: bytes
//    shapes: list[ShapeDict]
//    flags: dict[str, bool]
//    other_data: dict[str, Any]


const QString LABEL_FILE_SUFFIX = ".json";

const QSet<QString> RESERVED_TOP_LEVEL_KEYS = {
    "version",
    "imageData",
    "imagePath",
    "shapes",
    "flags",
    "imageHeight",
    "imageWidth",
};


bool is_label_file_path(const QString &filename) {
    const std::filesystem::path fs(filename.toStdString());
    const auto extension = QString::fromStdString(fs.extension().string());      // 包含.的后缀, 如: .json
    return extension.toLower() == LABEL_FILE_SUFFIX;
}

QByteArray _read_image_file(const QString &filename);
QByteArray read_image_file(const QString &filename) {
    try {
        return _read_image_file(filename);
    } catch (const std::system_error &e) {
        throw e;
    } catch (const std::exception &e) {
        throw std::invalid_argument(std::format("failed to read image {}: {}", filename.toStdString(), e.what()));
    }
}

QByteArray _read_image_file(const QString &filename) {
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
    //        has_transparency = "A" in oriented.mode or (
    //            oriented.mode == "P" and "transparency" in oriented.info
    //        )
    //        fmt = "PNG" if has_transparency else "JPEG"
    //        if fmt == "JPEG" and oriented.mode == "P":
    //            oriented = oriented.convert("RGB")
    //        elif fmt == "PNG" and oriented.mode == "PA":
    //            oriented = oriented.convert("RGBA")
    //        oriented.save(fp=f, format=fmt, quality=95)
    //        f.seek(0)
    //        image_data = f.read()
    //
    //logger.debug(
    //    "Loaded image file: {!r} in {:.0f}ms", filename, (time.time() - t_start) * 1000
    //)
    QByteArray image_data;
    QBuffer buffer(&image_data);
    buffer.open(QIODevice::WriteOnly);
    QImage(filename).save(&buffer, "PNG");
    return image_data;
}

void check_image_dimensions(
    const QByteArray &image_data,
    int32_t expected_height,
    int32_t expected_width
) {
    if (expected_height == 0 && expected_width == 0)
        return;
    //if expected_height is not None and (
    //    isinstance(expected_height, bool) or not isinstance(expected_height, int)
    //):
    //    raise TypeError(f"imageHeight must be int: {expected_height}")
    //if expected_width is not None and (
    //    isinstance(expected_width, bool) or not isinstance(expected_width, int)
    //):
    //    raise TypeError(f"imageWidth must be int: {expected_width}")
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
        nlohmann::json raw;
        ifs >> raw;
        ifs.close();

        image_path = QString::fromStdString(raw["imagePath"].get<std::string>());
        if (raw.contains("imageData") && !raw["imageData"].is_null() && !raw["imageData"].get<std::string>().empty()) {
            image_data = QByteArray::fromStdString(base64::b64decode(raw["imageData"].get<std::string>()));
        } else {
            image_data = read_image_file(
                QFileInfo(filename).absolutePath() + "/" +  image_path
            );
        }
        check_image_dimensions(
            image_data,
            raw["imageHeight"].get<int32_t>(),
            raw["imageWidth"].get<int32_t>()
        );
        shapes = raw["shapes"].items() | std::views::transform([](auto &it) { return load_shape_json_obj(it.value()); }) | std::ranges::to<QList<ShapeDict>>();
        //for shape_index, shape_json_obj in enumerate(raw["shapes"]):
        //    if not isinstance(shape_json_obj, dict):
        //        raise TypeError(f"shapes[{shape_index}] must be dict: {shape_json_obj}")
        //    try:
        //        shape = _load_shape_json_obj(shape_json_obj=shape_json_obj)
        //    except (TypeError, ValueError, RuntimeError) as e:
        //        raise ValueError(f"shapes[{shape_index}]: {e}") from e
        //    shapes.append(shape)
        flags = validate_flags(raw["flags"]);
    } catch (
        //OSError,
        //json.JSONDecodeError,
        //KeyError,
        //TypeError,
        //ValueError,
        //RuntimeError,
        std::exception &e) {
        throw LabelFileReadError("failed to load " + filename.toStdString());
    }
    //other_data = {k: v for k, v in raw.items() if k not in RESERVED_TOP_LEVEL_KEYS}
    return AnnotationEx{
        .image_path_=image_path,
        .image_data_=image_data,
        .shapes_=shapes,
        .flags_=flags,
        .other_data_=other_data,
    };
}

void write_label_file(
    const QString &filename,
    const AnnotationEx &annotation,
    const int32_t image_height,
    const int32_t image_width,
    const bool save_image_data
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
        nlohmann::ordered_json payload = {
            {"version", _version_},
            {"flags", nlohmann::json::object()},
            {"shapes", std::vector<ShapeDict>{annotation.shapes_.begin(), annotation.shapes_.end()}},
            {"imagePath", annotation.image_path_},
            {"imageData", std::nullptr_t()},
            {"imageHeight", image_height},
            {"imageWidth", image_width}
        };
        if (!annotation.flags_.empty()) {
            payload["flags"] = annotation.flags_.toStdMap();
        }
        if (!image_data_b64.empty()) {
            payload["imageData"] = image_data_b64;
        }
        for (const auto &[key, value] : annotation.other_data_.toStdMap()) {
            if (RESERVED_TOP_LEVEL_KEYS.contains(key))
                throw std::invalid_argument("reserved key in other_data: {key!r}");
            payload[key.toStdString()] = value.toStdString();
        }
        // A failed save must leave the previous file intact, so write next to
        // it and rename over it only once the temporary file closed cleanly.
        // Windows cannot represent POSIX modes, so preservation is POSIX-only.
        //try:
        //    existing_mode = (
        //        None if os.name == "nt" else stat.S_IMODE(os.stat(filename).st_mode)
        //    )
        //except FileNotFoundError:
        //    existing_mode = None
        //temporary_path = Path(f"{filename}.tmp")
        try {
            if (std::ofstream ofs(filename.toLocal8Bit()); ofs.is_open()) {
                ofs.width(2);
                ofs << payload;
                ofs.close();
            }
        } catch (...) {
        }
    } catch (const std::exception &e) {
        throw LabelFileWriteError("failed to write " + filename.toStdString() + ": " + e.what());
    }
}


//_DISPLAYABLE_MODES = {"1", "L", "P", "RGB", "RGBA", "LA", "PA"}
//
//
//def _imread(filename: str) -> PIL.Image.Image:
//    ext: str = Path(filename).suffix.lower()
//    try:
//        image_pil = PIL.Image.open(filename)
//        if image_pil.mode not in _DISPLAYABLE_MODES:
//            raise PIL.UnidentifiedImageError
//        return image_pil
//    except PIL.UnidentifiedImageError:
//        if ext in (".tif", ".tiff"):
//            return _imread_tiff(filename)
//        raise
//
//
//def _imread_tiff(filename: str) -> PIL.Image.Image:
//    img_arr: NDArray = tifffile.imread(filename)
//
//    if img_arr.ndim == 2:
//        img_arr_normalized = _normalize_to_uint8(img_arr)
//    elif img_arr.ndim == 3:
//        if img_arr.shape[2] >= 3:
//            img_arr_normalized = np.stack(
//                [_normalize_to_uint8(img_arr[:, :, i]) for i in range(3)],
//                axis=2,
//            )
//        else:
//            img_arr_normalized = _normalize_to_uint8(img_arr[:, :, 0])
//    else:
//        raise OSError(f"Unsupported image shape: {img_arr.shape}")
//
//    return PIL.Image.fromarray(img_arr_normalized)
//
//
//def _normalize_to_uint8(arr: NDArray) -> NDArray[np.uint8]:
//    arr = arr.astype(np.float64)
//    finite = arr[np.isfinite(arr)]
//    if finite.size == 0:
//        return np.zeros(arr.shape, dtype=np.uint8)
//    min_val = finite.min()
//    max_val = finite.max()
//    if max_val - min_val == 0:
//        return np.zeros(arr.shape, dtype=np.uint8)
//    normalized = (arr - min_val) / (max_val - min_val) * 255
//    bounded = np.nan_to_num(np.clip(normalized, 0, 255), nan=0.0)
//    return bounded.astype(np.uint8)