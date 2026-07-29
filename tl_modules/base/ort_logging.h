#ifndef __INC_ORT_LOGGING_H
#define __INC_ORT_LOGGING_H

#include "spdlog/spdlog.h"
#include "common/format_cv.h"
#include "onnxruntime_cxx_api.h"


namespace std {
template <>
struct formatter<ONNXTensorElementDataType> : formatter<string_view> {
    auto format(const ONNXTensorElementDataType dt, format_context &ctx) const {
        string_view name = "unknown";
        switch (dt) {
            case ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT:           name = "FLOAT"; break;
            case ONNX_TENSOR_ELEMENT_DATA_TYPE_UINT8:           name = "UINT8"; break;
            case ONNX_TENSOR_ELEMENT_DATA_TYPE_INT8:            name = "INT8"; break;
            case ONNX_TENSOR_ELEMENT_DATA_TYPE_UINT16:          name = "UINT16"; break;
            case ONNX_TENSOR_ELEMENT_DATA_TYPE_INT16:           name = "INT16"; break;
            case ONNX_TENSOR_ELEMENT_DATA_TYPE_INT32:           name = "INT32"; break;
            case ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64:           name = "INT64"; break;
            case ONNX_TENSOR_ELEMENT_DATA_TYPE_STRING:          name = "STRING"; break;
            case ONNX_TENSOR_ELEMENT_DATA_TYPE_BOOL:            name = "BOOL"; break;
            case ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT16:         name = "FLOAT16"; break;
            case ONNX_TENSOR_ELEMENT_DATA_TYPE_DOUBLE:          name = "DOUBLE"; break;
            case ONNX_TENSOR_ELEMENT_DATA_TYPE_UINT32:          name = "UINT32"; break;
            case ONNX_TENSOR_ELEMENT_DATA_TYPE_UINT64:          name = "UINT64"; break;
            case ONNX_TENSOR_ELEMENT_DATA_TYPE_COMPLEX64:       name = "COMPLEX64"; break;
            case ONNX_TENSOR_ELEMENT_DATA_TYPE_COMPLEX128:      name = "COMPLEX128"; break;
            case ONNX_TENSOR_ELEMENT_DATA_TYPE_BFLOAT16:        name = "BFLOAT16"; break;
            case ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT8E4M3FN:    name = "FLOAT8E4M3FN"; break;
            case ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT8E4M3FNUZ:  name = "FLOAT8E4M3FNUZ"; break;
            case ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT8E5M2:      name = "FLOAT8E5M2"; break;
            case ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT8E5M2FNUZ:  name = "FLOAT8E5M2FNUZ"; break;
            case ONNX_TENSOR_ELEMENT_DATA_TYPE_UINT4:           name = "UINT4"; break;
            case ONNX_TENSOR_ELEMENT_DATA_TYPE_INT4:            name = "INT4"; break;
            default:                                            name = "UNDEFINED"; break;
        }
        return formatter<string_view>::format(name, ctx);
    }
};
} //namespace std

//OrtLoggingFunction
//void* param: 用户自定义数据的指针, 可以用来传递上下文信息或配置选项给日志处理函数
//int level: 日志的级别
//const char* category: 日志的类别, 如"onnxruntime", 这有助于区分不同的日志来源或组件
//const char* logid: 与日志消息相关的唯一标识符或标签, 这可以用于跟踪或关联特定的日志事件
//const char* code_location: 产生日志消息的代码位置, 如文件名和行号, 这有助于快速定位问题代码
//const char* message: 实际的日志消息字符串, 包含了要记录的详细信息
void LoggingFunction(void *param, OrtLoggingLevel severity, const char *category, const char *logid, const char *code_location, const char *message);

#endif //__INC_ORT_LOGGING_H