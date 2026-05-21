// ============================================================================ // ui_params.inl - included by ui.cpp inside namespace mariusfx::ui::{anonymous}. // This is not a stand-alone translation unit. It exists only as a logical // module to keep ui.cpp browsable. Do not compile or include directly. // ============================================================================ 
void pl_draw_param_editor(mfx::runtime *rt, ImVec2 origin, float width, float height)
{
    using namespace theme;
    ImDrawList *dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(origin, ImVec2(origin.x + width, origin.y + height), col::bg_app);

    const int sel_idx = find_selected_index();
    ImGui::SetCursorScreenPos(origin);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::BeginChild("##mfx_params_col", ImVec2(width, height), false,
                      ImGuiWindowFlags_NoBackground);

    // ── Empty state ─────────────────────────────────────────────────
    if (sel_idx < 0) {
        const char *l1 = "Nothing selected";
        const char *l2 = "Click a shader on the left to edit its parameters.";
        const char *l3 = "Drag rows to reorder  Right-click for options  Ctrl+F to search";
        const float w1 = ImGui::CalcTextSize(l1).x;
        const float w2 = ImGui::CalcTextSize(l2).x;
        const float w3 = ImGui::CalcTextSize(l3).x;
        const float cy = origin.y + height * 0.36f;
        dl->AddText(ImVec2(origin.x + (width - w1) * 0.5f, cy),         col::text_muted,   l1);
        dl->AddText(ImVec2(origin.x + (width - w2) * 0.5f, cy + 28.0f), col::text_dimmer,  l2);
        dl->AddText(ImVec2(origin.x + (width - w3) * 0.5f, cy + 60.0f), col::text_dimmest, l3);
        ImGui::EndChild();
        ImGui::PopStyleVar();
        return;
    }

    // Lazy refresh of the uniform list when the selection changes.
    const TechRow *sel = &g_techs[sel_idx];
    if (std::strcmp(g_uniforms_for_key, g_selected_key) != 0 ||
        std::strcmp(g_uniforms_for_effect, sel->effect_full) != 0)
    {
        copy_str(g_uniforms_for_key,    sizeof(g_uniforms_for_key),    g_selected_key);
        copy_str(g_uniforms_for_effect, sizeof(g_uniforms_for_effect), sel->effect_full);
        refresh_uniform_list(rt, sel->effect_full);
    }

    // ── Header card (single row, no stacked chevrons) ───────────────
    const float pad = L::panel_pad;
    const ImVec2 hca(origin.x + pad,         origin.y + pad);
    const ImVec2 hcb(origin.x + width - pad, hca.y + L::pe_header_h);
    dl->AddRectFilled(hca, hcb, col::bg_card, size::radius_card);
    dl->AddRect      (hca, hcb, col::border_default, size::radius_card);
    // Accent bar on the left edge mirrors the row's enabled state.
    dl->AddRectFilled(ImVec2(hca.x, hca.y + 8), ImVec2(hca.x + 3, hcb.y - 8),
                      sel->enabled ? col::accent : col::border_default, 1.5f);

    // Toggle on the right.
    const float sw_w = 40.0f, sw_h = 22.0f;
    const float sw_x = hcb.x - 14.0f - sw_w;
    const float sw_y = hca.y + (L::pe_header_h - sw_h) * 0.5f;
    ImGui::SetCursorScreenPos(ImVec2(sw_x, sw_y));
    char tid[160]; snprintf(tid, sizeof(tid), "##phdr_t_%s", sel->name);
    if (toggle_switch(tid, sel->enabled, sw_w, sw_h))
        rt->set_technique_state(sel->handle, !sel->enabled);

    // Perf badges to the left of the toggle. They share one column,
    // right-aligned, so their widths don't push the title around.
    uint64_t cpu_ns = 0, gpu_ns = 0;
    rt_get_technique_timing(rt, sel->handle, &cpu_ns, &gpu_ns);
    char gbuf[32]; snprintf(gbuf, sizeof(gbuf), "GPU %.2f ms", gpu_ns / 1'000'000.0f);
    char cbuf[32]; snprintf(cbuf, sizeof(cbuf), "CPU %.2f ms", cpu_ns / 1'000'000.0f);
    const float gw = ImGui::CalcTextSize(gbuf).x;
    const float cw = ImGui::CalcTextSize(cbuf).x;
    const float perf_right = sw_x - 18.0f;
    dl->AddText(ImVec2(perf_right - gw, hca.y + 12.0f),
                sel->enabled ? col::stat_fps : col::text_dimmer, gbuf);
    dl->AddText(ImVec2(perf_right - cw, hca.y + 12.0f + ImGui::GetTextLineHeight() + 2.0f),
                col::text_dim, cbuf);

    // Title (technique name) + subtitle (effect file). The subtitle is
    // suppressed when it would just repeat the technique name (e.g. a
    // .fx file that exposes a single same-named technique).
    const float tx = hca.x + 18.0f;
    dl->AddText(ImVec2(tx, hca.y + 12.0f), col::text_primary, sel->name);
    const bool subtitle_distinct = std::strcmp(sel->name, sel->effect_short) != 0;
    if (subtitle_distinct) {
        dl->AddText(ImVec2(tx, hca.y + 12.0f + ImGui::GetTextLineHeight() + 4.0f),
                    col::text_dimmer, sel->effect_short);
    }
    // DEBUG: Show enumeration stats
    dl->AddText(ImVec2(tx, hca.y + 12.0f + ImGui::GetTextLineHeight() * 2 + 8.0f),
                IM_COL32(255, 200, 0, 255), g_uniforms_for_effect);

    // ── Body toolbar (Expand / Collapse / Reset all) ────────────────
    const float bar_y = hcb.y + L::pe_header_gap;
    const ImVec2 bar_a(origin.x + pad,         bar_y);
    const ImVec2 bar_b(origin.x + width - pad, bar_y + L::pe_bar_h);

    // Compute counts once for the left-hand summary ("3 categories  24 parameters").
    int n_cats = 0;
    {
        std::vector<std::string> seen;
        for (const auto &u : g_uniforms) {
            const std::string c = u.category;
            bool found = false;
            for (const auto &s : seen) if (s == c) { found = true; break; }
            if (!found) { seen.push_back(c); ++n_cats; }
        }
    }
    char summary[80];
    if (g_uniforms.empty())
        snprintf(summary, sizeof(summary), "No parameters");
    else
        snprintf(summary, sizeof(summary), "%d categor%s    %d parameter%s",
                 n_cats, n_cats == 1 ? "y" : "ies",
                 (int)g_uniforms.size(), g_uniforms.size() == 1 ? "" : "s");
    dl->AddText(ImVec2(bar_a.x, bar_a.y + (L::pe_bar_h - ImGui::GetTextLineHeight()) * 0.5f),
                col::text_dimmer, summary);

    // Right-aligned action cluster: ▾ all  ▴ all  ↺ all.
    auto bar_btn = [&](float right_x, const char *id_, const char *tooltip,
                       int chev_dir /*-1 = reset glyph*/) -> std::pair<float, bool>
    {
        const float bw = 28.0f, bh = L::pe_bar_h - 4.0f;
        const ImVec2 ba(right_x - bw, bar_a.y + 2.0f);
        const ImVec2 bb_(ba.x + bw,   ba.y + bh);
        ImGui::SetCursorScreenPos(ba);
        ImGui::InvisibleButton(id_, ImVec2(bw, bh));
        const bool hov = ImGui::IsItemHovered();
        const bool clk = ImGui::IsItemClicked();
        if (hov) { ImGui::SetMouseCursor(ImGuiMouseCursor_Hand); ImGui::SetTooltip("%s", tooltip); }
        dl->AddRectFilled(ba, bb_, hov ? col::bg_card_hover : col::bg_card, 6.0f);
        dl->AddRect      (ba, bb_, hov ? col::border_accent : col::border_subtle, 6.0f);
        const ImVec2 cc((ba.x + bb_.x) * 0.5f, (ba.y + bb_.y) * 0.5f);
        if (chev_dir < 0) draw_reset_glyph(cc, 6.0f, hov ? col::accent : col::text_dim);
        else              glyph_chevron(dl, cc, chev_dir, hov ? col::accent : col::text_dim);
        return { ba.x - 6.0f, clk };
    };
    float rx = bar_b.x;
    {
        auto [nx, clk] = bar_btn(rx, "##mfx_pe_reset_all", "Reset all uniforms", -1);
        if (clk) {
            rt->enumerate_uniform_variables(sel->effect_full,
                [](effect_runtime *r, effect_uniform_variable v) {
                    r->reset_uniform_value(v);
                });
            rt->save_current_preset();
            g_last_change_time = ImGui::GetTime();
        }
        rx = nx;
    }
    {
        auto [nx, clk] = bar_btn(rx, "##mfx_pe_collapse", "Collapse all categories", 2 /*up*/);
        if (clk) g_collapse_all_req = -1;
        rx = nx;
    }
    {
        auto [nx, clk] = bar_btn(rx, "##mfx_pe_expand", "Expand all categories", 0 /*down*/);
        if (clk) g_collapse_all_req = +1;
        rx = nx;
    }

    // ── Scroll body ────────────────────────────────────────────────
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,    ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,   ImVec2(8, 4));
    ImGui::PushStyleVar(ImGuiStyleVar_GrabMinSize,    12.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding,  6.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, ImVec2(4, 4));

    const float body_top  = pad + L::pe_header_h + L::pe_header_gap +
                            L::pe_bar_h + L::pe_bar_gap;
    const float body_h    = height - body_top - 12.0f;
    ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + body_top));
    ImGui::BeginChild("##mfx_params_body",
                      ImVec2(width, body_h > 60.0f ? body_h : 60.0f),
                      false, ImGuiWindowFlags_AlwaysVerticalScrollbar);

    // Inner left/right padding via Indent so the body width derived from
    // ContentRegionAvail in draw_uniform stays correct.
    ImGui::Indent(pad);

    if (g_uniforms.empty()) {
        ImGui::Dummy(ImVec2(0, 16));
        colored_text(col::text_dimmer, "This shader has no exposed parameters.");
    }
    else {
        // ReShade-style in-order iteration. Categories are detected as the
        // ui_category annotation changes between consecutive uniforms; we
        // do NOT pre-aggregate or de-duplicate them. This preserves the
        // shader author's intent when they intersperse parameters between
        // logical groups (e.g. Marty's MartyMods uses the same category
        // multiple times to anchor visual blocks). The previous pre-build
        // approach merged those into a single section, scrambling the
        // layout. See source/runtime_gui.cpp:3547 for the upstream pattern.
        std::string current_category;   // last category we opened a section for
        bool        is_first_section = true;
        bool        category_open    = true; // contents visible (uncategorised default)
        bool        any_section_open = false; // for trailing PopID balance

        ImGui::PushID(sel->effect_full); // namespace IDs by effect to avoid collisions

        int uniform_seq = 0; // monotonic counter so duplicate-named uniforms get unique IDs

        for (const UniformRow &u : g_uniforms)
        {
            // ── Section header on category transition ─────────────────
            if (std::strcmp(u.category, current_category.c_str()) != 0)
            {
                current_category = u.category;
                if (current_category.empty()) {
                    // Uncategorised  no header, contents are always visible.
                    // Insert a small breathing gap so the row doesn't bump
                    // straight into the previous section's last widget.
                    if (!is_first_section)
                        ImGui::Dummy(ImVec2(0, 6.0f));
                    category_open = true;
                }
                else {
                    char up[96]; size_t n = 0;
                    for (size_t i = 0; u.category[i] && n + 1 < sizeof(up); ++i, ++n)
                        up[n] = (u.category[i] >= 'a' && u.category[i] <= 'z')
                                  ? (char)(u.category[i] - 32) : u.category[i];
                    up[n] = 0;
                    char cid[200];
                    // The "###" suffix anchors the ImGui ID independently of the
                    // visible label, AND we tag with uniform_seq so an identical
                    // category name appearing later still opens a fresh section.
                    snprintf(cid, sizeof(cid), "%s###cat_%d_%s", up, uniform_seq, u.category);

                    ImGui::Dummy(ImVec2(0, is_first_section ? 6.0f : 12.0f));
                    ImGui::PushStyleColor(ImGuiCol_Header,        IM_COL32(0,0,0,0));
                    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, col::bg_card_hover);
                    ImGui::PushStyleColor(ImGuiCol_HeaderActive,  col::bg_card_hover);
                    ImGui::PushStyleColor(ImGuiCol_Text,          to_vec4(col::text_disabled));
                    if (g_collapse_all_req != 0)
                        ImGui::SetNextItemOpen(g_collapse_all_req > 0);
                    category_open = ImGui::CollapsingHeader(cid, ImGuiTreeNodeFlags_DefaultOpen);
                    ImGui::PopStyleColor(4);

                    if (category_open) {
                        ImGui::Dummy(ImVec2(0, 4));
                        any_section_open = true;
                    }
                }
                is_first_section = false;
            }

            if (!category_open) { ++uniform_seq; continue; }

            // ── Pre-widget annotations (ReShade parity) ──────────────
            // ui_spacing  inject N blank lines BEFORE the widget. Used by
            // shader authors to visually break up dense parameter groups.
            for (int s = 0; s < u.ui_spacing; ++s) ImGui::Spacing();
            // ui_text  user-facing prose displayed above the widget.
            if (u.ui_text[0]) {
                ImGui::PushTextWrapPos();
                colored_text(col::text_dim, "%s", u.ui_text);
                ImGui::PopTextWrapPos();
            }

            ImGui::PushID(uniform_seq);
            draw_uniform(rt, u);
            ImGui::PopID();
            ++uniform_seq;
        }
        (void)any_section_open;
        ImGui::PopID();
        ImGui::Dummy(ImVec2(0, 12));
    }

    ImGui::Unindent(pad);
    ImGui::EndChild();
    ImGui::PopStyleVar(5);

    // Consume the collapse-all request  applied once per frame.
    g_collapse_all_req = 0;

    ImGui::EndChild();
    ImGui::PopStyleVar();
}

// ── Statusbar (28px tall, bottom) ──────────────────────────────────────────