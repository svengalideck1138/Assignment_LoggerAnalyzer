// 초소형 테스트 프레임워크.
//
// 외부 프레임워크를 쓰지 않는 이유: 이 저장소는 서드파티를 소스로만
// 벤더링한다는 원칙이 있고, 필요한 것은 등록/실행/실패 카운트뿐이다.
// 등록은 정적 초기화로, 실행은 main 에서 순서대로.

#pragma once

#include <cstdio>
#include <vector>

namespace testfw {

struct Test {
    const char* name;
    void (*fn)();
};

inline std::vector<Test>& registry() {
    static std::vector<Test> r;
    return r;
}

struct Registrar {
    Registrar(const char* name, void (*fn)()) { registry().push_back(Test{name, fn}); }
};

inline int g_failures = 0;

inline int run_all() {
    int ran = 0;
    for (const Test& t : registry()) {
        const int before = g_failures;
        t.fn();
        ++ran;
        std::printf("[%s] %s\n", g_failures == before ? " OK " : "FAIL", t.name);
    }
    std::printf("\n%d test(s), %d failure(s)\n", ran, g_failures);
    return g_failures == 0 ? 0 : 1;
}

}  // namespace testfw

#define TEST(name)                                              \
    static void test_##name();                                  \
    static ::testfw::Registrar reg_##name(#name, &test_##name); \
    static void test_##name()

// 실패해도 계속 진행한다. 한 번에 모든 실패를 보고하는 편이
// 고치는 입장에서 낫다.
#define CHECK(cond)                                                             \
    do {                                                                        \
        if (!(cond)) {                                                          \
            std::printf("  [FAIL] %s:%d  %s\n", __FILE__, __LINE__, #cond);     \
            ++::testfw::g_failures;                                             \
        }                                                                       \
    } while (0)

#define CHECK_EQ_U64(a, b)                                                      \
    do {                                                                        \
        const unsigned long long va = static_cast<unsigned long long>(a);       \
        const unsigned long long vb = static_cast<unsigned long long>(b);       \
        if (va != vb) {                                                         \
            std::printf("  [FAIL] %s:%d  %s == %s  (%llu != %llu)\n", __FILE__, \
                        __LINE__, #a, #b, va, vb);                              \
            ++::testfw::g_failures;                                             \
        }                                                                       \
    } while (0)
