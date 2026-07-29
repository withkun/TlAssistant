#include "natsort.h"
#include <filesystem>


// 1. 自然排序比较器 (Natural Compare)
// 模拟 os_sort_keygen 的核心逻辑：逐段比较，数字按数值比，非数字按字典序比
static bool natural_compare(const std::string &a, const std::string &b) {
    size_t i = 0, j = 0;
    while (i < a.size() && j < b.size()) {
        // 如果当前字符都是数字，提取完整数字进行数值比较
        if (std::isdigit(a[i]) && std::isdigit(b[j])) {
            // 跳过前导零 (可选，视具体需求而定，通常 os_sorted 会处理)
            // 这里简单处理：提取子串转 long long
            size_t start_i = i;
            while (i < a.size() && std::isdigit(a[i])) i++;
            size_t start_j = j;
            while (j < b.size() && std::isdigit(b[j])) j++;

            std::string num_a = a.substr(start_i, i - start_i);
            std::string num_b = b.substr(start_j, j - start_j);

            // 先比长度，长度不同则位数多的更大 (避免溢出且高效)
            if (num_a.length() != num_b.length()) {
                return num_a.length() < num_b.length();
            }
            // 长度相同，逐字符比较 (等价于数值比较，因为无前导零或长度一致)
            if (num_a != num_b) {
                return num_a < num_b;
            }
        } else {
            // 非数字部分，按字符字典序比较 (区分大小写可根据需求调整)
            if (a[i] != b[j]) {
                return a[i] < b[j];
            }
            i++;
            j++;
        }
    }
    // 如果一个字符串是另一个的前缀，短的排前面
    return a.size() < b.size();
}

// 2. 封装排序函数 (模拟 os_sorted)
template <typename T>
std::vector<T> natsort::os_sorted(std::vector<T> &seq, const bool reverse) {
    // 假设 T 是 std::string 或 std::filesystem::path
    // 如果 T 是复杂对象，需要传入 key extractor，此处简化为直接对字符串排序
    auto comp = [](const T &a, const T &b) {
        // 获取用于比较的字符串表示
        std::string str_a, str_b;
        if constexpr (std::is_same_v<T, std::string>) {
            str_a = a; str_b = b;
        } else if constexpr (std::is_same_v<T, std::filesystem::path>) {
            str_a = a.string(); str_b = b.string();
        } else if constexpr (std::is_same_v<T, QString>) {
            str_a = a.toStdString(); str_b = b.toStdString();
        } else {
            // 其他类型需用户自定义转换，此处报错或忽略
            static_assert(false, "Unsupported type for os_sorted");
        }
        return natural_compare(str_a, str_b);
    };

    if (reverse) {
        std::sort(seq.begin(), seq.end(), [comp](const T& a, const T& b) {
            return comp(b, a); // 反向比较
        });
    } else {
        std::sort(seq.begin(), seq.end(), comp);
    }
    return seq;
}

QStringList natsort::os_sorted(QStringList &images, bool reverse) {
    std::vector<QString> seq;
    seq.insert(seq.end(), images.begin(), images.end());
    os_sorted(seq, reverse);

    images.clear();
    std::ranges::for_each(seq, [&images](const QString &s) { images.push_back(s); });
    return images;
}