#ifndef __INC_TOKENIZER_H
#define __INC_TOKENIZER_H

#include <regex>
#include <string>
#include <vector>
#include <unordered_map>


template <class T>
void Comb(size_t &seed, const T &v) {
    std::hash<T> hasher;
    seed ^= hasher(v) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
}

template <class T, class U>
struct Hash {
    std::size_t operator()(const std::pair<T, U> &v) const {
        size_t seed = 0;
        Comb(seed, v.first);
        Comb(seed, v.second);
        return seed;
    }
};

using SPair = std::pair<std::string, std::string>;

// https://github.com/openai/CLIP/blob/main/clip/clip.py
// https://github.com/openai/CLIP/blob/main/clip/simple_tokenizer.py
class SimpleTokenizer {
public:
    explicit SimpleTokenizer(const std::string &bpe_file = "bpe_simple_vocab_16e6.txt.gz");

    static SimpleTokenizer &instance();

    std::string bpe(const std::string &token);

    std::vector<int64_t> encode(const std::string &text);
    std::string decode(const std::vector<int64_t> &tokens);

    std::unordered_map<SPair, int32_t, Hash<std::string, std::string>>  bpe_ranks_;

    std::unordered_map<int32_t, std::string>                            byte_encoder_;
    std::unordered_map<std::string, int32_t>                            byte_decoder_;

    std::unordered_map<std::string, int32_t>                            encoder_;
    std::unordered_map<int32_t, std::string>                            decoder_;

    std::unordered_map<std::string, std::string>                        cache_;

    std::regex                                                          pattern_;
};

std::vector<std::vector<int64_t>> tokenize(const std::vector<std::string> &texts, size_t context_length = 77, bool truncate = false);

#endif //__INC_TOKENIZER_H