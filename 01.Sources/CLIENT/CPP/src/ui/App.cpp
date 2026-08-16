#include "App.hpp"

#include <imgui.h>

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

constexpr ImVec4 kGreen{0.35f, 0.85f, 0.45f, 1.0f};
constexpr ImVec4 kRed{1.0f, 0.4f, 0.4f, 1.0f};
constexpr ImVec4 kYellow{0.95f, 0.85f, 0.35f, 1.0f};
constexpr ImVec4 kGray{0.6f, 0.6f, 0.6f, 1.0f};

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
    ImGui::Separator();
    draw_file_section(busy);
    ImGui::Separator();
    draw_transfer_section(s, busy);
    ImGui::Separator();
    draw_result_section(s, busy);
    ImGui::Separator();
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
    ImGui::Text("1. Server");

    ImGui::BeginDisabled(busy);
    ImGui::SetNextItemWidth(160);
    ImGui::InputText("Address", host_.data(), host_.size());
    ImGui::SameLine();
    ImGui::SetNextItemWidth(100);
    if (ImGui::InputInt("Port", &port_, 0)) {
        if (port_ < 1) port_ = 1;
        if (port_ > 65535) port_ = 65535;
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(140);
    ImGui::InputText("Name", client_name_.data(), client_name_.size());
    ImGui::SameLine();
    if (ImGui::Button("Test Connection")) {
        client_.start_probe(host_.data(), static_cast<std::uint16_t>(port_),
                            client_name_.data());
    }
    ImGui::EndDisabled();

    if (s.hello_valid) {
        ImGui::TextColored(kGreen, "server %s  ·  session %u  ·  you are %s",
                           s.hello.server_version.c_str(), s.hello.session_id,
                           s.hello.observed_peer.c_str());
    } else {
        ImGui::TextColored(kGray, "not connected yet");
    }
}

void App::draw_file_section(bool busy) {
    ImGui::Text("2. Log file");

    ImGui::BeginDisabled(busy);
    ImGui::SetNextItemWidth(-110.0f);
    ImGui::InputText("##file", file_path_.data(), file_path_.size());
    ImGui::SameLine();
    if (ImGui::Button("Browse...", ImVec2(90, 0))) {
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
    ImGui::Text("3. Upload & Analyze");

    const bool can_upload = !busy && file_path_[0] != '\0';
    ImGui::BeginDisabled(!can_upload);
    if (ImGui::Button("Upload & Analyze", ImVec2(160, 0))) {
        save_note_.clear();
        client_.start_upload(host_.data(), static_cast<std::uint16_t>(port_),
                             client_name_.data(), file_path_.data());
    }
    ImGui::EndDisabled();

    ImGui::SameLine();
    ImGui::BeginDisabled(!busy);
    if (ImGui::Button("Cancel", ImVec2(90, 0))) {
        client_.request_cancel();
    }
    ImGui::EndDisabled();

    ImGui::SameLine();
    switch (s.phase) {
        case Phase::Done:
            ImGui::TextColored(kGreen, "done");
            break;
        case Phase::Failed:
            ImGui::TextColored(kRed, "failed: %s", s.error.c_str());
            break;
        case Phase::Cancelled:
            ImGui::TextColored(kYellow, "cancelled");
            break;
        case Phase::Idle:
            ImGui::TextColored(kGray, "idle");
            break;
        default:
            ImGui::TextColored(kYellow, "%s ...", phase_text(s.phase));
            break;
    }

    // ---- 진행률: 보낸 양과 서버가 파싱까지 끝낸 양을 구분해서 보여준다 ----
    {
        char overlay[128];
        std::snprintf(overlay, sizeof(overlay), "sent  %s / %s  (%.1f MiB/s)",
                      human_bytes(s.bytes_sent).c_str(), human_bytes(s.total_bytes).c_str(),
                      s.send_mibps);
        ImGui::ProgressBar(fraction(s.bytes_sent, s.total_bytes), ImVec2(-FLT_MIN, 0),
                           overlay);

        std::snprintf(overlay, sizeof(overlay), "server parsed  %s / %s",
                      human_bytes(s.bytes_consumed).c_str(),
                      human_bytes(s.total_bytes).c_str());
        ImGui::ProgressBar(fraction(s.bytes_consumed, s.total_bytes), ImVec2(-FLT_MIN, 0),
                           overlay);
    }

    // ---- 서버가 UPLOAD_ACK 으로 보내주는 진행 중 통계 ----
    if (s.lines_parsed > 0) {
        ImGui::TextColored(
            kGray, "lines %llu   accepted %llu   rejected %llu   spd samples %llu   avg %s",
            static_cast<unsigned long long>(s.lines_parsed),
            static_cast<unsigned long long>(s.accepted_lines),
            static_cast<unsigned long long>(s.rejected_lines),
            static_cast<unsigned long long>(s.spd_samples),
            s.spd_average.empty() ? "-" : s.spd_average.c_str());
    }

    if (!s.module_names.empty() &&
        ImGui::TreeNodeEx("Module counts (live)", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::BeginTable("##modules", 2,
                              ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg)) {
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
    ImGui::Text("4. Result");

    if (!s.result_valid) {
        ImGui::TextColored(kGray, "no analysis result yet");
        return;
    }

    ImGui::Text("lines %llu   accepted %llu   rejected %llu   oversize %llu",
                static_cast<unsigned long long>(s.result_total_lines),
                static_cast<unsigned long long>(s.result_accepted),
                static_cast<unsigned long long>(s.result_rejected),
                static_cast<unsigned long long>(s.result_oversize));
    ImGui::Text("spd samples %llu   average %s",
                static_cast<unsigned long long>(s.result_spd_samples),
                s.result_spd_average.c_str());
    ImGui::Text("server parse %llu ms   peak RSS %.1f MiB",
                static_cast<unsigned long long>(s.result_elapsed_ms),
                static_cast<double>(s.result_peak_rss_kb) / 1024.0);

    const bool has_csv = !s.csv.empty() && !busy;
    ImGui::BeginDisabled(!has_csv);
    ImGui::SetNextItemWidth(-230.0f);
    ImGui::InputText("##save", save_path_.data(), save_path_.size());
    ImGui::SameLine();
    if (ImGui::Button("Browse...##save", ImVec2(90, 0))) {
        browse_target_ = BrowseTarget::SavePath;
        browser_.open(FileBrowser::Mode::SaveFile, "Save result.csv", save_path_.data());
    }
    ImGui::SameLine();
    if (ImGui::Button("Save result.csv", ImVec2(120, 0))) {
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

    ImGui::Text("Log");
    ImGui::SameLine();
    if (ImGui::SmallButton("Clear")) {
        log_lines_.clear();
    }

    if (ImGui::BeginChild("##log", ImVec2(0, 0), ImGuiChildFlags_Borders)) {
        for (const std::string& line : log_lines_) {
            ImGui::TextUnformatted(line.c_str());
        }
        // 바닥을 보고 있을 때만 자동 스크롤한다.
        if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 4.0f) {
            ImGui::SetScrollHereY(1.0f);
        }
    }
    ImGui::EndChild();
}

}  // namespace bydacli
