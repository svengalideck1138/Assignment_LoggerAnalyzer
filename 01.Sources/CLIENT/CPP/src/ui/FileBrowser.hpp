// Zhenyu_LoggerAnalyzer C++ client - 내장 파일 브라우저.
//
// OS 네이티브 대화상자 대신 ImGui 로 그리는 이유:
//   - Windows(comdlg32)와 Linux(GTK/zenity)의 플랫폼 분기가 사라진다
//   - 어떤 데스크톱 환경에서도 동일하게 동작한다
// std::filesystem(C++17) 만 사용한다.

#pragma once

#include <array>
#include <filesystem>
#include <string>
#include <vector>

namespace bydacli {

class FileBrowser {
public:
    enum class Mode { OpenFile, SaveFile };

    // start 가 유효한 디렉터리(또는 그 안의 파일)면 거기서 시작한다.
    void open(Mode mode, const std::string& title, const std::string& start);

    // 매 프레임 호출한다. 이번 프레임에 선택이 확정되면 true.
    bool draw();

    bool visible() const noexcept { return visible_; }
    const std::string& selected() const noexcept { return selected_; }

private:
    void refresh();
    void go(const std::filesystem::path& dir);

    Mode mode_ = Mode::OpenFile;
    bool visible_ = false;
    std::string title_;
    std::filesystem::path dir_;
    std::string selected_;

    struct Entry {
        std::string name;   // UTF-8 표시용
        bool is_dir = false;
        std::uintmax_t size = 0;
    };
    std::vector<Entry> entries_;
    std::string list_error_;

    std::array<char, 1024> path_edit_{};
    std::array<char, 512> name_edit_{};
};

}  // namespace bydacli
