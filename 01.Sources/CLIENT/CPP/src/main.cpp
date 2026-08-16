// Zhenyu_LoggerAnalyzer C++ client - 진입점.
//
// GLFW + OpenGL3 + Dear ImGui. Windows 와 Linux 에서 같은 코드가 빌드된다.
// 네트워크 I/O 는 전부 TransferClient 의 워커 스레드에서 돌고,
// 이 파일의 메인 루프는 초당 60회 화면만 그린다 (과제 C2: UI 비차단).

#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include <cstdio>

#include "ui/App.hpp"

namespace {

void glfw_error_callback(int error, const char* description) {
    std::fprintf(stderr, "GLFW error %d: %s\n", error, description);
}

}  // namespace

int main() {
    glfwSetErrorCallback(glfw_error_callback);
    if (glfwInit() == GLFW_FALSE) {
        std::fprintf(stderr, "glfwInit failed\n");
        return 1;
    }

    // OpenGL 3.0 + GLSL 130: Windows 기본 드라이버와 라즈베리파이(Mesa)
    // 어느 쪽에서도 동작하는 가장 무난한 조합이다.
    const char* glsl_version = "#version 130";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);

    // HiDPI: 모니터 배율에 맞춰 창 크기와 폰트를 함께 키운다.
    float scale = 1.0f;
    if (GLFWmonitor* monitor = glfwGetPrimaryMonitor()) {
        float sx = 1.0f, sy = 1.0f;
        glfwGetMonitorContentScale(monitor, &sx, &sy);
        scale = sx > sy ? sx : sy;
    }

    GLFWwindow* window =
        glfwCreateWindow(static_cast<int>(980 * scale), static_cast<int>(760 * scale),
                         "Zhenyu_LoggerAnalyzer Client (C++/ImGui)", nullptr, nullptr);
    if (window == nullptr) {
        std::fprintf(stderr, "glfwCreateWindow failed\n");
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);  // vsync

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;  // 창 배치를 파일로 남기지 않는다

    ImGui::StyleColorsDark();
    ImGui::GetStyle().ScaleAllSizes(scale);
    io.FontGlobalScale = scale;

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(glsl_version);

    {
        bydacli::App app;

        while (glfwWindowShouldClose(window) == GLFW_FALSE) {
            glfwPollEvents();

            ImGui_ImplOpenGL3_NewFrame();
            ImGui_ImplGlfw_NewFrame();
            ImGui::NewFrame();

            app.draw();

            ImGui::Render();
            int w = 0, h = 0;
            glfwGetFramebufferSize(window, &w, &h);
            glViewport(0, 0, w, h);
            glClearColor(0.08f, 0.08f, 0.10f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

            glfwSwapBuffers(window);
        }
        // app(TransferClient) 의 파괴자가 진행 중인 워커를 취소하고 join 한다.
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
