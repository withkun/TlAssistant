#include "shape_to_json.h"
#include "common/base64.h"

#include <QBuffer>

namespace nlohmann {
void to_json(nlohmann::json &json, const QString &value) {
    json = value.toStdString();
}

void from_json(const nlohmann::json &json, QString &value) {
    if (json.is_string() && !json.is_null()) {
        value = QString::fromStdString(json.get<std::string>());
    }
}

void to_json(nlohmann::json &json, const TlShape &shape) {
    std::vector<std::vector<double>> shape_points;
    for (const auto &pnt : shape.points_) {
        shape_points.push_back({pnt.x(), pnt.y()});
    }

    json["label"]       = shape.label_;
    json["points"]      = shape_points;
    if (shape.group_id_ == None) {
        json["group_id"]= nullptr;
    } else {
        json["group_id"]= shape.group_id_;
    }
    json["description"] = shape.description_;
    json["shape_type"]  = shape.shape_type_;
    json["flags"]       = nullptr;  //shape.flags_ ;

    if (shape.mask_.empty()) {
        json["mask"]    = nullptr;
    } else {
        //QByteArray imageData;
        //QBuffer buffer(&imageData);
        //buffer.open(QIODevice::WriteOnly);
        //shape.mask_.save(&buffer);
        //json["mask"]    = base64::b64encode(imageData);
        json["mask"]    = base64::mat_to_img_b64(shape.mask_);
    }
}

void from_json(const nlohmann::json &json, TlShape &shape) {
try {
    shape.label_ = json["label"].get<QString>();
    if (json.contains("group_id") && !json["group_id"].is_null()) {
        shape.group_id_ = json["group_id"].get<int32_t>();
    }
    if (shape.group_id_ == -1) { shape.group_id_ = None; }

    if (json.contains("description") && !json["description"].is_null()) {
        shape.description_ = json["description"].get<QString>();
    }
    shape.shape_type_  = json["shape_type"].get<QString>();
    //shape.flags_       = json["flags"].get<std::string>();

    for (const auto &[x, y] : json["points"].get<std::vector<std::pair<float, float>>>()) {
        shape.points_.push_back({x, y});
    }

    if (json.contains("mask") && !json["mask"].is_null()) {
        const auto mask = json["mask"].get<std::string>();
        //QByteArray byteArray;
        //byteArray.fromStdString(base64::b64decode(mask));
        //shape.mask_.loadFromData(byteArray);
        shape.mask_ = base64::img_b64_to_mat(mask);
    }
    shape.close();
} catch (const std::exception &e) {
    std::cerr << e.what() << std::endl;
    throw;
}
}


void to_json(nlohmann::ordered_json &json, const QString &value) {
    json = value.toStdString();
}

void from_json(const nlohmann::ordered_json &json, QString &value) {
    value = QString::fromStdString(json.get<std::string>());
}

void to_json(nlohmann::ordered_json &json, const TlShape &shape) {
    std::vector<std::vector<double>> shape_points;
    for (const auto &pnt : shape.points_) {
        shape_points.push_back({pnt.x(), pnt.y()});
    }
try {
    json["label"]       = shape.label_;
    json["points"]      = shape_points;
    if (shape.group_id_ == None) {
        json["group_id"] = std::nullptr_t();
    } else {
        json["group_id"] = shape.group_id_;
    }
    json["description"] = shape.description_;
    json["shape_type"]  = shape.shape_type_;
    json["flags"]       = nlohmann::json({});  //shape.flags_ ;

    if (shape.mask_.empty()) {
        json["mask"]    = std::nullptr_t();
    } else {
        //QByteArray imageData;
        //QBuffer buffer(&imageData);
        //buffer.open(QIODevice::WriteOnly);
        //shape.mask_.save(&buffer);
        //json["mask"]    = base64::b64encode(imageData);
        json["mask"]    = base64::mat_to_img_b64(shape.mask_);
    }
} catch (const std::exception &e) {
    std::cerr << e.what() << std::endl;
    throw;
}
}

void from_json(const nlohmann::ordered_json &json, TlShape &shape) {
try {
    shape.label_ = json["label"].get<QString>();
    if (json["group_id"].is_number()) {
        shape.group_id_ = json["group_id"].get<int32_t>();
    }
    if (shape.group_id_ == -1) { shape.group_id_ = None; }

    if (json.contains("description") && !json["description"].is_null()) {
        shape.description_ = json["description"].get<QString>();
    }
    shape.shape_type_   = json["shape_type"].get<QString>();
    //shape.flags_        = json["flags"].get<std::string>();

    const auto points = json["points"].get<std::vector<std::vector<float>>>();
    for (const auto &pnt : points) {
        shape.points_.push_back({pnt[0], pnt[1]});
    }

    if (json.contains("mask") && !json["mask"].is_null()) {
        const auto mask = json["mask"].get<std::string>();
        //QByteArray byteArray;
        //byteArray.fromStdString(base64::b64decode(mask));
        //shape.mask_.loadFromData(byteArray);
        shape.mask_ = base64::img_b64_to_mat(mask);
    }
} catch (const std::exception &e) {
    std::cerr << e.what() << std::endl;
    throw;
}
}
} // namespace nlohmann