#include "App.hpp"

#include <imgui.h>

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>

#include "../net/Payload.hpp"

namespace bydacli {

namespace fs = std::filesystem;

namespace {

// 진행률 (0..1). whole 이 0 이면 0.
float fraction(std::uint64_t part, std::uint64_t whole) {
    return whole > 0 ? static_cast<float>(static_cast<double>(part) /
                                          static_cast<double>(whole))
                     : 0.0f;
}

constexpr ImVec4 kGreen{0.30f, 0.85f, 0.42f, 1.0f};
constexpr ImVec4 kRed{1.00f, 0.36f, 0.36f, 1.0f};
constexpr ImVec4 kYellow{0.98f, 0.80f, 0.28f, 1.0f};
constexpr ImVec4 kGray{0.52f, 0.55f, 0.60f, 1.0f};

// 폰트 크기에 비례한 px. HiDPI 배율이 바뀌어도 배치가 유지된다.
float em(float n) { return ImGui::GetFontSize() * n; }

// 상태 LED 의 발광 방식.
//   Solid   : 고정 밝기
//   Blink   : 200ms 켜짐 / 200ms 꺼짐 (디지털 점멸)
//   Breathe : sin 곡선으로 밝기가 부드럽게 오르내린다 (PWM 디밍 느낌)
enum class LedMode { Solid, Blink, Breathe };

// 상태 LED (C# 클라이언트의 LED 이미지에 해당하는 부분을 draw list 로
// 직접 그린다). 높이를 '텍스트 한 줄'에 맞춰서, SameLine 으로 뒤에
// 오는 글씨와 세로 중심이 정확히 일치한다.
void led(ImVec4 color, LedMode mode) {
    if (mode == LedMode::Blink) {
        if (std::fmod(ImGui::GetTime(), 0.4) >= 0.2) {
            color.x *= 0.25f;
            color.y *= 0.25f;
            color.z *= 0.25f;
        }
    } else if (mode == LedMode::Breathe) {
        // 0.15 ~ 1.0 사이를 약 1.5Hz 로 오간다. 완전히 꺼지지는 않아서
        // '살아 있는 경고'라는 느낌을 준다.
        const float wave =
            0.5f + 0.5f * std::sin(static_cast<float>(ImGui::GetTime()) * 3.0f * 3.14159f);
        const float duty = 0.15f + 0.85f * wave;
        color.x *= duty;
        color.y *= duty;
        color.z *= duty;
    }
    const float h = ImGui::GetTextLineHeight();
    const float r = em(0.28f);
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const ImVec2 center(pos.x + r, pos.y + h * 0.5f);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    // 은은한 후광 -> 본체 -> 하이라이트 순서로 겹쳐 그린다.
    dl->AddCircleFilled(center, r * 1.6f,
                        ImGui::ColorConvertFloat4ToU32(
                            ImVec4(color.x, color.y, color.z, 0.18f)));
    dl->AddCircleFilled(center, r, ImGui::ColorConvertFloat4ToU32(color));
    dl->AddCircleFilled(ImVec2(center.x - r * 0.30f, center.y - r * 0.30f), r * 0.34f,
                        ImGui::ColorConvertFloat4ToU32(ImVec4(1, 1, 1, 0.35f)));

    ImGui::Dummy(ImVec2(r * 2.4f, h));
}

// "라벨 위, 입력창 아래" 폼 필드의 시작. EndGroup 은 호출자가 한다.
//
// AlignTextToFramePadding: 같은 줄에서 입력창 '뒤'에 오는 텍스트는
// ImGui 가 베이스라인을 맞추려고 FramePadding 만큼 아래로 민다.
// 첫 그룹의 라벨에는 이 오프셋이 없어서 혼자 위로 떠 보이므로,
// 모든 라벨에 같은 오프셋을 강제해 높이를 통일한다.
void field_label(const char* label) {
    ImGui::BeginGroup();
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("%s", label);
}

}  // namespace

void App::draw() {
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::Begin("##main", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoSavedSettings);

    const Snapshot s = client_.snapshot();
    const bool busy = client_.busy();

    draw_server_section(s, busy);
    draw_file_section(s);
    draw_transfer_section(s, busy);
    draw_result_section(s, busy);
    draw_log_section();

    ImGui::End();

    // ---- 파일 브라우저 (열려 있으면) ----
    if (browser_.draw()) {
        const std::string& sel = browser_.selected();
        if (browse_target_ == BrowseTarget::LogFile) {
            std::snprintf(file_path_.data(), file_path_.size(), "%s", sel.c_str());
        } else if (browse_target_ == BrowseTarget::SavePath) {
            std::snprintf(save_path_.data(), save_path_.size(), "%s", sel.c_str());
        }
        browse_target_ = BrowseTarget::None;
    }
}

void App::draw_server_section(const Snapshot& s, bool busy) {
    ImGui::SeparatorText("1 · SERVER");

    const bool transferring = s.phase == Phase::Uploading ||
                              s.phase == Phase::WaitingAnalysis ||
                              s.phase == Phase::ReceivingResult;

    ImGui::BeginDisabled(busy);

    // 창이 넓으면 네 요소를 한 줄에, 좁으면 두 줄로 나눠 배치한다.
    const bool one_row = ImGui::GetContentRegionAvail().x >= em(38.0f);
    const float gap = em(1.2f);

    if (one_row) {
        field_label("Server address");
        ImGui::SetNextItemWidth(em(11.0f));
        ImGui::InputText("##host", host_.data(), host_.size());
        ImGui::EndGroup();
        ImGui::SameLine(0, gap);
    } else {
        field_label("Server address");
        // 좁은 화면: 주소가 남은 폭을 쓰고, 포트가 오른쪽 끝에 붙는다.
        ImGui::SetNextItemWidth(-em(7.0f));
        ImGui::InputText("##host", host_.data(), host_.size());
        ImGui::EndGroup();
        ImGui::SameLine(0, gap);
    }

    field_label("Port");
    ImGui::SetNextItemWidth(one_row ? em(5.0f) : -1.0f);
    if (ImGui::InputInt("##port", &port_, 0)) {
        if (port_ < 1) port_ = 1;
        if (port_ > 65535) port_ = 65535;
    }
    ImGui::EndGroup();
    if (one_row) {
        ImGui::SameLine(0, gap);
    }

    field_label("Client name");
    ImGui::SetNextItemWidth(one_row ? em(9.0f) : -em(10.0f));
    ImGui::InputText("##name", client_name_.data(), client_name_.size());
    ImGui::EndGroup();

    ImGui::EndDisabled();  // 입력창은 세션이 살아 있는 동안 잠근다

    ImGui::SameLine(0, gap);

    // ---- Connect <-> Disconnect 토글 버튼 ----
    // 버튼을 입력창 줄에 맞추기 위해 같은 높이의 빈 라벨을 둔다.
    field_label(" ");
    const ImVec2 btn_size(one_row ? em(8.0f) : -1.0f, 0);
    if (!busy) {
        if (ImGui::Button("Connect", btn_size)) {
            client_.connect(host_.data(), static_cast<std::uint16_t>(port_),
                            client_name_.data());
        }
    } else if (!s.connected) {
        // 접속 시도 중: 누를 수 없는 상태로 보여준다.
        ImGui::BeginDisabled(true);
        ImGui::Button("Connect", btn_size);
        ImGui::EndDisabled();
    } else {
        // 전송 중에는 끊기 전에 Cancel 부터 하도록 잠근다.
        ImGui::BeginDisabled(transferring);
        if (ImGui::Button("Disconnect", btn_size)) {
            client_.disconnect();
        }
        ImGui::EndDisabled();
    }
    ImGui::EndGroup();

    // ---- 연결 상태 LED + 문구 (좁은 창에서는 자동 줄바꿈) ----
    if (busy && !s.connected) {
        led(kYellow, LedMode::Blink);
        ImGui::SameLine();
        ImGui::TextColored(kYellow, "%s ...", phase_text(s.phase));
    } else if (s.phase == Phase::Failed) {
        led(kRed, LedMode::Breathe);  // 실패: PWM 디밍처럼 숨쉰다
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Text, kRed);
        ImGui::TextWrapped("%s", s.error.c_str());
        ImGui::PopStyleColor();
    } else if (s.connected) {
        // 첫 줄: LED + connected. 둘째 줄: 상세 정보 (LED 폭만큼 들여쓰기).
        // 한 줄에 다 붙이면 좁은 창에서 IP 주소 중간이 꺾여 보기 흉하다.
        led(kGreen, LedMode::Blink);  // 연결 유지 상태: 200ms 점멸
        ImGui::SameLine();
        ImGui::TextColored(kGreen, "connected");

        const float indent = em(0.28f) * 2.4f + ImGui::GetStyle().ItemSpacing.x;
        ImGui::Indent(indent);
        ImGui::PushStyleColor(ImGuiCol_Text, kGray);
        ImGui::TextWrapped("%s  ·  session %u  ·  your address %s",
                           s.hello.server_version.c_str(), s.hello.session_id,
                           s.hello.observed_peer.c_str());
        ImGui::PopStyleColor();
        ImGui::Unindent(indent);
    } else {
        led(kGray, LedMode::Solid);
        ImGui::SameLine();
        ImGui::TextColored(kGray, "not connected yet");
    }
}

void App::draw_file_section(const Snapshot& s) {
    ImGui::SeparatorText("2 · LOG FILE");

    // 연결 유지 중에도 파일은 고를 수 있어야 한다.
    // 잠그는 것은 '실제 전송 중'일 때뿐이다.
    const bool transferring = s.phase == Phase::Uploading ||
                              s.phase == Phase::WaitingAnalysis ||
                              s.phase == Phase::ReceivingResult;
    ImGui::BeginDisabled(transferring);
    ImGui::SetNextItemWidth(-em(7.0f));
    ImGui::InputTextWithHint("##file", "select the 500MB test log ...", file_path_.data(),
                             file_path_.size());
    ImGui::SameLine();
    if (ImGui::Button("Browse...", ImVec2(-1, 0))) {
        browse_target_ = BrowseTarget::LogFile;
        browser_.open(FileBrowser::Mode::OpenFile, "Select a log file", file_path_.data());
    }
    ImGui::EndDisabled();

    if (file_path_[0] != '\0') {
        std::error_code ec;
        const auto size = fs::file_size(fs::u8path(file_path_.data()), ec);
        if (!ec) {
            ImGui::TextColored(kGray, "%s", human_bytes(size).c_str());
        } else {
            ImGui::TextColored(kRed, "file not found");
        }
    }
}

void App::draw_transfer_section(const Snapshot& s, bool busy) {
    (void)busy;
    ImGui::SeparatorText("3 · UPLOAD & ANALYZE");

    const bool transferring = s.phase == Phase::Uploading ||
                              s.phase == Phase::WaitingAnalysis ||
                              s.phase == Phase::ReceivingResult;

    const bool can_upload = s.connected && !transferring && file_path_[0] != '\0';
    ImGui::BeginDisabled(!can_upload);
    if (ImGui::Button("Upload & Analyze", ImVec2(em(10.0f), em(1.7f)))) {
        save_note_.clear();
        client_.start_upload(file_path_.data());
    }
    ImGui::EndDisabled();

    ImGui::SameLine();
    ImGui::BeginDisabled(!transferring);
    if (ImGui::Button("Cancel", ImVec2(em(5.5f), em(1.7f)))) {
        client_.request_cancel();
    }
    ImGui::EndDisabled();

    // 버튼 옆에 현재 단계. 큰 버튼(2em)의 세로 중앙에 글씨를 맞춘다.
    ImGui::SameLine(0, em(1.0f));
    ImGui::BeginGroup();
    {
        float pad = (em(1.7f) - ImGui::GetTextLineHeight()) * 0.5f -
                    ImGui::GetStyle().ItemSpacing.y;
        if (pad < 0.0f) pad = 0.0f;
        ImGui::Dummy(ImVec2(1, pad));
    }
    switch (s.phase) {
        case Phase::Done:
            ImGui::TextColored(kGreen, "done");
            break;
        case Phase::Failed:
            ImGui::PushStyleColor(ImGuiCol_Text, kRed);
            ImGui::TextWrapped("failed: %s", s.error.c_str());
            ImGui::PopStyleColor();
            break;
        case Phase::Cancelled:
            ImGui::TextColored(kYellow, "cancelled");
            break;
        case Phase::Idle:
            ImGui::TextColored(kGray, "idle");
            break;
        case Phase::Connected:
            ImGui::TextColored(kGray, "ready");
            break;
        default:
            ImGui::TextColored(kYellow, "%s ...", phase_text(s.phase));
            break;
    }
    ImGui::EndGroup();

    // ---- 진행률: 서버가 '분석까지 끝낸' 양 기준 하나만 보여준다 ----
    // 보낸 양(bytes_sent)은 소켓 버퍼에 들어간 것뿐이라 실제 진행보다
    // 앞서 간다. 채워지는 기준은 분석 완료량으로 하고, 전송 속도 같은
    // 부가 정보는 오버레이 문구에만 싣는다.
    {
        char overlay[160];
        if (s.phase == Phase::Uploading && s.bytes_sent > s.bytes_consumed) {
            std::snprintf(overlay, sizeof(overlay), "analyzed  %s / %s   (sent %s, %.1f MiB/s)",
                          human_bytes(s.bytes_consumed).c_str(),
                          human_bytes(s.total_bytes).c_str(),
                          human_bytes(s.bytes_sent).c_str(), s.send_mibps);
        } else {
            std::snprintf(overlay, sizeof(overlay), "analyzed  %s / %s",
                          human_bytes(s.bytes_consumed).c_str(),
                          human_bytes(s.total_bytes).c_str());
        }
        ImGui::ProgressBar(fraction(s.bytes_consumed, s.total_bytes),
                           ImVec2(-FLT_MIN, em(1.25f)), overlay);
    }

    // ---- 서버가 UPLOAD_ACK 으로 보내주는 진행 중 통계 (자동 줄바꿈) ----
    if (s.lines_parsed > 0) {
        ImGui::PushStyleColor(ImGuiCol_Text, kGray);
        ImGui::TextWrapped(
            "lines %llu   accepted %llu   rejected %llu   spd samples %llu   avg %s",
            static_cast<unsigned long long>(s.lines_parsed),
            static_cast<unsigned long long>(s.accepted_lines),
            static_cast<unsigned long long>(s.rejected_lines),
            static_cast<unsigned long long>(s.spd_samples),
            s.spd_average.empty() ? "-" : s.spd_average.c_str());
        ImGui::PopStyleColor();
    }

    if (!s.module_names.empty() && ImGui::TreeNode("Module counts (live)")) {
        if (ImGui::BeginTable("##modules", 2,
                              ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg |
                                  ImGuiTableFlags_BordersOuter)) {
            for (std::size_t i = 0; i < s.module_names.size(); ++i) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted(s.module_names[i].c_str());
                ImGui::TableSetColumnIndex(1);
                const std::uint64_t c = i < s.module_counts.size() ? s.module_counts[i] : 0;
                ImGui::Text("%llu", static_cast<unsigned long long>(c));
            }
            ImGui::EndTable();
        }
        ImGui::TreePop();
    }
}

void App::draw_result_section(const Snapshot& s, bool busy) {
    ImGui::SeparatorText("4 · RESULT");

    if (!s.result_valid) {
        ImGui::TextColored(kGray, "no analysis result yet");
        return;
    }

    // 핵심 결과를 표로. 과제의 두 답(모듈 카운트는 CSV, 평균 속도)이 먼저다.
    if (ImGui::BeginTable("##summary", 4,
                          ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg |
                              ImGuiTableFlags_BordersOuter)) {
        auto row = [](const char* k1, const std::string& v1, const char* k2,
                      const std::string& v2) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextDisabled("%s", k1);
            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(v1.c_str());
            ImGui::TableSetColumnIndex(2);
            ImGui::TextDisabled("%s", k2);
            ImGui::TableSetColumnIndex(3);
            ImGui::TextUnformatted(v2.c_str());
        };

        row("spd average", s.result_spd_average,
            "spd samples", std::to_string(s.result_spd_samples));
        row("total lines", std::to_string(s.result_total_lines),
            "accepted", std::to_string(s.result_accepted));
        row("rejected", std::to_string(s.result_rejected),
            "oversize", std::to_string(s.result_oversize));
        row("server parse", std::to_string(s.result_elapsed_ms) + " ms",
            "server peak RSS", [&] {
                char b[32];
                std::snprintf(b, sizeof(b), "%.1f MiB",
                              static_cast<double>(s.result_peak_rss_kb) / 1024.0);
                return std::string(b);
            }());
        ImGui::EndTable();
    }

    // 버튼 두 개는 고정 폭으로 오른쪽에 두고, 경로 입력이 남은 폭을 쓴다.
    // 창이 좁아져도 버튼 라벨이 잘리지 않는다.
    const bool has_csv = !s.csv.empty() && !busy;
    const float buttons_w = em(6.0f) + em(9.0f) + ImGui::GetStyle().ItemSpacing.x * 2;
    ImGui::BeginDisabled(!has_csv);
    ImGui::SetNextItemWidth(-buttons_w);
    ImGui::InputText("##save", save_path_.data(), save_path_.size());
    ImGui::SameLine();
    if (ImGui::Button("Browse...##save", ImVec2(em(6.0f), 0))) {
        browse_target_ = BrowseTarget::SavePath;
        browser_.open(FileBrowser::Mode::SaveFile, "Save result.csv", save_path_.data());
    }
    ImGui::SameLine();
    if (ImGui::Button("Save result.csv", ImVec2(em(9.0f), 0))) {
        save_csv(s);
    }
    ImGui::EndDisabled();

    if (!save_note_.empty()) {
        ImGui::TextColored(save_note_error_ ? kRed : kGreen, "%s", save_note_.c_str());
    }
}

void App::save_csv(const Snapshot& s) {
    const fs::path out = fs::u8path(save_path_.data());
    std::ofstream f(out, std::ios::binary | std::ios::trunc);
    if (!f) {
        save_note_ = "cannot open for writing: " + std::string(save_path_.data());
        save_note_error_ = true;
        return;
    }
    f.write(s.csv.data(), static_cast<std::streamsize>(s.csv.size()));
    f.close();
    if (!f) {
        save_note_ = "write failed: " + std::string(save_path_.data());
        save_note_error_ = true;
        return;
    }
    save_note_ = "saved " + std::to_string(s.csv.size()) + " bytes to " +
                 std::string(save_path_.data());
    save_note_error_ = false;
}

void App::draw_log_section() {
    // 워커가 쌓아 둔 새 로그 줄을 가져온다.
    for (std::string& line : client_.drain_log()) {
        log_lines_.push_back(std::move(line));
    }
    if (log_lines_.size() > 4000) {
        log_lines_.erase(log_lines_.begin(),
                         log_lines_.begin() + static_cast<std::ptrdiff_t>(2000));
    }

    ImGui::SeparatorText("LOG");

    // 헤더 줄: 왼쪽에 줄 수, 오른쪽에 Copy / Clear 버튼.
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("%d lines", static_cast<int>(log_lines_.size()));

    const float bw = em(4.0f);
    const float gap = ImGui::GetStyle().ItemSpacing.x;
    ImGui::SameLine();
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x -
                         (bw * 2 + gap));
    if (ImGui::Button("Copy", ImVec2(bw, 0))) {
        std::string all;
        for (const std::string& line : log_lines_) {
            all += line;
            all += '\n';
        }
        ImGui::SetClipboardText(all.c_str());
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear", ImVec2(bw, 0))) {
        log_lines_.clear();
    }

    if (ImGui::BeginChild("##log", ImVec2(0, 0), ImGuiChildFlags_Borders)) {
        // 창보다 긴 줄은 자동으로 줄바꿈한다.
        ImGui::PushTextWrapPos(0.0f);
        for (const std::string& line : log_lines_) {
            ImGui::TextUnformatted(line.c_str());
        }
        ImGui::PopTextWrapPos();
        // 바닥을 보고 있을 때만 자동 스크롤한다.
        if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 4.0f) {
            ImGui::SetScrollHereY(1.0f);
        }
    }
    ImGui::EndChild();
}

}  // namespace bydacli
