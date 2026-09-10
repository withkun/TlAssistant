#include "tokenizer.h"
#include "zlib.h"

#include <Shlobj.h>
#include <filesystem>
#include <format>
#include <fstream>
#include <sstream>
#include <iostream>
#include <cstdint>
#include <regex>
#include <stdexcept>
#include <string>
#include <vector>


bool readGzipFile(const std::string &filename, std::string &output) {
    std::ifstream file(filename, std::ios::ate | std::ios::binary);
    if (!file) {
        std::cerr << "Failed to open file: " << filename << std::endl;
        return false;
    }
    const std::streamsize compressedSize = file.tellg();
    file.seekg(0, std::ios::beg);

    // 读取整个文件到内存中
    std::vector<char> buffer(compressedSize);
    if (!file.read(buffer.data(), compressedSize)) {
        std::cerr << "Failed to read file: " << filename << std::endl;
        return false;
    }

    // 使用zlib解压数据
    uLongf uncompressedSize = compressedSize*10; // 解压后的数据长度（初始猜测）
    Bytef *uncompressedData = new Bytef[uncompressedSize]; // 分配内存用于存储解压后的数据

    z_stream strm;
    strm.zalloc = Z_NULL;
    strm.zfree = Z_NULL;
    strm.opaque = Z_NULL;
    strm.avail_in = compressedSize; // 输入数据的大小
    strm.next_in = reinterpret_cast<Bytef *>(buffer.data()); // 输入数据的指针
    strm.avail_out = uncompressedSize; // 输出缓冲区的大小
    strm.next_out = uncompressedData; // 输出缓冲区的指针

    // 初始化解压缩流
    if (inflateInit2(&strm, 16+MAX_WBITS) != Z_OK) { // 使用16+MAX_WBITS来自动检测gzip或zlib格式
    //if (inflateInit(&strm) != Z_OK) {
        std::cerr << "inflateInit failed" << std::endl;
        return false;
    }

    // 解压缩数据
    const int ret = inflate(&strm, Z_FINISH);
    if (ret != Z_STREAM_END) {
        std::cerr << "inflate failed: " << ret << std::endl;
        inflateEnd(&strm);
        delete[] uncompressedData;
        return false;
    }
    output = std::string(uncompressedData, uncompressedData + uncompressedSize); // 将解压后的数据转换为字符串

    // 清理资源
    inflateEnd(&strm);
    delete[] uncompressedData; // 释放内存
    return true;
}

std::string value_to_utf8(const int32_t value) {
    if (value <= 0x7f) {       // 0xxxxxxx
        return std::string() + static_cast<char>(value);
    }
    if (value <= 0x7ff) {      // 110xxxxx 10xxxxxx
        return std::string() + static_cast<char>(0xc0 | ((value >>  6)       )) +
                               static_cast<char>(0x80 | ((value      ) & 0x3f));
    }
    if (value <= 0xffff) {     // 1110xxxx 10xxxxxx 10xxxxxx
        return std::string() + static_cast<char>(0xe0 | ((value >> 12)       )) +
                               static_cast<char>(0x80 | ((value >>  6) & 0x3f)) +
                               static_cast<char>(0x80 | ((value      ) & 0x3f));
    }
    if (value <= 0x10ffff) {   // 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx
        return std::string() + static_cast<char>(0xf0 | ((value >> 18)       )) +
                               static_cast<char>(0x80 | ((value >> 12) & 0x3f)) +
                               static_cast<char>(0x80 | ((value >>  6) & 0x3f)) +
                               static_cast<char>(0x80 | ((value      ) & 0x3f));
    }
    throw std::out_of_range(std::format("Invalid unicode code value: {}", value));
}

// UTF-8解码器(含错误替换功能)
std::string utf8_replace_errors(const std::vector<uint8_t> &bytes) {
    std::string result;
    const std::string replacement = "\uFFFD"; // U+FFFD 替换字符
    for (size_t i = 0; i < bytes.size(); ) {
        uint8_t lead = bytes[i];
        // 1字节序列 (0xxxxxxx)
        if ((lead & 0x80) == 0) {
            result += lead;
            i++;
        }
        // 2字节序列 (110xxxxx 10xxxxxx)
        else if ((lead & 0xE0) == 0xC0 && i+1 < bytes.size()) {
            uint16_t code = ((lead & 0x1F) << 6) | (bytes[i+1] & 0x3F);
            if (code >= 0x80) {
                result += char(code);
                i += 2;
            } else {
                result += replacement;
                i++;
            }
        }
        // 3字节序列 (1110xxxx 10xxxxxx 10xxxxxx)
        else if ((lead & 0xF0) == 0xE0 && i+2 < bytes.size()) {
            uint32_t code = ((lead & 0x0F) << 12) |
                            ((bytes[i+1] & 0x3F) << 6) |
                            (bytes[i+2] & 0x3F);
            if (code >= 0x800) {
                result += char(code);
                i += 3;
            } else {
                result += replacement;
                i++;
            }
        }
        // 4字节序列 (11110xxx 10xxxxxx 10xxxxxx 10xxxxxx)
        else if ((lead & 0xF8) == 0xF0 && i+3 < bytes.size()) {
            uint32_t code = ((lead & 0x07) << 18) |
                            ((bytes[i+1] & 0x3F) << 12) |
                            ((bytes[i+2] & 0x3F) << 6) |
                            (bytes[i+3] & 0x3F);
            if (code >= 0x10000 && code <= 0x10FFFF) {
                // C++标准字符串不支持4字节字符, 需要代理对
                result += char(0xD800 | ((code >> 10) & 0x3FF));
                result += char(0xDC00 | (code & 0x3FF));
                i += 4;
            } else {
                result += replacement;
                i++;
            }
        }
        // 无效序列
        else {
            result += replacement;
            i++;
        }
    }
    return result;
}

// UTF-8编码器
std::unordered_map<int32_t, std::string> bytes_to_unicode() {
    std::vector<int32_t> bs;

    // Add ASCII printable characters
    for (int32_t i = '!' ; i <= '~'; ++i) {
        bs.push_back(i);
    }

    // Add Latin-1 Supplement characters
    for (int32_t i = 0xA1; i <= 0xAC; ++i) {
        bs.push_back(i);
    }

    // Add Latin-1 Extended-A characters
    for (int32_t i = 0xAE; i <= 0xFF; ++i) {
        bs.push_back(i);
    }

    std::vector<int32_t> cs = bs;
    for (int32_t b = 0, n = 0; b < 256; ++b) {
        if (std::find(bs.begin(), bs.end(), b) == bs.end()) {
            bs.push_back(b);
            cs.push_back(256 + n);
            n++;
        }
    }

    std::unordered_map<int32_t, std::string> result;
    for (size_t i = 0; i < bs.size(); ++i) {
        result[bs[i]] = value_to_utf8(cs[i]);
    }

    return result;
}

// std::smatch: 保存匹配结果的容器, 可以通过 std::regex_match 或 std::regex_search 函数填充.
// std::regex_match: 用于检查整个字符串是否与正则表达式匹配.
// std::regex_search: 在输入字符串中搜索与正则表达式匹配的内容.
// std::regex_replace: 用于在字符串中执行正则表达式替换操作.
// std::regex_iterator: 用于迭代一个字符串中所有与正则表达式匹配的子串.
// std::regex_token_iterator: 用于迭代一个字符串中所有与正则表达式匹配的子串及其非匹配部分.
// std::cregex_iterator: 操作原生C风格的字符数组.
// std::sregex_iterator: 操作std::string类型的字符串.
// std::cregex_token_iterator: 操作原生C风格的字符数组.
// std::sregex_token_iterator: 操作std::string类型的字符串.
std::vector<std::string> split(const std::string &s, const std::string &delim) {
    const std::regex pattern(delim);
    std::vector<std::string> tokens;
    // show prefix before match: 即捕获分割符外的部分
    std::sregex_token_iterator iterator(s.begin(), s.end(), pattern, -1);
    for (auto it = iterator; it != std::sregex_token_iterator(); ++it) {
        if (!it->str().empty()) {
            tokens.push_back(*it);
        }
    }
    return tokens;
}

std::string html_unescape(const std::string &text) {
    size_t pos = 0;
    // 简单的HTML实体解码实现
    std::string result = text;
    while ((pos = result.find('&', pos)) != std::string::npos) {
        size_t semicolon_pos = result.find(';', pos);
        if (semicolon_pos == std::string::npos) break;

        std::string entity = result.substr(pos, semicolon_pos - pos + 1);

        if (entity == "&amp;") {
            result.replace(pos, entity.length(), "&");
        } else if (entity == "&lt;") {
            result.replace(pos, entity.length(), "<");
        } else if (entity == "&gt;") {
            result.replace(pos, entity.length(), ">");
        } else if (entity == "&quot;") {
            result.replace(pos, entity.length(), "\"");
        } else if (entity == "&apos;") {
            result.replace(pos, entity.length(), "'");
        }

        pos += 1;
    }

    return result;
}

std::string basic_clean(const std::string &text) {
    // 处理可能的嵌套编码情况
    std::string result = html_unescape(html_unescape(text));
    // 去除开头空白
    result = std::regex_replace(result, std::regex("^\\s+"), "");
    // 去除结尾空白
    result = std::regex_replace(result, std::regex("\\s+$"), "");
    return result;
}

std::string whitespace_clean(const std::string &text) {
    // 多个连续空白替换为单个空格
    std::string result = std::regex_replace(text, std::regex("\\s+"), " ");
    // 去除开头空白
    result = std::regex_replace(result, std::regex("^\\s+"), "");
    // 去除结尾空白
    result = std::regex_replace(result, std::regex("\\s+$"), "");
    return result;
}

SimpleTokenizer &SimpleTokenizer::instance() {
    static SimpleTokenizer tokenizer_;
    return tokenizer_;
}

SimpleTokenizer::SimpleTokenizer(const std::string &bpe_file) {
    std::filesystem::path bpe_path(bpe_file);
    if (!bpe_path.has_parent_path()) {
        TCHAR szPath[1024]{};
        GetModuleFileName(nullptr, szPath, 1024);
        std::filesystem::path exe_directory{szPath};
        bpe_path = exe_directory.parent_path() / bpe_file;
    }

    // 初始化字节编码器
    byte_encoder_ = bytes_to_unicode();
    for (const auto &[fst, snd] : byte_encoder_) {
        byte_decoder_[snd] = fst;
    }

    std::vector<std::string> values;
    values.reserve(byte_encoder_.size());
    std::ranges::transform(byte_encoder_, std::back_inserter(values), [](const std::pair<int32_t, std::string> &p) { return p.second; });

    std::vector<std::string> vocab(values.begin(), values.end());
    vocab.reserve(values.size() * 2);
    for (const auto &v : values) {
        vocab.push_back(v + "</w>");
    }

    // 读取BPE文件
    std::string output;
    readGzipFile(absolute(bpe_path).string(), output);
    std::vector<std::string> tokens = split(output, "\n");
    std::vector<std::string> merges(tokens.begin() + 1, tokens.begin() + (49152 - 256 - 2 + 1));
    for (const auto &item : merges) {
        std::istringstream iss(item);
        std::string a, b;
        if (iss >> a >> b) {
            vocab.push_back(a + b);
            bpe_ranks_[{a, b}] = bpe_ranks_.size();
        }
    }
    vocab.emplace_back("<|startoftext|>");
    vocab.emplace_back("<|endoftext|>");

    // 创建编码器/解码器
    for (size_t i = 0; i < vocab.size(); ++i) {
        encoder_[vocab[i]] = i;
        decoder_[i] = vocab[i];
    }

    // 初始化缓存
    cache_ = {
        {"<|startoftext|>", "<|startoftext|>"},
        {"<|endoftext|>",   "<|endoftext|>"}
    };

    // 编译正则表达式
    pattern_ = std::regex(
        R"(<\|startoftext\|>|<\|endoftext\|>|'s|'t|'re|'ve|'m|'ll|'d|\w+|\d+|[^\s\w]+)",
        std::regex_constants::icase
    );
}
// 生成相邻字节对
std::vector<SPair> get_pairs(const std::vector<std::string>& word) {
    std::vector<SPair> pairs;
    for (int i = 0; i < word.size() - 1; ++i) {
        pairs.emplace_back(word[i], word[i+1]);
    }
    return pairs;
}

std::string SimpleTokenizer::bpe(const std::string &token) {
    if (cache_.contains(token)) {
        return cache_[token];
    }

    // 预处理token
    std::vector<std::string> word;
    for (int i = 0; i < token.size() - 1; ++i) {
        word.push_back(token.substr(i, 1));
    }
    word.push_back(token.substr(token.size() - 1) + "</w>");  // ('b', 'i', 'r', 'd</w>')
    if (word.size() <= 1) {
        return token + "</w>";
    }

    while (true) {
        const auto pairs = get_pairs(word);     // {('b', 'i'), ('i', 'r'), ('r', 'd</w>')}
        if (pairs.empty()) { break; }

        // 查找最小rank的bigram
        const auto Pred = [this](const SPair &a, const SPair &b) { return bpe_ranks_[a] < bpe_ranks_[b]; };
        const auto bigram_it = std::ranges::min_element(pairs, Pred);
        if (bigram_it == pairs.end()) { break; }
        const auto &bigram = *bigram_it;
        if (!bpe_ranks_.contains(bigram)) { break; }

        const auto first = bigram.first;
        const auto second = bigram.second;

        size_t i = 0;
        std::vector<std::string> new_word;
        while (i < word.size()) {
            // 查找从i开始第一个等于first的位置j
            size_t j = word.size();
            for (size_t k = i; k < word.size(); ++k) {
                if (word[k] == first) {
                    j = k;
                    break;
                }
            }

            // 处理查找结果
            if (j == word.size()) {
                // 未找到, 添加剩余元素并退出
                for (size_t k = i; k < word.size(); k++) {
                    new_word.push_back(word[k]);
                }
                break;
            }
            // 添加i到j-1的元素
            for (size_t k = i; k < j; k++) {
                new_word.push_back(word[k]);
            }
            i = j;

            // 检查是否需要合并
            if (i < word.size() - 1 && word[i] == first && word[i+1] == second) {
                new_word.push_back(first + second);
                i += 2;
            } else {
                new_word.push_back(word[i]);
                i += 1;
            }
        }

        word = new_word;
        if (word.size() == 1) { break; }
    }

    std::string result;
    for (const auto &w : word) {
        result += w + " ";
    }
    if (!result.empty()) result.pop_back(); // 移除末尾空格

    cache_[token] = result;
    return result;
}

std::vector<int64_t> SimpleTokenizer::encode(const std::string &text) {
    std::string tokens = whitespace_clean(basic_clean(text));
    std::ranges::transform(tokens, tokens.begin(), [](int32_t c){ return std::tolower(c); });

    std::vector<int64_t> bpe_tokens;
    // show whole match: 即捕获分割符的部分
    const std::sregex_iterator iterator(tokens.begin(), tokens.end(), pattern_);
    for (std::sregex_iterator it = iterator; it != std::sregex_iterator(); ++it) {
        std::string token;
        for (const auto &b : it->str()) {
            token += byte_encoder_[b];   //bird
        }

        std::string bpe_result = bpe(token);
        std::vector<std::string> sub_tokens;
        std::istringstream iss(bpe_result);
        std::string sub_token;
        while (std::getline(iss, sub_token, ' ')) {
            if (!sub_token.empty()) {
                sub_tokens.push_back(sub_token);
            }
        }

        for (const auto &st : sub_tokens) {
            if (encoder_.contains(st)) {
                bpe_tokens.push_back(encoder_[st]);
            } else {
                bpe_tokens.push_back(-1);
            }
        }
    }
    return bpe_tokens;
}

std::string SimpleTokenizer::decode(const std::vector<int64_t> &tokens) {
    // 第一阶段: token -> 字符映射
    std::string text;
    for (const auto &token : tokens) {
        auto it = decoder_.find(token);
        if (it == decoder_.end()) {
            throw std::runtime_error("Unknown token: " + token);
        }
        text += it->second;
    }

    // 第二阶段: 字符 -> 字节映射
    std::vector<uint8_t> bytes;
    bytes.reserve(text.size());
    for (const char &c : text) {
        auto it = byte_decoder_.find(std::string(1, c));
        if (it == byte_decoder_.end()) {
            throw std::runtime_error("Unknown character: " + std::string(1, c));
        }
        bytes.push_back(it->second);
    }

    // 第三阶段: UTF-8解码（含错误替换）
    std::string decoded = utf8_replace_errors(bytes);

    // 第四阶段: 替换标签
    size_t pos = 0;
    while ((pos = decoded.find("</w>", pos)) != std::string::npos) {
        decoded.replace(pos, 4, " ");
        pos += 1; // 防止无限循环
    }

    return decoded;
}

std::vector<std::vector<int64_t>> tokenize(const std::vector<std::string> &texts, size_t context_length, bool truncate) {
    //"""
    //Returns the tokenized representation of given input string(s)
    //
    //Parameters
    //----------
    //texts : Union[str, List[str]]
    //    An input string or a list of input strings to tokenize
    //
    //context_length : int
    //    The context length to use; all CLIP models use 77 as the context length
    //
    //truncate: bool
    //    Whether to truncate the text in case its encoding is longer than the context length
    //
    //Returns
    //-------
    //    A two-dimensional tensor containing the resulting tokens, shape = [number of input strings, context_length].
    //    We return LongTensor when torch version is <1.8.0, since older index_select requires indices to be long.
    //"""
    // 1. 获取特殊标记的ID
    SimpleTokenizer &tokenizer = SimpleTokenizer::instance();
    int32_t sot_token = tokenizer.encoder_.at("<|startoftext|>");  //49406
    int32_t eot_token = tokenizer.encoder_.at("<|endoftext|>");    //49407

    // 2. 为所有文本编码并添加特殊标记
    std::vector<std::vector<int64_t>> all_tokens;
    all_tokens.reserve(texts.size());

    // bird, toy
    // [[49406, 3329, 49407], [49406, 5988, 49407]]
    for (const auto &text : texts) {
        std::vector<int64_t> tokens = tokenizer.encode(text);
        // 在开头添加起始标记, 结尾添加结束标记
        tokens.insert(tokens.begin(), sot_token);
        tokens.push_back(eot_token);
        all_tokens.push_back(std::move(tokens));
    }

    // 3. 初始化结果二维向量, 并填充0（相当于np.zeros）
    std::vector<std::vector<int64_t>> result(texts.size(), std::vector<int64_t>(context_length, 0));

    // 4. 填充或截断每个序列
    for (size_t i = 0; i < all_tokens.size(); ++i) {
        const auto &tokens = all_tokens[i];
        const size_t token_len = tokens.size();

        // 处理长度超过上下文限制的情况
        if (token_len > context_length) {
            if (truncate) {
                // 截断: 取前context_length个标记, 并将最后一个替换为结束标记
                for (int j = 0; j < context_length; ++j) {
                    result[i][j] = tokens[j];
                }
                result[i][context_length - 1] = eot_token;
            } else {
                throw std::runtime_error(std::format("Input \"{}\" is too long for context length {}", texts[i], context_length));
            }
        }

        // 正常填充: 复制令牌到结果中
        for (size_t j = 0; j < token_len; ++j) {
            result[i][j] = tokens[j];
        }
    }

    return result;
}