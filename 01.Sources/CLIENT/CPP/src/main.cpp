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

// 테마 전체를 배율과 함께 다시 적용한다. 창이 다른 배율의 모니터로
// 옮겨질 때도 호출되므로, 항상 '기본 스타일에서 다시' 만든다
// (이미 커진 스타일에 또 곱하면 눈덩이처럼 불어난다).
void apply_theme(float scale) {
    ImGuiStyle st;  // 기본값에서 시작
    ImGui::StyleColorsDark(&st);

    // 컴팩트한 밀도: 여백을 줄이고 테두리 1px 로 윤곽을 세운다.
    st.WindowPadding = ImVec2(12, 10);
    st.FramePadding = ImVec2(9, 4);
    st.ItemSpacing = ImVec2(8, 6);
    st.ItemInnerSpacing = ImVec2(6, 4);
    st.CellPadding = ImVec2(6, 3);
    st.ScrollbarSize = 12.0f;
    st.FrameRounding = 4.0f;
    st.GrabRounding = 4.0f;
    st.ChildRounding = 6.0f;
    st.PopupRounding = 6.0f;
    st.ScrollbarRounding = 6.0f;
    st.FrameBorderSize = 1.0f;
    st.ChildBorderSize = 1.0f;
    st.SeparatorTextBorderSize = 1.0f;
    st.SeparatorTextPadding = ImVec2(16, 4);

    ImVec4* c = st.Colors;
    // 차분한 남색 바탕 + 부드러운 텍스트 톤.
    c[ImGuiCol_Text] = ImVec4(0.880f, 0.890f, 0.920f, 1.0f);
    c[ImGuiCol_TextDisabled] = ImVec4(0.450f, 0.480f, 0.550f, 1.0f);
    c[ImGuiCol_WindowBg] = ImVec4(0.058f, 0.062f, 0.080f, 1.0f);
    c[ImGuiCol_ChildBg] = ImVec4(0.043f, 0.047f, 0.062f, 1.0f);
    c[ImGuiCol_PopupBg] = ImVec4(0.070f, 0.075f, 0.095f, 1.0f);
    c[ImGuiCol_Border] = ImVec4(0.180f, 0.200f, 0.260f, 0.50f);
    c[ImGuiCol_FrameBg] = ImVec4(0.110f, 0.125f, 0.165f, 1.0f);
    c[ImGuiCol_FrameBgHovered] = ImVec4(0.145f, 0.170f, 0.225f, 1.0f);
    c[ImGuiCol_FrameBgActive] = ImVec4(0.180f, 0.215f, 0.285f, 1.0f);
    c[ImGuiCol_Button] = ImVec4(0.135f, 0.260f, 0.440f, 1.0f);
    c[ImGuiCol_ButtonHovered] = ImVec4(0.175f, 0.330f, 0.550f, 1.0f);
    c[ImGuiCol_ButtonActive] = ImVec4(0.215f, 0.400f, 0.660f, 1.0f);
    c[ImGuiCol_Header] = ImVec4(0.180f, 0.260f, 0.400f, 0.70f);
    c[ImGuiCol_HeaderHovered] = ImVec4(0.220f, 0.330f, 0.510f, 0.85f);
    c[ImGuiCol_HeaderActive] = ImVec4(0.250f, 0.390f, 0.600f, 1.0f);
    c[ImGuiCol_PlotHistogram] = ImVec4(0.235f, 0.510f, 0.870f, 1.0f);
    c[ImGuiCol_PlotHistogramHovered] = ImVec4(0.280f, 0.580f, 0.950f, 1.0f);
    c[ImGuiCol_Separator] = ImVec4(0.200f, 0.230f, 0.300f, 0.55f);
    c[ImGuiCol_TableRowBgAlt] = ImVec4(1.0f, 1.0f, 1.0f, 0.025f);

    st.ScaleAllSizes(scale);
    ImGui::GetStyle() = st;
    ImGui::GetIO().FontGlobalScale = scale;
}

// 창이 배율이 다른 모니터로 이동했을 때 GLFW 가 불러준다.
void content_scale_callback(GLFWwindow* /*window*/, float xscale, float yscale) {
    apply_theme(xscale > yscale ? xscale : yscale);
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
        glfwCreateWindow(static_cast<int>(640 * scale), static_cast<int>(700 * scale),
                         "Zhenyu_LoggerAnalyzer Client (C++/ImGui)", nullptr, nullptr);
    if (window == nullptr) {
        std::fprintf(stderr, "glfwCreateWindow failed\n");
        glfwTerminate();
        return 1;
    }
    // 레이아웃이 성립하는 최소 크기 아래로는 줄이지 못하게 한다.
    glfwSetWindowSizeLimits(window, static_cast<int>(520 * scale),
                            static_cast<int>(440 * scale), GLFW_DONT_CARE, GLFW_DONT_CARE);
    glfwSetWindowContentScaleCallback(window, content_scale_callback);
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);  // vsync

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;  // 창 배치를 파일로 남기지 않는다

    // 테마: 어두운 남색 배경 + 파란 액센트, 둥근 모서리.
    // 모니터 배율이 바뀌면 content_scale_callback 이 다시 적용한다.
    apply_theme(scale);

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
