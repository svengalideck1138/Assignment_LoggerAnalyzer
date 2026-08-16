#include "FileBrowser.hpp"

#include <imgui.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace bydacli {

namespace fs = std::filesystem;

namespace {

std::string to_utf8(const fs::path& p) {
    // C++17: u8string() 은 std::string 을 돌려준다.
    return p.u8string();
}

template <std::size_t N>
void copy_to(std::array<char, N>& dst, const std::string& s) {
    const std::size_t n = s.size() < N - 1 ? s.size() : N - 1;
    std::memcpy(dst.data(), s.data(), n);
    dst[n] = '\0';
}

// 대소문자 무시 부분 일치.
bool contains_nocase(const std::string& hay, const char* needle) {
    if (needle[0] == '\0') return true;
    const std::size_t nn = std::strlen(needle);
    if (nn > hay.size()) return false;
    for (std::size_t i = 0; i + nn <= hay.size(); ++i) {
        std::size_t j = 0;
        while (j < nn && std::tolower(static_cast<unsigned char>(hay[i + j])) ==
                             std::tolower(static_cast<unsigned char>(needle[j]))) {
            ++j;
        }
        if (j == nn) return true;
    }
    return false;
}

// 폰트 크기에 비례한 px.
float em(float n) { return ImGui::GetFontSize() * n; }

// 강조(확정) 버튼: 더 밝은 파랑.
bool accent_button(const char* label, const ImVec2& size) {
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.22f, 0.46f, 0.78f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.28f, 0.55f, 0.90f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.33f, 0.62f, 1.00f, 1.0f));
    const bool pressed = ImGui::Button(label, size);
    ImGui::PopStyleColor(3);
    return pressed;
}

// 보조(취소 등) 버튼: 회색.
bool muted_button(const char* label, const ImVec2& size) {
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.22f, 0.24f, 0.30f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.28f, 0.31f, 0.38f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.34f, 0.38f, 0.46f, 1.0f));
    const bool pressed = ImGui::Button(label, size);
    ImGui::PopStyleColor(3);
    return pressed;
}

std::string size_text(std::uintmax_t n) {
    char buf[32];
    const double v = static_cast<double>(n);
    constexpr double ki = 1024.0;
    if (v >= ki * ki * ki) {
        std::snprintf(buf, sizeof(buf), "%.2f GiB", v / (ki * ki * ki));
    } else if (v >= ki * ki) {
        std::snprintf(buf, sizeof(buf), "%.1f MiB", v / (ki * ki));
    } else if (v >= ki) {
        std::snprintf(buf, sizeof(buf), "%.1f KiB", v / ki);
    } else {
        std::snprintf(buf, sizeof(buf), "%llu B", static_cast<unsigned long long>(n));
    }
    return buf;
}

}  // namespace

void FileBrowser::open(Mode mode, const std::string& title, const std::string& start) {
    mode_ = mode;
    // ### 뒤가 ImGui ID 가 된다. 제목이 바뀌어도 창 상태가 유지된다.
    title_ = title + "###filebrowser";
    want_open_ = true;
    visible_ = true;
    selected_.clear();
    name_edit_[0] = '\0';
    filter_[0] = '\0';

    std::error_code ec;
    fs::path p = fs::u8path(start);
    if (!start.empty() && fs::is_directory(p, ec)) {
        dir_ = p;
    } else if (!start.empty() && p.has_parent_path() &&
               fs::is_directory(p.parent_path(), ec)) {
        dir_ = p.parent_path();
        copy_to(name_edit_, to_utf8(p.filename()));
    } else {
        dir_ = fs::current_path(ec);
        if (!start.empty() && !p.has_parent_path()) {
            copy_to(name_edit_, start);  // "result.csv" 같은 순수 파일명
        }
    }
    refresh();
}

void FileBrowser::go(const fs::path& dir) {
    std::error_code ec;
    if (fs::is_directory(dir, ec)) {
        dir_ = dir;
        refresh();
    }
}

void FileBrowser::refresh() {
    entries_.clear();
    list_error_.clear();
    copy_to(path_edit_, to_utf8(dir_));

    std::error_code ec;
    for (fs::directory_iterator it(dir_, fs::directory_options::skip_permission_denied, ec);
         !ec && it != fs::directory_iterator(); it.increment(ec)) {
        Entry e;
        e.name = to_utf8(it->path().filename());
        std::error_code ec2;
        e.is_dir = it->is_directory(ec2);
        if (!e.is_dir) {
            e.size = it->file_size(ec2);
            if (ec2) e.size = 0;
        }
        entries_.push_back(std::move(e));
    }
    if (ec) {
        list_error_ = "cannot list directory: " + ec.message();
    }

    std::sort(entries_.begin(), entries_.end(), [](const Entry& a, const Entry& b) {
        if (a.is_dir != b.is_dir) return a.is_dir;  // 디렉터리 먼저
        return a.name < b.name;
    });
}

void FileBrowser::draw_shortcuts() {
    // 바로가기 버튼은 전부 같은 높이의 회색 계열로 통일한다.
    if (muted_button("Home", ImVec2(em(3.6f), 0))) {
#ifdef _WIN32
        const char* home = std::getenv("USERPROFILE");
#else
        const char* home = std::getenv("HOME");
#endif
        if (home != nullptr) {
            go(fs::u8path(home));
        }
    }

#ifdef _WIN32
    // 존재하는 드라이브 문자를 같은 크기의 버튼으로 늘어놓는다.
    const DWORD mask = GetLogicalDrives();
    for (int i = 0; i < 26; ++i) {
        if ((mask & (1u << i)) == 0) {
            continue;
        }
        char label[4] = {static_cast<char>('A' + i), ':', '\0', '\0'};
        ImGui::SameLine();
        if (muted_button(label, ImVec2(em(2.2f), 0))) {
            char root[4] = {static_cast<char>('A' + i), ':', '\\', '\0'};
            go(fs::path(root));
        }
    }
#endif
}

bool FileBrowser::draw_table() {
    bool confirmed = false;
    const float footer = ImGui::GetFrameHeightWithSpacing() * 1.4f;

    if (ImGui::BeginTable("##files", 2,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter |
                              ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp,
                          ImVec2(0, -footer))) {
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 0.78f);
        ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthStretch, 0.22f);
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableHeadersRow();

        for (const Entry& e : entries_) {
            if (!contains_nocase(e.name, filter_.data())) {
                continue;
            }

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);

            const std::string label =
                (e.is_dir ? std::string("[+] ") : std::string("     ")) + e.name;
            const bool is_selected = !e.is_dir && e.name == name_edit_.data();
            if (ImGui::Selectable(label.c_str(), is_selected,
                                  ImGuiSelectableFlags_SpanAllColumns |
                                      ImGuiSelectableFlags_AllowDoubleClick)) {
                if (e.is_dir) {
                    if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                        go(dir_ / fs::u8path(e.name));
                    }
                } else {
                    copy_to(name_edit_, e.name);
                    if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                        selected_ = to_utf8(dir_ / fs::u8path(e.name));
                        confirmed = true;
                    }
                }
            }

            ImGui::TableSetColumnIndex(1);
            if (e.is_dir) {
                ImGui::TextDisabled("--");
            } else {
                ImGui::TextUnformatted(size_text(e.size).c_str());
            }
        }
        ImGui::EndTable();
    }
    return confirmed;
}

bool FileBrowser::draw() {
    if (!visible_) {
        return false;
    }
    if (want_open_) {
        ImGui::OpenPopup(title_.c_str());
        want_open_ = false;
    }

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowSize(ImVec2(vp->WorkSize.x * 0.66f, vp->WorkSize.y * 0.70f),
                             ImGuiCond_Appearing);
    ImGui::SetNextWindowPos(vp->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

    bool confirmed = false;
    bool keep_open = true;
    if (!ImGui::BeginPopupModal(title_.c_str(), &keep_open)) {
        if (!keep_open) {
            visible_ = false;
        }
        return false;
    }

    // ---- 바로가기 + 경로 바 ----
    draw_shortcuts();
    ImGui::SameLine();
    if (muted_button("Up", ImVec2(em(2.8f), 0))) {
        if (dir_.has_parent_path() && dir_ != dir_.parent_path()) {
            go(dir_.parent_path());
        }
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-em(4.0f));
    if (ImGui::InputText("##path", path_edit_.data(), path_edit_.size(),
                         ImGuiInputTextFlags_EnterReturnsTrue)) {
        go(fs::u8path(path_edit_.data()));
    }
    ImGui::SameLine();
    if (accent_button("Go", ImVec2(-1, 0))) {
        go(fs::u8path(path_edit_.data()));
    }

    // ---- 필터 ----
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 18.0f);
    ImGui::InputTextWithHint("##filter", "filter by name ...", filter_.data(),
                             filter_.size());
    if (!list_error_.empty()) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", list_error_.c_str());
    }

    // ---- 목록 ----
    if (draw_table()) {
        confirmed = true;
    }

    // ---- 파일명 + 확정 버튼 ----
    // 확정(Open/Save)은 강조색, Cancel 은 회색으로 위계를 준다.
    const char* verb = mode_ == Mode::OpenFile ? "Open" : "Save";
    const ImVec2 confirm_size(em(5.5f), em(1.55f));
    const float buttons_w =
        confirm_size.x * 2 + ImGui::GetStyle().ItemSpacing.x * 2;

    ImGui::SetNextItemWidth(-buttons_w);
    if (ImGui::InputTextWithHint("##name", "file name ...", name_edit_.data(),
                                 name_edit_.size(),
                                 ImGuiInputTextFlags_EnterReturnsTrue)) {
        confirmed = true;  // Enter = 확정 시도
    }
    ImGui::SameLine();

    const bool has_name = name_edit_[0] != '\0';
    ImGui::BeginDisabled(!has_name);
    if (accent_button(verb, confirm_size)) {
        confirmed = true;
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (muted_button("Cancel##fb", confirm_size)) {
        visible_ = false;
        ImGui::CloseCurrentPopup();
    }

    // ---- 확정 검증 ----
    if (confirmed) {
        confirmed = false;
        if (has_name) {
            const fs::path chosen = dir_ / fs::u8path(name_edit_.data());
            std::error_code ec;
            if (mode_ == Mode::OpenFile && !fs::is_regular_file(chosen, ec)) {
                list_error_ = "file does not exist: " + std::string(name_edit_.data());
            } else {
                selected_ = to_utf8(chosen);
                confirmed = true;
                visible_ = false;
                ImGui::CloseCurrentPopup();
            }
        }
    }

    ImGui::EndPopup();
    if (!keep_open) {
        visible_ = false;
    }
    return confirmed;
}

}  // namespace bydacli
