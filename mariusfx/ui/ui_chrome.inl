// ============================================================================ // ui_chrome.inl - included by ui.cpp inside namespace mariusfx::ui::{anonymous}. // This is not a stand-alone translation unit. It exists only as a logical // module to keep ui.cpp browsable. Do not compile or include directly. // ============================================================================ 
// ── Preset picker popup ────────────────────────────────────────────────────
// Floating window listing every `.ini` in the current preset folder.
//
//   Top:     title, folder subtext.
//   Middle:  search field, scrollable list. Each row has on-hover action
//            glyphs (rename  duplicate  delete) and a current-marker dot.
//            Clicking anywhere outside the action zone loads the preset.
//   Bottom:  "+ New" creates an empty preset using the current settings as
//            a template (export_current_preset); "Save current" overwrites
//            the loaded preset on disk; "Open folder" shells out.
//
// Closes when the user clicks outside, presses Escape, or toggles the
// trigger button again.
void draw_preset_popup(mfx::runtime *rt, ImVec2 win_pos, ImVec2 win_size)
{
    if (!g_preset_popup_open) return;
    using namespace theme;

    // Lazy init: if we've never scanned the preset folder, do it now.
    // This happens on the first time the user opens the preset picker.
    if (g_preset_files_folder.empty())
    {
        const std::string cur = current_preset_path(rt);
        if (!cur.empty()) {
            fs::path p(cur);
            refresh_preset_files(p.parent_path().string());
        }
        else {
            // Fallback: no preset loaded yet. Scan the default ReShade preset
            // folder (same directory as the DLL). The runtime will tell us.
            char buf[512];
            size_t s = sizeof(buf);
            rt->get_current_preset_path(buf, &s);
            if (buf[0]) {
                fs::path p(buf);
                refresh_preset_files(p.parent_path().string());
            }
        }
    }

    // ── Position + clamping ──────────────────────────────────────────────
    const float pw = g_preset_popup_width;
    const float ph = 380.0f;
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
        // Anything outside our window bounds closes us. We re-poll the
        // window rect because ImGui has clipped/translated it.
        const ImVec2 wa = ImGui::GetWindowPos();
        const ImVec2 ws = ImGui::GetWindowSize();
        if (m.x < wa.x || m.y < wa.y || m.x > wa.x + ws.x || m.y > wa.y + ws.y)
            g_preset_popup_open = false;
    }

    // ── Header (compact: PRESETS label + count + trimmed folder path) ────
    {
        ImDrawList *dlh = ImGui::GetWindowDrawList();
        const ImVec2 hp = ImGui::GetCursorScreenPos();
        char hdr[64];
        snprintf(hdr, sizeof(hdr), "PRESETS    %d", (int)g_preset_files.size());
        dlh->AddText(hp, col::text_disabled, hdr);

        const std::string &folder = g_preset_files_folder;
        char trimmed[260] = "";
        const char *f = folder.c_str();
        if (folder.size() > 60)
            snprintf(trimmed, sizeof(trimmed), "%s", f + folder.size() - 59);
        else
            copy_str(trimmed, sizeof(trimmed), f);
        const float tw = ImGui::CalcTextSize(trimmed).x;
        const float ww = ImGui::GetContentRegionAvail().x;
        dlh->AddText(ImVec2(hp.x + ww - tw, hp.y),
                     col::text_dimmest, trimmed[0] ? trimmed : "(no folder)");
        ImGui::Dummy(ImVec2(0, ImGui::GetTextLineHeight() + 8.0f));
    }

    // ── Search (rounded card style, matches the pipeline column) ─────────
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
        ImGui::InputTextWithHint("##mfx_preset_search", "Filter presets",
                                 g_preset_search, sizeof(g_preset_search));
        ImGui::PopItemWidth();
        ImGui::PopStyleColor(4);
        ImGui::Dummy(ImVec2(0, sb_h + 6.0f));
    }

    // ── List ─────────────────────────────────────────────────────────────
    const std::string current = current_preset_path(rt);
    const float footer_h = 50.0f;
    const float list_h   = ph - 100.0f - footer_h;

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 2));
    ImGui::BeginChild("##mfx_preset_list", ImVec2(0, list_h), false);

    int visible_rows = 0;
    for (int i = 0; i < (int)g_preset_files.size(); ++i)
    {
        const std::string &path = g_preset_files[i];
        const std::string name  = preset_basename(path);
        if (g_preset_search[0] && !icase_contains(name.c_str(), g_preset_search))
            continue;
        ++visible_rows;

        const bool is_current = (path == current);
        const bool is_renaming = (g_preset_rename_idx == i);

        // Reserve a uniform row height regardless of mode.
        const float row_h = 32.0f;
        const ImVec2 ra   = ImGui::GetCursorScreenPos();
        const float  row_w= ImGui::GetContentRegionAvail().x;
        const ImVec2 rb   = ImVec2(ra.x + row_w, ra.y + row_h);

        ImDrawList *dl = ImGui::GetWindowDrawList();

        // Default background.
        if (is_current)
            dl->AddRectFilled(ra, rb, col::accent_subtle, 6.0f);

        // Mouse hover (computed manually so action sub-buttons keep priority).
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
            // ── Inline rename field ──────────────────────────────────
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
            // ── Inline delete confirmation ───────────────────────────
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
            // ── Normal row ───────────────────────────────────────────
            dl->AddText(ImVec2(ra.x + 22.0f, ra.y + (row_h - ImGui::GetTextLineHeight()) * 0.5f),
                        is_current ? col::accent_strong : col::text_secondary,
                        name.c_str());

            // Action glyphs on hover.
            int  action = -1;        // 0=rename, 1=duplicate, 2=delete
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
                        // Pencil: diagonal line + small triangle tip.
                        dl->AddLine(ImVec2(ctr.x - 4, ctr.y + 4),
                                    ImVec2(ctr.x + 3, ctr.y - 3), c, 1.5f);
                        dl->AddLine(ImVec2(ctr.x + 3, ctr.y - 3),
                                    ImVec2(ctr.x + 5, ctr.y - 1), c, 1.5f);
                        dl->AddLine(ImVec2(ctr.x - 4, ctr.y + 4),
                                    ImVec2(ctr.x - 6, ctr.y + 6), c, 1.5f);
                    }
                    else if (b == 1)
                    {
                        // Duplicate: two overlapping squares.
                        dl->AddRect(ImVec2(ctr.x - 5, ctr.y - 5),
                                    ImVec2(ctr.x + 2, ctr.y + 2), c, 1.0f, 0, 1.2f);
                        dl->AddRect(ImVec2(ctr.x - 2, ctr.y - 2),
                                    ImVec2(ctr.x + 5, ctr.y + 5), c, 1.0f, 0, 1.2f);
                    }
                    else
                    {
                        // Trash / X.
                        dl->AddLine(ImVec2(ctr.x - 4, ctr.y - 4),
                                    ImVec2(ctr.x + 4, ctr.y + 4), c, 1.5f);
                        dl->AddLine(ImVec2(ctr.x + 4, ctr.y - 4),
                                    ImVec2(ctr.x - 4, ctr.y + 4), c, 1.5f);
                    }

                    if (bh && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) action = b;
                }
            }

            // Whole-row hit area (consumes the rest of the click).
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
                // Duplicate = export the CURRENT live state into <name>_copy.ini
                // if duplicating the loaded preset, else copy_file the source.
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
                     g_preset_files.empty() ? "No .ini files in this folder."
                                            : "No preset matches your filter.");
        ImGui::Unindent(12);
    }

    ImGui::EndChild();
    ImGui::PopStyleVar();

    // ── Footer ───────────────────────────────────────────────────────────
    ImGui::Dummy(ImVec2(0, 4));
    if (g_preset_new_mode)
    {
        // Inline "name your new preset" entry  saves the CURRENT live
        // state into <name>.ini and switches to it.
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

// ── Toolbar (52px tall) ────────────────────────────────────────────────────
void pl_draw_toolbar(mfx::runtime *rt, ImVec2 origin, float width, float h)
{
    using namespace theme;
    ImDrawList *dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(origin, ImVec2(origin.x + width, origin.y + h), col::bg_titlebar);
    dl->AddLine(ImVec2(origin.x, origin.y + h - 1),
                ImVec2(origin.x + width, origin.y + h - 1),
                col::border_default);

    const float pad = 14.0f, gap = 10.0f;
    const float cy  = origin.y + h * 0.5f;
    float x = origin.x + pad;

    // Logo: stacked-wave brand glyph.
    {
        const float sz = 32.0f;
        const ImVec2 a(x, cy - sz * 0.5f);
        const ImVec2 b(x + sz, cy + sz * 0.5f);
        dl->AddRectFilled(a, b, col::accent_subtle, 8.0f);
        dl->AddRect      (a, b, col::accent,        8.0f);
        glyph_wave(dl, ImVec2(a.x + sz * 0.5f, a.y + sz * 0.5f), 18.0f, col::accent_strong);
        x += sz + gap + 4.0f;
    }

    // Preset pill.
    {
        const std::string cur_name = preset_basename(current_preset_path(rt));
        const char *label = cur_name.empty() ? "(no preset)" : cur_name.c_str();
        const float pill_h = 32.0f;
        const float tx_w   = ImGui::CalcTextSize(label).x;
        const float pw     = std::max(170.0f, tx_w + 64.0f);
        const ImVec2 a(x, cy - pill_h * 0.5f);
        const ImVec2 b(x + pw, cy + pill_h * 0.5f);

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
        x += pw + gap;
    }

    // Right-cluster icons (right-to-left).
    float right_x = origin.x + width - pad;
    auto icon_btn = [&](const char *id, bool active) -> std::pair<ImVec2, bool> {
        const float bw = 32.0f, bh = 30.0f;
        right_x -= bw;
        const ImVec2 a(right_x, cy - bh * 0.5f);
        const ImVec2 b(right_x + bw, cy + bh * 0.5f);
        ImGui::SetCursorScreenPos(a);
        ImGui::InvisibleButton(id, ImVec2(bw, bh));
        const bool hov = ImGui::IsItemHovered();
        const bool clk = ImGui::IsItemClicked();
        if (hov) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        dl->AddRectFilled(a, b, (hov || active) ? col::bg_card_hover : col::bg_card, 6.0f);
        dl->AddRect      (a, b, active ? col::border_accent : col::border_default, 6.0f);
        right_x -= 6.0f;
        return { ImVec2((a.x + b.x) * 0.5f, cy), clk };
    };

    // Dock toggle.
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
    // Settings.
    {
        const bool act = g_active_sheet == SHEET_SETTINGS;
        auto [cc, clk] = icon_btn("##mfx_settings", act);
        glyph_gear(dl, cc, act ? col::accent : col::text_dim);
        if (clk) g_active_sheet = act ? SHEET_NONE : SHEET_SETTINGS;
    }
    // Stats.
    {
        const bool act = g_active_sheet == SHEET_STATISTICS;
        auto [cc, clk] = icon_btn("##mfx_stats", act);
        glyph_bars(dl, cc, act ? col::accent : col::text_dim);
        if (clk) g_active_sheet = act ? SHEET_NONE : SHEET_STATISTICS;
    }
    (void)x; // The middle space between the preset pill and the right
             // cluster is intentionally left empty  the shader search
             // bar lives at the top of the pipeline column instead.
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