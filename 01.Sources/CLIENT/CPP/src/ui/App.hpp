// Zhenyu_LoggerAnalyzer C++ client - 메인 UI.
//
// 과제 요구사항 C1 의 네 요소를 그대로 배치한다:
//   파일 선택기 / 업로드 버튼 / 실시간 진행률 Progress Bar / 결과 다운로드 버튼
//
// UI 스레드는 그리기만 한다. 모든 네트워크 I/O 는 TransferClient 의
// 워커 스레드에서 돌고, 여기서는 매 프레임 스냅샷을 복사해 그린다.

#pragma once

#include <array>
#include <string>
#include <vector>

#include "../net/Transfer.hpp"
#include "FileBrowser.hpp"

namespace bydacli {

class App {
public:
    // 매 프레임 호출. 전체 화면 창 하나에 모든 패널을 그린다.
    void draw();

private:
    void draw_server_section(const Snapshot& s, bool busy);
    void draw_file_section(bool busy);
    void draw_transfer_section(const Snapshot& s, bool busy);
    void draw_result_section(const Snapshot& s, bool busy);
    void draw_log_section();

    void save_csv(const Snapshot& s);

    TransferClient client_;
    FileBrowser browser_;

    enum class BrowseTarget { None, LogFile, SavePath };
    BrowseTarget browse_target_ = BrowseTarget::None;

    std::array<char, 64> host_{"127.0.0.1"};
    int port_ = 8088;
    std::array<char, 64> client_name_{"imgui-client"};
    std::array<char, 1024> file_path_{};
    std::array<char, 1024> save_path_{"result.csv"};

    std::vector<std::string> log_lines_;
    std::string save_note_;   // 저장 성공/실패 안내
    bool save_note_error_ = false;
};

}  // namespace bydacli
