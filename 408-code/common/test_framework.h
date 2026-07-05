/**
 * @file test_framework.h
 * @brief 极简测试框架 (无外部依赖)
 *
 * @考点 408 不考测试,但工业代码必备;框架刻意保持极简以聚焦知识点本身
 * @业务 真实项目用 GoogleTest/Catch2;此处自实现避免依赖
 * @陷阱 断言失败时立即返回 false,避免错误传播
 */
#ifndef CS408_COMMON_TEST_FRAMEWORK_H
#define CS408_COMMON_TEST_FRAMEWORK_H

#include <iostream>
#include <string>
#include <vector>
#include <functional>

namespace cs408 {

class TestRunner {
public:
    static TestRunner& instance() {
        static TestRunner r;
        return r;
    }

    void add(const std::string& name, std::function<bool()> fn) {
        cases_.push_back({name, fn});
    }

    int run_all() {
        int passed = 0;
        int total  = static_cast<int>(cases_.size());
        for (auto& c : cases_) {
            bool ok = false;
            try { ok = c.fn(); }
            catch (const std::exception& e) {
                std::cerr << "[EXCEPT] " << c.name << ": " << e.what() << "\n";
            } catch (...) {
                std::cerr << "[EXCEPT] " << c.name << ": unknown\n";
            }
            if (ok) {
                ++passed;
                std::cout << "[PASS] " << c.name << "\n";
            } else {
                std::cout << "[FAIL] " << c.name << "\n";
            }
        }
        std::cout << "\n=== " << passed << "/" << total << " passed ===\n";
        return (passed == total) ? 0 : 1;
    }

private:
    struct Case { std::string name; std::function<bool()> fn; };
    std::vector<Case> cases_;
};

// 简单断言 (支持 1 或 2 参数:CS408_EXPECT(cond) 或 CS408_EXPECT(cond, msg))
#define CS408_EXPECT_1(cond) \
    do { if(!(cond)) { std::cerr << "  EXPECT FAIL: " << #cond << " @ " << __FILE__ << ":" << __LINE__ << "\n"; return false; } } while(0)
#define CS408_EXPECT_2(cond, msg) \
    do { if(!(cond)) { std::cerr << "  EXPECT FAIL: " << #cond << " | " << msg << " @ " << __FILE__ << ":" << __LINE__ << "\n"; return false; } } while(0)
#define GET_CS408_EXPECT_MACRO(_1, _2, NAME, ...) NAME
#define CS408_EXPECT(...) GET_CS408_EXPECT_MACRO(__VA_ARGS__, CS408_EXPECT_2, CS408_EXPECT_1)(__VA_ARGS__)

// 单参别名
#define CS408_EXPECT_TRUE(cond) CS408_EXPECT_1(cond)

#define CS408_EXPECT_EQ(a, b) \
    do { if(!((a) == (b))) { std::cerr << "  EXPECT_EQ FAIL: " << #a << " != " << #b << " @ " << __FILE__ << ":" << __LINE__ << "\n"; return false; } } while(0)

#define CS408_TEST(name, body) \
    static bool _test_##name(); \
    static int _reg_test_##name = [](){ cs408::TestRunner::instance().add(#name, _test_##name); return 0; }(); \
    static bool _test_##name() body

} // namespace cs408

#endif // CS408_COMMON_TEST_FRAMEWORK_H
