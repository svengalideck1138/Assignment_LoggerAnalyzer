#include "FileBrowser.hpp"

#include <imgui.h>

#include <algorithm>
#include <cstring>

namespace bydacli {

namespace fs = std::filesystem;

namespace {

std::string to_utf8(const fs::path& p) {
    // C++17: u8string() 은 std::string 을 돌려준다.
    return p.u8string();
}

void copy_to(std::array<char, 1024>& dst, const std::string& s) {
    const std::size_t n = s.size() < dst.size() - 1 ? s.size() : dst.size() - 1;
    std::memcpy(dst.data(), s.data(), n);
    dst[n] = '\0';
}

void copy_to(std::array<char, 512>& dst, const std::string& s) {
    const std::size_t n = s.size() < dst.size() - 1 ? s.size() : dst.size() - 1;
    std::memcpy(dst.data(), s.data(), n);
    dst[n] = '\0';
}

}  // namespace

void FileBrowser::open(Mode mode, const std::string& title, const std::string& start) {
    mode_ = mode;
    title_ = title;
    visible_ = true;
    selected_.clear();
    name_edit_[0] = '\0';

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

bool FileBrowser::draw() {
    if (!visible_) {
        return false;
    }

    bool confirmed = false;
    ImGui::SetNextWindowSize(ImVec2(560, 420), ImGuiCond_FirstUseEver);
    if (ImGui::Begin(title_.c_str(), &visible_)) {
        // ---- 경로 바 ----
        if (ImGui::Button("Up")) {
            if (dir_.has_parent_path() && dir_ != dir_.parent_path()) {
                go(dir_.parent_path());
            }
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-70.0f);
        ImGui::InputText("##path", path_edit_.data(), path_edit_.size());
        ImGui::SameLine();
        if (ImGui::Button("Go")) {
            go(fs::u8path(path_edit_.data()));
        }

        if (!list_error_.empty()) {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", list_error_.c_str());
        }

        // ---- 목록 ----
        const float footer = ImGui::GetFrameHeightWithSpacing() * 2.2f;
        if (ImGui::BeginChild("##list", ImVec2(0, -footer), ImGuiChildFlags_Borders)) {
            for (const Entry& e : entries_) {
                const std::string label = (e.is_dir ? "[D] " : "     ") + e.name;
                const bool is_selected = !e.is_dir && e.name == name_edit_.data();
                if (ImGui::Selectable(label.c_str(), is_selected,
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
                            visible_ = false;
                        }
                    }
                }
                if (!e.is_dir && ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("%llu bytes", static_cast<unsigned long long>(e.size));
                }
            }
        }
        ImGui::EndChild();

        // ---- 파일명 + 확정 버튼 ----
        ImGui::SetNextItemWidth(-110.0f);
        ImGui::InputText("##name", name_edit_.data(), name_edit_.size());
        ImGui::SameLine();

        const char* verb = mode_ == Mode::OpenFile ? "Open" : "Save";
        const bool has_name = name_edit_[0] != '\0';
        ImGui::BeginDisabled(!has_name);
        if (ImGui::Button(verb, ImVec2(90, 0)) && has_name) {
            const fs::path chosen = dir_ / fs::u8path(name_edit_.data());
            std::error_code ec;
            if (mode_ == Mode::OpenFile && !fs::is_regular_file(chosen, ec)) {
                list_error_ = "file does not exist: " + std::string(name_edit_.data());
            } else {
                selected_ = to_utf8(chosen);
                confirmed = true;
                visible_ = false;
            }
        }
        ImGui::EndDisabled();
    }
    ImGui::End();
    return confirmed;
}

}  // namespace bydacli
