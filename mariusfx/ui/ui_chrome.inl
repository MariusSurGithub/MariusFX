// ============================================================================ // ui_chrome.inl - included by ui.cpp inside namespace mariusfx::ui::{anonymous}. // This is not a stand-alone translation unit. It exists only as a logical // module to keep ui.cpp browsable. Do not compile or include directly. // ============================================================================ 
// Forward declarations for glyphs used by draw_preset_popup (defined further below).
inline void glyph_folder(ImDrawList *dl, ImVec2 c, ImU32 color);
inline void glyph_chevron(ImDrawList *dl, ImVec2 c, int dir, ImU32 color);
inline void glyph_search(ImDrawList *dl, ImVec2 c, ImU32 color);

// ── Preset picker popup ────────────────────────────────────────────────────
// File browser with folder navigation for preset packs.
//
//   Top:     breadcrumb path + back button.
//   Middle:  search field, scrollable list with folders first, then .ini files.
//            Folders are clickable to navigate into. Files have action glyphs.
//   Bottom:  "+ New" / "Save current" / "Refresh".
//
// Closes when the user clicks outside, presses Escape, or toggles the
// trigger button again.
void draw_preset_popup(mfx::runtime *rt, ImVec2 win_pos, ImVec2 win_size)
{
    if (!g_preset_popup_open) return;
    using namespace theme;

    // Lazy init: if we've never scanned the preset folder, do it now.
    if (g_preset_files_folder.empty())
    {
        const std::string cur = current_preset_path(rt);
        if (!cur.empty()) {
            fs::path p(cur);
            const std::string folder = p.parent_path().string();
            g_preset_root_folder = folder;
            refresh_preset_files(folder);
        }
        else {
            char buf[512];
            size_t s = sizeof(buf);
            rt->get_current_preset_path(buf, &s);
            if (buf[0]) {
                fs::path p(buf);
                const std::string folder = p.parent_path().string();
                g_preset_root_folder = folder;
                refresh_preset_files(folder);
            }
        }
    }

    // ── Position + clamping ──────────────────────────────────────────────
    const float pw = g_preset_popup_width;
    const float ph = 440.0f;
    float px = g_preset_popup_anchor.x;
    float py = g_preset_popup_anchor.y;
    if (px + pw > win_pos.x + win_size.x - 8.0f) px = win_pos.x + win_size.x - pw - 8.0f;
    if (py + ph > win_pos.y + win_size.y - 8.0f) py = win_pos.y + win_size.y - ph - 8.0f;
    if (px < win_pos.x + 8.0f) px = win_pos.x + 8.0f;
    if (py < win_pos.y + 8.0f) py = win_pos.y + 8.0f;

    ImGui::SetNextWindowPos (ImVec2(px, py));
    ImGui::SetNextWindowSize(ImVec2(pw, ph));
    ImGui::PushStyleVar  (ImGuiStyleVar_WindowRounding,   10.0f);
    ImGui::PushStyleVar  (ImGuiStyleVar_WindowBorderSize, 1.0f);
    ImGui::PushStyleVar  (ImGuiStyleVar_WindowPadding,    ImVec2(14, 12));
    ImGui::PushStyleColor(ImGuiCol_WindowBg,              to_vec4(col::bg_app));
    ImGui::PushStyleColor(ImGuiCol_Border,                to_vec4(col::border_default));

    static bool s_request_focus = true;
    if (s_request_focus) { ImGui::SetNextWindowFocus(); s_request_focus = false; }

    const bool win_open = ImGui::Begin("##mfx_preset_picker", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove     | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoSavedSettings);

    if (!win_open)
    {
        ImGui::End();
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar(3);
        return;
    }

    // Outside-click + Escape ⇒ close.
    const bool focused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false))
    {
        g_preset_popup_open = false;
    }
    else if (!focused && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
    {
        const ImVec2 m = ImGui::GetIO().MousePos;
        const ImVec2 wa = ImGui::GetWindowPos();
        const ImVec2 ws = ImGui::GetWindowSize();
        if (m.x < wa.x || m.y < wa.y || m.x > wa.x + ws.x || m.y > wa.y + ws.y)
            g_preset_popup_open = false;
    }

    // ── Header: Back button + breadcrumb path ────────────────────────────
    {
        ImDrawList *dlh = ImGui::GetWindowDrawList();
        const ImVec2 hp = ImGui::GetCursorScreenPos();
        const float ww = ImGui::GetContentRegionAvail().x;
        const bool can_go_back = !g_preset_nav_history.empty();

        // Back arrow button
        const float back_btn_w = 28.0f, back_btn_h = 26.0f;
        const ImVec2 ba(hp.x, hp.y);
        const ImVec2 bb(hp.x + back_btn_w, hp.y + back_btn_h);
        const ImVec2 mp = ImGui::GetIO().MousePos;
        const bool back_hov = can_go_back && mp.x >= ba.x && mp.x <= bb.x && mp.y >= ba.y && mp.y <= bb.y;

        if (can_go_back) {
            dlh->AddRectFilled(ba, bb, back_hov ? col::bg_card_hover : col::bg_card, 6.0f);
            dlh->AddRect(ba, bb, col::border_subtle, 6.0f);
        }
        // Left arrow glyph
        {
            const ImVec2 ctr((ba.x + bb.x) * 0.5f, (ba.y + bb.y) * 0.5f);
            const ImU32 ac = can_go_back ? (back_hov ? col::accent : col::text_secondary) : col::text_dimmest;
            dlh->AddLine(ImVec2(ctr.x + 3, ctr.y - 4), ImVec2(ctr.x - 3, ctr.y), ac, 1.8f);
            dlh->AddLine(ImVec2(ctr.x - 3, ctr.y), ImVec2(ctr.x + 3, ctr.y + 4), ac, 1.8f);
        }

        // Invisible button for back
        ImGui::SetCursorScreenPos(ba);
        ImGui::InvisibleButton("##mfx_preset_back", ImVec2(back_btn_w, back_btn_h));
        if (can_go_back && ImGui::IsItemClicked())
            navigate_preset_back();
        if (back_hov) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

        // Breadcrumb: show current folder name relative to root
        const float bread_x = hp.x + back_btn_w + 8.0f;
        const std::string cur_folder_name = folder_display_name(g_preset_files_folder);
        char breadcrumb[320] = "";
        if (g_preset_nav_history.empty()) {
            snprintf(breadcrumb, sizeof(breadcrumb), "%s", cur_folder_name.c_str());
        } else {
            // Show: root > ... > current
            const std::string root_name = folder_display_name(g_preset_root_folder);
            if (g_preset_nav_history.size() == 1)
                snprintf(breadcrumb, sizeof(breadcrumb), "%s > %s", root_name.c_str(), cur_folder_name.c_str());
            else
                snprintf(breadcrumb, sizeof(breadcrumb), "%s > ... > %s", root_name.c_str(), cur_folder_name.c_str());
        }
        const float bread_y = hp.y + (back_btn_h - ImGui::GetTextLineHeight()) * 0.5f;
        dlh->AddText(ImVec2(bread_x, bread_y), col::text_primary, breadcrumb);

        // Item count on the right
        char count_str[32];
        snprintf(count_str, sizeof(count_str), "%d files  %d folders",
                 (int)g_preset_files.size(), (int)g_preset_subdirs.size());
        const float cw = ImGui::CalcTextSize(count_str).x;
        dlh->AddText(ImVec2(hp.x + ww - cw, bread_y), col::text_dimmest, count_str);

        ImGui::Dummy(ImVec2(0, back_btn_h + 6.0f));
    }

    // ── Search (rounded card style) ─────────────────────────────────────
    {
        ImDrawList *dls = ImGui::GetWindowDrawList();
        const float sb_h = 32.0f;
        const float sb_w = ImGui::GetContentRegionAvail().x;
        const ImVec2 sa = ImGui::GetCursorScreenPos();
        const ImVec2 sb(sa.x + sb_w, sa.y + sb_h);
        const float sscy = (sa.y + sb.y) * 0.5f;
        dls->AddRectFilled(sa, sb, col::bg_card, 8.0f);
        dls->AddRect      (sa, sb, col::border_subtle, 8.0f);
        glyph_search(dls, ImVec2(sa.x + 12.0f, sscy), col::text_dim);

        ImGui::SetCursorScreenPos(ImVec2(sa.x + 26.0f, sa.y + 6.0f));
        ImGui::PushStyleColor(ImGuiCol_FrameBg,        ImVec4(0,0,0,0));
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0,0,0,0));
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive,  ImVec4(0,0,0,0));
        ImGui::PushStyleColor(ImGuiCol_Text,           to_vec4(col::text_primary));
        ImGui::PushItemWidth(sb_w - 36.0f);
        ImGui::InputTextWithHint("##mfx_preset_search", "Search presets & folders...",
                                 g_preset_search, sizeof(g_preset_search));
        ImGui::PopItemWidth();
        ImGui::PopStyleColor(4);
        ImGui::Dummy(ImVec2(0, sb_h + 6.0f));
    }

    // ── List (folders first, then .ini files) ────────────────────────────
    const std::string current = current_preset_path(rt);
    const float footer_h = 50.0f;
    const float list_h   = ph - 140.0f - footer_h;

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 2));
    ImGui::BeginChild("##mfx_preset_list", ImVec2(0, list_h), false);

    int visible_rows = 0;
    const float row_h = 32.0f;

    // ── Folder rows ──────────────────────────────────────────────────────
    for (int di = 0; di < (int)g_preset_subdirs.size(); ++di)
    {
        const std::string &dir_path = g_preset_subdirs[di];
        const std::string dir_name  = folder_display_name(dir_path);
        if (g_preset_search[0] && !icase_contains(dir_name.c_str(), g_preset_search))
            continue;
        ++visible_rows;

        const ImVec2 ra = ImGui::GetCursorScreenPos();
        const float  row_w = ImGui::GetContentRegionAvail().x;
        const ImVec2 rb(ra.x + row_w, ra.y + row_h);
        ImDrawList *dl = ImGui::GetWindowDrawList();

        const ImVec2 mp = ImGui::GetIO().MousePos;
        const bool row_hov = mp.x >= ra.x && mp.x <= rb.x && mp.y >= ra.y && mp.y <= rb.y;

        dl->AddRectFilled(ra, rb, row_hov ? col::bg_card_hover : IM_COL32(0,0,0,0), 6.0f);

        // Folder icon
        const ImVec2 icon_c(ra.x + 16.0f, (ra.y + rb.y) * 0.5f);
        glyph_folder(dl, icon_c, row_hov ? col::accent : col::text_dim);

        // Folder name
        dl->AddText(ImVec2(ra.x + 30.0f, ra.y + (row_h - ImGui::GetTextLineHeight()) * 0.5f),
                    row_hov ? col::accent_strong : col::text_secondary,
                    dir_name.c_str());

        // Right chevron indicating "enter"
        const ImVec2 chev_c(rb.x - 14.0f, (ra.y + rb.y) * 0.5f);
        glyph_chevron(dl, chev_c, 1, row_hov ? col::accent : col::text_dimmest);

        // Hit area
        ImGui::SetCursorScreenPos(ra);
        char fid[32]; snprintf(fid, sizeof(fid), "##dir_row_%d", di);
        ImGui::InvisibleButton(fid, ImVec2(row_w, row_h));
        if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
            navigate_preset_folder(dir_path);
        if (row_hov) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    }

    // Separator between folders and files (if both present)
    if (!g_preset_subdirs.empty() && !g_preset_files.empty() && visible_rows > 0)
    {
        ImGui::Dummy(ImVec2(0, 2.0f));
        ImDrawList *dl = ImGui::GetWindowDrawList();
        const ImVec2 sp = ImGui::GetCursorScreenPos();
        const float sw = ImGui::GetContentRegionAvail().x;
        dl->AddLine(ImVec2(sp.x + 8.0f, sp.y), ImVec2(sp.x + sw - 8.0f, sp.y), col::border_subtle);
        ImGui::Dummy(ImVec2(0, 4.0f));
    }

    // ── Preset file rows ─────────────────────────────────────────────────
    for (int i = 0; i < (int)g_preset_files.size(); ++i)
    {
        const std::string &path = g_preset_files[i];
        const std::string name  = preset_basename(path);
        if (g_preset_search[0] && !icase_contains(name.c_str(), g_preset_search))
            continue;
        ++visible_rows;

        const bool is_current = (path == current);
        const bool is_renaming = (g_preset_rename_idx == i);

        const ImVec2 ra   = ImGui::GetCursorScreenPos();
        const float  row_w= ImGui::GetContentRegionAvail().x;
        const ImVec2 rb   = ImVec2(ra.x + row_w, ra.y + row_h);

        ImDrawList *dl = ImGui::GetWindowDrawList();

        if (is_current)
            dl->AddRectFilled(ra, rb, col::accent_subtle, 6.0f);

        const ImVec2 mp = ImGui::GetIO().MousePos;
        const bool   row_hov = mp.x >= ra.x && mp.x <= rb.x && mp.y >= ra.y && mp.y <= rb.y;
        if (row_hov && !is_current)
            dl->AddRectFilled(ra, rb, col::bg_card_hover, 6.0f);

        // Current-marker dot.
        const ImVec2 dot(ra.x + 12.0f, (ra.y + rb.y) * 0.5f);
        if (is_current) dl->AddCircleFilled(dot, 3.5f, col::accent);
        else            dl->AddCircle      (dot, 3.5f, col::border_default);

        if (is_renaming)
        {
            ImGui::SetCursorScreenPos(ImVec2(ra.x + 22.0f, ra.y + 3.0f));
            ImGui::SetNextItemWidth(row_w - 110.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8, 4));
            ImGui::SetKeyboardFocusHere();
            const bool entered = ImGui::InputText("##rename_in",
                g_preset_rename_buf, sizeof(g_preset_rename_buf),
                ImGuiInputTextFlags_EnterReturnsTrue |
                ImGuiInputTextFlags_AutoSelectAll);
            ImGui::PopStyleVar();

            ImGui::SameLine(0, 6);
            const bool ok = action_button("Save", col::save_bg, col::save_border, col::save_text, 24.0f);
            ImGui::SameLine(0, 4);
            const bool cancel = action_button("X", col::bg_card, col::border_default, col::text_dim, 24.0f);

            const bool commit = entered || ok;
            if (commit && g_preset_rename_buf[0])
            {
                fs::path src(path);
                fs::path dst = src.parent_path() / (std::string(g_preset_rename_buf) + ".ini");
                std::error_code ec;
                if (!fs::exists(dst, ec) || dst == src)
                {
                    fs::rename(src, dst, ec);
                    if (!ec)
                    {
                        if (is_current) rt->set_current_preset_path(dst.string().c_str());
                        refresh_preset_files(g_preset_files_folder);
                    }
                }
                g_preset_rename_idx = -1;
            }
            else if (cancel)
            {
                g_preset_rename_idx = -1;
            }
        }
        else if (g_preset_delete_confirm == i)
        {
            dl->AddText(ImVec2(ra.x + 22.0f, ra.y + (row_h - ImGui::GetTextLineHeight()) * 0.5f),
                        col::dot_red, "Delete this preset?");
            ImGui::SetCursorScreenPos(ImVec2(rb.x - 130.0f, ra.y + 4.0f));
            const bool yes = action_button("Delete",
                IM_COL32(0xFF, 0x5F, 0x57, 0x22),
                IM_COL32(0xFF, 0x5F, 0x57, 0x55),
                col::dot_red, 24.0f);
            ImGui::SameLine(0, 4);
            const bool no = action_button("Cancel", col::bg_card, col::border_default, col::text_dim, 24.0f);
            if (yes)
            {
                std::error_code ec; fs::remove(path, ec);
                if (!ec) refresh_preset_files(g_preset_files_folder);
                g_preset_delete_confirm = -1;
            }
            else if (no)
            {
                g_preset_delete_confirm = -1;
            }
        }
        else
        {
            dl->AddText(ImVec2(ra.x + 22.0f, ra.y + (row_h - ImGui::GetTextLineHeight()) * 0.5f),
                        is_current ? col::accent_strong : col::text_secondary,
                        name.c_str());

            int  action = -1;
            bool any_btn_hov = false;
            if (row_hov)
            {
                const float bw = 22.0f, gap = 2.0f;
                const float sx = rb.x - 8.0f - 3.0f * bw - 2.0f * gap;
                for (int b = 0; b < 3; ++b)
                {
                    const ImVec2 ba(sx + b * (bw + gap), ra.y + (row_h - bw) * 0.5f);
                    const ImVec2 bb(ba.x + bw, ba.y + bw);
                    const bool bh = mp.x >= ba.x && mp.x <= bb.x && mp.y >= ba.y && mp.y <= bb.y;
                    any_btn_hov = any_btn_hov || bh;
                    const ImU32 c = bh ? col::accent : col::text_dimmer;

                    const ImVec2 ctr((ba.x + bb.x) * 0.5f, (ba.y + bb.y) * 0.5f);
                    if (bh) dl->AddRectFilled(ba, bb, col::bg_card_hover, 4.0f);

                    if (b == 0)
                    {
                        dl->AddLine(ImVec2(ctr.x - 4, ctr.y + 4),
                                    ImVec2(ctr.x + 3, ctr.y - 3), c, 1.5f);
                        dl->AddLine(ImVec2(ctr.x + 3, ctr.y - 3),
                                    ImVec2(ctr.x + 5, ctr.y - 1), c, 1.5f);
                        dl->AddLine(ImVec2(ctr.x - 4, ctr.y + 4),
                                    ImVec2(ctr.x - 6, ctr.y + 6), c, 1.5f);
                    }
                    else if (b == 1)
                    {
                        dl->AddRect(ImVec2(ctr.x - 5, ctr.y - 5),
                                    ImVec2(ctr.x + 2, ctr.y + 2), c, 1.0f, 0, 1.2f);
                        dl->AddRect(ImVec2(ctr.x - 2, ctr.y - 2),
                                    ImVec2(ctr.x + 5, ctr.y + 5), c, 1.0f, 0, 1.2f);
                    }
                    else
                    {
                        dl->AddLine(ImVec2(ctr.x - 4, ctr.y - 4),
                                    ImVec2(ctr.x + 4, ctr.y + 4), c, 1.5f);
                        dl->AddLine(ImVec2(ctr.x + 4, ctr.y - 4),
                                    ImVec2(ctr.x - 4, ctr.y + 4), c, 1.5f);
                    }

                    if (bh && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) action = b;
                }
            }

            ImGui::SetCursorScreenPos(ra);
            char rid[32]; snprintf(rid, sizeof(rid), "##preset_row_%d", i);
            ImGui::InvisibleButton(rid, ImVec2(row_w, row_h));
            const bool row_clicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);

            if (action == 0)
            {
                g_preset_rename_idx = i;
                copy_str(g_preset_rename_buf, sizeof(g_preset_rename_buf), name.c_str());
            }
            else if (action == 1)
            {
                fs::path src(path);
                fs::path dst = src.parent_path() / (preset_basename(path) + "_copy.ini");
                int n = 2;
                while (fs::exists(dst))
                    dst = src.parent_path() / (preset_basename(path) + "_copy" + std::to_string(n++) + ".ini");

                std::error_code ec;
                if (is_current) rt->export_current_preset(dst.string().c_str());
                else            fs::copy_file(src, dst, ec);
                refresh_preset_files(g_preset_files_folder);
            }
            else if (action == 2)
            {
                g_preset_delete_confirm = i;
            }
            else if (row_clicked && !any_btn_hov && !is_current)
            {
                rt->set_current_preset_path(path.c_str());
                g_preset_popup_open = false;
            }
        }
    }

    if (visible_rows == 0)
    {
        ImGui::Dummy(ImVec2(0, 12));
        ImGui::Indent(12);
        colored_text(col::text_dimmer,
                     (g_preset_files.empty() && g_preset_subdirs.empty())
                         ? "Empty folder."
                         : "No match for your filter.");
        ImGui::Unindent(12);
    }

    ImGui::EndChild();
    ImGui::PopStyleVar();

    // ── Footer ───────────────────────────────────────────────────────────
    ImGui::Dummy(ImVec2(0, 4));
    if (g_preset_new_mode)
    {
        ImGui::SetNextItemWidth(-160.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10, 6));
        ImGui::SetKeyboardFocusHere();
        const bool entered = ImGui::InputTextWithHint("##new_preset_in",
            "new_preset_name", g_preset_new_buf, sizeof(g_preset_new_buf),
            ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::PopStyleVar();
        ImGui::SameLine(0, 6);
        const bool ok     = action_button("Create",
            col::save_bg, col::save_border, col::save_text, 28.0f);
        ImGui::SameLine(0, 4);
        const bool cancel = action_button("Cancel",
            col::bg_card, col::border_default, col::text_dim, 28.0f);
        if ((entered || ok) && g_preset_new_buf[0])
        {
            fs::path dir = g_preset_files_folder;
            fs::path dst = dir / (std::string(g_preset_new_buf) + ".ini");
            if (!fs::exists(dst))
            {
                rt->export_current_preset(dst.string().c_str());
                rt->set_current_preset_path(dst.string().c_str());
                refresh_preset_files(g_preset_files_folder);
            }
            g_preset_new_mode = false;
            g_preset_new_buf[0] = 0;
        }
        else if (cancel)
        {
            g_preset_new_mode = false;
            g_preset_new_buf[0] = 0;
        }
    }
    else
    {
        if (action_button("+ New preset",
                          col::accent_subtle, col::border_accent, col::accent_strong, 30.0f))
        {
            g_preset_new_mode = true;
            g_preset_new_buf[0] = 0;
        }
        ImGui::SameLine(0, 6);
        if (action_button("Save current",
                          col::save_bg, col::save_border, col::save_text, 30.0f))
            rt->save_current_preset();
        ImGui::SameLine(0, 6);
        if (action_button("Refresh",
                          col::bg_card, col::border_default, col::text_dim, 30.0f))
            refresh_preset_files(g_preset_files_folder);
    }

    ImGui::End();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(3);

    // Re-arm focus request next time the popup opens.
    if (!g_preset_popup_open) s_request_focus = true;
}


// 
//  PIPELINE-EDITOR UI (Phase 5)  glyphs + helpers
// 
inline void glyph_drag_handle(ImDrawList *dl, ImVec2 c, ImU32 color)
{
    for (int row = 0; row < 3; ++row)
        for (int col = 0; col < 2; ++col)
            dl->AddCircleFilled(
                ImVec2(c.x + (col == 0 ? -3.0f : 3.0f), c.y + (row - 1) * 5.0f),
                1.6f, color, 6);
}
inline void glyph_eye(ImDrawList *dl, ImVec2 c, bool open, ImU32 color)
{
    if (open) {
        dl->PathClear();
        dl->PathArcTo(ImVec2(c.x, c.y + 4.5f), 7.5f, IM_PI, IM_PI * 2.0f, 12);
        dl->PathArcTo(ImVec2(c.x, c.y - 4.5f), 7.5f, 0.0f,  IM_PI,        12);
        dl->PathStroke(color, ImDrawFlags_Closed, 1.4f);
        dl->AddCircleFilled(c, 2.2f, color, 10);
    } else {
        dl->AddLine(ImVec2(c.x - 6.5f, c.y - 0.5f),
                    ImVec2(c.x + 6.5f, c.y + 2.0f), color, 1.5f);
    }
}
inline void glyph_plus(ImDrawList *dl, ImVec2 c, float r, ImU32 color)
{
    dl->AddLine(ImVec2(c.x - r, c.y), ImVec2(c.x + r, c.y), color, 1.6f);
    dl->AddLine(ImVec2(c.x, c.y - r), ImVec2(c.x, c.y + r), color, 1.6f);
}
inline void glyph_x(ImDrawList *dl, ImVec2 c, float r, ImU32 color)
{
    dl->AddLine(ImVec2(c.x - r, c.y - r), ImVec2(c.x + r, c.y + r), color, 1.6f);
    dl->AddLine(ImVec2(c.x + r, c.y - r), ImVec2(c.x - r, c.y + r), color, 1.6f);
}
inline void glyph_gear(ImDrawList *dl, ImVec2 c, ImU32 color)
{
    const float r1 = 4.0f, r2 = 6.5f;
    dl->AddCircle(c, r1, color, 16, 1.3f);
    dl->AddCircleFilled(c, 1.3f, color, 6);
    for (int i = 0; i < 6; ++i) {
        const float a = (float)i * (IM_PI * 2.0f / 6.0f);
        dl->AddLine(ImVec2(c.x + std::cos(a) * r1, c.y + std::sin(a) * r1),
                    ImVec2(c.x + std::cos(a) * r2, c.y + std::sin(a) * r2),
                    color, 1.3f);
    }
}
inline void glyph_bars(ImDrawList *dl, ImVec2 c, ImU32 color)
{
    const float bx = c.x - 5.0f, by = c.y + 5.0f;
    dl->AddRectFilled(ImVec2(bx,        by - 4.0f), ImVec2(bx + 2.0f,  by), color, 0.5f);
    dl->AddRectFilled(ImVec2(bx + 4.0f, by - 7.0f), ImVec2(bx + 6.0f,  by), color, 0.5f);
    dl->AddRectFilled(ImVec2(bx + 8.0f, by - 10.0f),ImVec2(bx + 10.0f, by), color, 0.5f);
}
inline void glyph_chevron(ImDrawList *dl, ImVec2 c, int dir, ImU32 color)
{
    const float r = 4.0f;
    ImVec2 a, b, d;
    if (dir == 0) {
        a = ImVec2(c.x - r, c.y - r * 0.5f);
        b = ImVec2(c.x,     c.y + r * 0.5f);
        d = ImVec2(c.x + r, c.y - r * 0.5f);
    } else {
        a = ImVec2(c.x - r * 0.5f, c.y - r);
        b = ImVec2(c.x + r * 0.5f, c.y);
        d = ImVec2(c.x - r * 0.5f, c.y + r);
    }
    dl->AddLine(a, b, color, 1.4f);
    dl->AddLine(b, d, color, 1.4f);
}
inline void glyph_search(ImDrawList *dl, ImVec2 c, ImU32 color)
{
    dl->AddCircle(ImVec2(c.x - 1.0f, c.y - 1.0f), 4.5f, color, 12, 1.4f);
    dl->AddLine(ImVec2(c.x + 2.4f, c.y + 2.4f),
                ImVec2(c.x + 5.5f, c.y + 5.5f), color, 1.6f);
}
inline void glyph_folder(ImDrawList *dl, ImVec2 c, ImU32 color)
{
    dl->AddRectFilled(ImVec2(c.x - 6.0f, c.y - 4.0f), ImVec2(c.x - 1.0f, c.y - 2.0f), color, 1.0f);
    dl->AddRectFilled(ImVec2(c.x - 6.0f, c.y - 3.0f), ImVec2(c.x + 6.0f, c.y + 4.0f), color, 1.5f);
}
// Two stacked sine waves  the MariusFX brand glyph.
inline void glyph_wave(ImDrawList *dl, ImVec2 c, float w, ImU32 color)
{
    const int segs = 22;
    const float amp = w * 0.11f;
    const float spacing_y = w * 0.18f;
    for (int row = 0; row < 2; ++row) {
        const float yc = c.y + (row == 0 ? -spacing_y : spacing_y);
        const float phase = (row == 0) ? 0.0f : IM_PI;
        for (int i = 0; i < segs; ++i) {
            const float t0 = i      / float(segs);
            const float t1 = (i + 1)/ float(segs);
            const float x0 = c.x - w * 0.5f + t0 * w;
            const float x1 = c.x - w * 0.5f + t1 * w;
            const float y0 = yc + std::sin(t0 * IM_PI * 2.0f + phase) * amp;
            const float y1 = yc + std::sin(t1 * IM_PI * 2.0f + phase) * amp;
            dl->AddLine(ImVec2(x0, y0), ImVec2(x1, y1), color, 2.0f);
        }
    }
}

// ── Glyphs for icon sidebar ────────────────────────────────────────────────
inline void glyph_layers(ImDrawList *dl, ImVec2 c, float s, ImU32 color)
{
    // Stacked-layers icon (shaders/pipeline)
    const float h = s * 0.28f;
    for (int i = 0; i < 3; ++i) {
        const float y = c.y - s * 0.3f + i * h * 1.5f;
        const float w = s * 0.45f - i * s * 0.06f;
        dl->AddLine(ImVec2(c.x - w, y), ImVec2(c.x, y - h), color, 1.6f);
        dl->AddLine(ImVec2(c.x, y - h), ImVec2(c.x + w, y), color, 1.6f);
        dl->AddLine(ImVec2(c.x + w, y), ImVec2(c.x, y + h), color, 1.6f);
        dl->AddLine(ImVec2(c.x, y + h), ImVec2(c.x - w, y), color, 1.6f);
    }
}
inline void glyph_chart(ImDrawList *dl, ImVec2 c, float s, ImU32 color)
{
    // Bar chart icon (statistics)
    const float bw = s * 0.14f;
    const float base_y = c.y + s * 0.35f;
    const float heights[4] = { 0.3f, 0.55f, 0.75f, 0.45f };
    for (int i = 0; i < 4; ++i) {
        const float x = c.x - s * 0.3f + i * s * 0.2f;
        const float h = s * heights[i];
        dl->AddRectFilled(ImVec2(x, base_y - h), ImVec2(x + bw, base_y), color, 1.0f);
    }
}
inline void glyph_cog(ImDrawList *dl, ImVec2 c, float s, ImU32 color)
{
    // Gear/cog icon (settings)
    const float r1 = s * 0.22f, r2 = s * 0.38f;
    dl->AddCircle(c, r1, color, 16, 1.5f);
    for (int i = 0; i < 8; ++i) {
        const float a = (float)i * (IM_PI * 2.0f / 8.0f);
        dl->AddLine(ImVec2(c.x + std::cos(a) * r1, c.y + std::sin(a) * r1),
                    ImVec2(c.x + std::cos(a) * r2, c.y + std::sin(a) * r2),
                    color, 1.5f);
    }
}

// ── Vertical icon sidebar (Map Studio style) ──────────────────────────────
// Renders a narrow column on the far left with mode icons + labels.
// Each icon button switches the right panel mode (SHEET_NONE = pipeline
// params, SHEET_STATISTICS, SHEET_SETTINGS).
void pl_draw_icon_sidebar(ImVec2 origin, float width, float height)
{
    using namespace theme;
    ImDrawList *dl = ImGui::GetWindowDrawList();
    const ImVec2 bk_end(origin.x + width, origin.y + height);

    // Background
    dl->AddRectFilled(origin, bk_end, col::bg_icon_sidebar);
    // Right border
    dl->AddLine(ImVec2(bk_end.x - 1, origin.y),
                ImVec2(bk_end.x - 1, bk_end.y), col::border_default);

    // Title label at top
    const float cx = origin.x + width * 0.5f;
    float y = origin.y + 16.0f;

    // Brand glyph (small wave)
    {
        const float sz = 28.0f;
        const ImVec2 a(cx - sz * 0.5f, y);
        const ImVec2 b(cx + sz * 0.5f, y + sz);
        dl->AddRectFilled(a, b, col::accent_subtle, 7.0f);
        dl->AddRect      (a, b, col::accent,        7.0f);
        glyph_wave(dl, ImVec2(cx, y + sz * 0.5f), 16.0f, col::accent_strong);
        y += sz + 20.0f;
    }

    // Mode buttons
    struct SidebarEntry {
        const char *id;
        const char *label;
        Sheet       sheet;
        void (*glyph)(ImDrawList*, ImVec2, float, ImU32);
    };
    const SidebarEntry entries[] = {
        { "##sb_shaders",  "Shaders",  SHEET_NONE,       glyph_layers },
        { "##sb_stats",    "Stats",    SHEET_STATISTICS,  glyph_chart  },
        { "##sb_settings", "Settings", SHEET_SETTINGS,    glyph_cog    },
    };

    for (const auto &e : entries) {
        const float btn_h = 56.0f;
        const bool  active = (g_active_sheet == e.sheet);
        const ImVec2 ba(origin.x + 4.0f, y);
        const ImVec2 bb(origin.x + width - 4.0f, y + btn_h);
        const ImVec2 ic(cx, ba.y + 20.0f);

        ImGui::SetCursorScreenPos(ba);
        ImGui::InvisibleButton(e.id, ImVec2(bb.x - ba.x, btn_h));
        const bool hov = ImGui::IsItemHovered();
        if (hov) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        if (ImGui::IsItemClicked()) {
            g_active_sheet = active ? SHEET_NONE : e.sheet;
            // For shaders, always go to SHEET_NONE
            if (e.sheet == SHEET_NONE) g_active_sheet = SHEET_NONE;
        }

        // Background + accent bar for active
        if (active) {
            dl->AddRectFilled(ba, bb, col::accent_subtle, 8.0f);
            dl->AddRectFilled(ImVec2(ba.x, ba.y + 8), ImVec2(ba.x + 2.5f, bb.y - 8),
                              col::accent, 1.5f);
        } else if (hov) {
            dl->AddRectFilled(ba, bb, col::bg_card_hover, 8.0f);
        }

        // Icon
        const ImU32 icon_col = active ? col::accent_strong : (hov ? col::text_primary : col::text_dim);
        e.glyph(dl, ic, 24.0f, icon_col);

        // Label
        const ImU32 lbl_col = active ? col::accent_strong : (hov ? col::text_secondary : col::text_dimmer);
        const float lw = ImGui::CalcTextSize(e.label).x;
        dl->AddText(ImVec2(cx - lw * 0.5f, ba.y + 38.0f), lbl_col, e.label);

        y += btn_h + 4.0f;
    }
}

// ── Header bar (replaces old toolbar) ──────────────────────────────────────
// Horizontal title bar: brand title + subtitle on the left, preset pill +
// dock toggle + reload on the right. Sits to the right of the icon sidebar.
void pl_draw_header(mfx::runtime *rt, ImVec2 origin, float width, float h)
{
    using namespace theme;
    ImDrawList *dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(origin, ImVec2(origin.x + width, origin.y + h), col::bg_titlebar);
    dl->AddLine(ImVec2(origin.x, origin.y + h - 1),
                ImVec2(origin.x + width, origin.y + h - 1),
                col::border_default);

    const float pad = 18.0f;
    const float cy  = origin.y + h * 0.5f;

    // Left: Title + subtitle
    {
        const float tx = origin.x + pad;
        dl->AddText(ImVec2(tx, origin.y + 14.0f), col::text_dimmest, "MARIUSFX");
        // Context-dependent subtitle
        const char *subtitle = "Pipeline Editor";
        if (g_active_sheet == SHEET_STATISTICS) subtitle = "Performance";
        else if (g_active_sheet == SHEET_SETTINGS) subtitle = "Settings";
        dl->AddText(ImVec2(tx, origin.y + 14.0f + ImGui::GetTextLineHeight() + 2.0f),
                    col::text_primary, subtitle);
    }

    // Right cluster (right-to-left)
    float right_x = origin.x + width - pad;
    const float gap = 8.0f;

    auto icon_btn = [&](const char *id, bool active) -> std::pair<ImVec2, bool> {
        const float bw = 34.0f, bh = 32.0f;
        right_x -= bw;
        const ImVec2 a(right_x, cy - bh * 0.5f);
        const ImVec2 b(right_x + bw, cy + bh * 0.5f);
        ImGui::SetCursorScreenPos(a);
        ImGui::InvisibleButton(id, ImVec2(bw, bh));
        const bool hov = ImGui::IsItemHovered();
        const bool clk = ImGui::IsItemClicked();
        if (hov) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        dl->AddRectFilled(a, b, (hov || active) ? col::bg_card_hover : col::bg_card, 8.0f);
        dl->AddRect      (a, b, active ? col::border_accent : col::border_subtle, 8.0f);
        right_x -= gap;
        return { ImVec2((a.x + b.x) * 0.5f, cy), clk };
    };

    // Dock toggle
    {
        auto [cc, clk] = icon_btn("##mfx_dock", false);
        const ImVec2 ga(cc.x - 7.0f, cc.y - 7.0f);
        const ImVec2 gb(cc.x + 7.0f, cc.y + 7.0f);
        dl->AddRect(ga, gb, col::text_dim, 1.0f, 0, 1.2f);
        if (g_dock_side == DOCK_LEFT)
            dl->AddRectFilled(ga, ImVec2(ga.x + (gb.x - ga.x) * 0.42f, gb.y), col::accent);
        else
            dl->AddRectFilled(ImVec2(gb.x - (gb.x - ga.x) * 0.42f, ga.y), gb, col::accent);
        if (clk) {
            g_dock_side = (g_dock_side == DOCK_LEFT) ? DOCK_RIGHT : DOCK_LEFT;
            g_force_size = true;
        }
    }

    // Reload button
    {
        auto [cc, clk] = icon_btn("##mfx_reload", false);
        // Circular-arrow glyph
        const float r = 6.0f;
        dl->PathClear();
        dl->PathArcTo(cc, r, -IM_PI * 0.8f, IM_PI * 0.6f, 16);
        dl->PathStroke(col::text_dim, 0, 1.5f);
        // Arrow tip
        const float ax = cc.x + r * std::cos(IM_PI * 0.6f);
        const float ay = cc.y + r * std::sin(IM_PI * 0.6f);
        dl->AddTriangleFilled(
            ImVec2(ax - 3, ay - 4), ImVec2(ax + 3, ay - 1), ImVec2(ax, ay + 3), col::text_dim);
        if (clk) rt_reload_all(rt);
    }

    // Preset pill
    {
        const std::string cur_name = preset_basename(current_preset_path(rt));
        const char *label = cur_name.empty() ? "(no preset)" : cur_name.c_str();
        const float pill_h = 34.0f;
        const float tx_w   = ImGui::CalcTextSize(label).x;
        const float pw     = std::max(170.0f, tx_w + 64.0f);
        right_x -= pw;
        const ImVec2 a(right_x, cy - pill_h * 0.5f);
        const ImVec2 b(right_x + pw, cy + pill_h * 0.5f);

        ImGui::SetCursorScreenPos(a);
        ImGui::InvisibleButton("##mfx_preset_pill", ImVec2(pw, pill_h));
        const bool hov = ImGui::IsItemHovered();
        if (hov) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        if (ImGui::IsItemClicked()) {
            g_preset_popup_open  = !g_preset_popup_open;
            g_preset_popup_anchor = ImVec2(a.x, b.y + 8.0f);
            g_preset_popup_width  = std::max(pw + 100.0f, 440.0f);
        }
        dl->AddRectFilled(a, b, hov ? col::bg_card_hover : col::bg_card, pill_h * 0.5f);
        dl->AddRect      (a, b,
                          g_preset_popup_open ? col::border_accent : col::border_default,
                          pill_h * 0.5f);
        glyph_folder(dl, ImVec2(a.x + 16.0f, cy), col::text_dim);
        dl->AddText(ImVec2(a.x + 30.0f, cy - ImGui::GetTextLineHeight() * 0.5f),
                    col::text_primary, label);
        glyph_chevron(dl, ImVec2(b.x - 14.0f, cy + 1.0f), 0,
                      g_preset_popup_open ? col::accent : col::text_dim);
        right_x -= gap;
    }
}

// Render `text` at `pos` with the substring matching `needle` highlighted
// in `hi` (rest in `base`). Case-insensitive. Returns the advance width.
// When needle is empty / null / no match, falls back to a plain AddText.
float draw_text_with_match(ImDrawList *dl, ImVec2 pos, const char *text,
                           const char *needle, ImU32 base, ImU32 hi)
{
    if (!text)         return 0.0f;
    if (!needle || !needle[0]) {
        dl->AddText(pos, base, text);
        return ImGui::CalcTextSize(text).x;
    }
    const size_t nlen = std::strlen(needle);
    int match_pos = -1;
    for (const char *p = text; *p; ++p) {
        bool ok = true;
        for (size_t i = 0; i < nlen; ++i) {
            char x = p[i], y = needle[i];
            if (!x) { ok = false; break; }
            if (x >= 'A' && x <= 'Z') x += 32;
            if (y >= 'A' && y <= 'Z') y += 32;
            if (x != y) { ok = false; break; }
        }
        if (ok) { match_pos = (int)(p - text); break; }
    }
    if (match_pos < 0) {
        dl->AddText(pos, base, text);
        return ImGui::CalcTextSize(text).x;
    }
    char buf[512];
    float adv = 0.0f;
    if (match_pos > 0) {
        const int n = std::min(match_pos, (int)sizeof(buf) - 1);
        std::memcpy(buf, text, n); buf[n] = 0;
        dl->AddText(pos, base, buf);
        const float w = ImGui::CalcTextSize(buf).x;
        pos.x += w; adv += w;
    }
    {
        const int n = std::min((int)nlen, (int)sizeof(buf) - 1);
        std::memcpy(buf, text + match_pos, n); buf[n] = 0;
        dl->AddText(pos, hi, buf);
        const float w = ImGui::CalcTextSize(buf).x;
        pos.x += w; adv += w;
    }
    if (text[match_pos + nlen]) {
        dl->AddText(pos, base, text + match_pos + nlen);
        adv += ImGui::CalcTextSize(text + match_pos + nlen).x;
    }
    return adv;
}

// ── Single technique row (unified  one list, toggle on the right) ─────────
// Click anywhere on the row body = select for editing.
// Click the toggle switch on the right = enable/disable the shader.
// Drag handle is shown only for enabled rows (where order matters).
//
// `allow_drag`: when false (Available section), drag-drop is fully disabled
// and the drag-handle slot is suppressed  there's no concept of "order"
// for disabled shaders since they're alphabetically sorted.