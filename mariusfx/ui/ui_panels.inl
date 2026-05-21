// ============================================================================ // ui_panels.inl - included by ui.cpp inside namespace mariusfx::ui::{anonymous}. // This is not a stand-alone translation unit. It exists only as a logical // module to keep ui.cpp browsable. Do not compile or include directly. // ============================================================================ 
void pl_draw_statusbar(mfx::runtime *rt, ImVec2 origin, float width)
{
    using namespace theme;
    ImDrawList *dl = ImGui::GetWindowDrawList();
    const float h = 28.0f;
    dl->AddRectFilled(origin, ImVec2(origin.x + width, origin.y + h), col::bg_titlebar);
    dl->AddLine(origin, ImVec2(origin.x + width, origin.y), col::border_default);

    const float cy = origin.y + (h - ImGui::GetTextLineHeight()) * 0.5f;

    int n_enabled = 0;
    for (const auto &t : g_techs)
        if (!t.hidden && t.enabled) ++n_enabled;

    const float fps = ImGui::GetIO().Framerate;
    const float ms  = (fps > 0.0f) ? (1000.0f / fps) : 0.0f;
    char b_fps[24]; snprintf(b_fps, sizeof(b_fps), "%.0f fps", fps);
    char b_ms [24]; snprintf(b_ms,  sizeof(b_ms),  "%.1f ms",  ms);
    char b_eff[40]; snprintf(b_eff, sizeof(b_eff), "%d effect%s active",
                             n_enabled, n_enabled == 1 ? "" : "s");

    float x = origin.x + 16.0f;
    auto seg = [&](const char *t, ImU32 c, bool last) {
        dl->AddText(ImVec2(x, cy), c, t);
        x += ImGui::CalcTextSize(t).x;
        if (!last) {
            dl->AddText(ImVec2(x + 10.0f, cy), col::text_dimmest, "");
            x += 22.0f;
        }
    };
    seg(b_fps, col::stat_fps, false);
    seg(b_ms,  col::stat_lat, false);
    seg(b_eff, col::text_dim, true);

    // ── Saving indicator ─────────────────────────────────────────────
    // Visible for ~1.2 s after the last uniform change. The host
    // persists the change synchronously on every set_uniform_value_*
    // call, so this is purely a visual confirmation — no debouncing or
    // async save wrapper involved.
    const double since_change = ImGui::GetTime() - g_last_change_time;
    if (since_change < 1.2 && g_last_change_time > 0.0)
    {
        const float fade = (since_change < 0.6)
                           ? 1.0f
                           : 1.0f - (float)(since_change - 0.6) / 0.6f;
        const ImU32 sc = IM_COL32(0x4F, 0xC3, 0xF7,
                                  (int)(140.0f + 115.0f * fade));
        const ImVec2 dot_c(x + 14.0f, origin.y + h * 0.5f);
        dl->AddCircleFilled(dot_c, 3.5f, sc, 12);
        dl->AddText(ImVec2(x + 22.0f, cy), sc, "Saving");
        x += 22.0f + ImGui::CalcTextSize("Saving").x + 14.0f;
    }

    // Right side: hot-reload version badge + current preset.
    // The badge fades the freshly-loaded indicator over the first second.
    char b_ver[32];
    snprintf(b_ver, sizeof(b_ver), "v%d", g_load_count);
    const float since = (float)(ImGui::GetTime() - g_load_time_sec);
    const float fresh = (since < 1.5f)
                        ? (since < 0.5f ? 1.0f : 1.0f - (since - 0.5f) / 1.0f)
                        : 0.0f;
    const ImU32 ver_col = (fresh > 0.0f)
        ? IM_COL32(0x4F, 0xC3, 0xF7,
                   (int)(120.0f + 135.0f * fresh))
        : col::text_dimmest;
    const float vw = ImGui::CalcTextSize(b_ver).x;

    const std::string pn = preset_basename(current_preset_path(rt));
    float right_x = origin.x + width - 16.0f;
    if (!pn.empty()) {
        const float pw = ImGui::CalcTextSize(pn.c_str()).x;
        glyph_folder(dl, ImVec2(right_x - pw - 12.0f, origin.y + h * 0.5f),
                     col::text_dimmer);
        dl->AddText(ImVec2(right_x - pw, cy), col::text_dim, pn.c_str());
        right_x -= pw + 24.0f;
        // separator
        dl->AddText(ImVec2(right_x - 2.0f, cy), col::text_dimmest, "");
        right_x -= 14.0f;
    }
    // Version badge (always shown, accent-coloured for ~1.5s after a reload).
    dl->AddText(ImVec2(right_x - vw, cy), ver_col, b_ver);
}

// ── Right-panel: shared header (mode title + back-to-params chip) ──────────
//
// The Stats and Settings panels share the same shell as the Param editor:
// a header card at the top, a scrollable body below. The header announces
// the active mode and offers a quick way back to PARAMS.

// ── Right-panel: STATISTICS ────────────────────────────────────────────────
//
// MariusFX-owned performance view. Three blocks:
//   1. KPI strip    FPS, frame ms, total GPU cost, % of 60fps budget used.
//   2. Frame graph  last 240 ms with overlaid Avg / 1% Low / Peak readout.
//   3. Per-shader cost  sorted desc, with % column so the user can spot
//                        which shader is eating the budget.
//
// FPS comes from ImGui::GetIO().Framerate, which is `1.0 / mean(delta_t)`
// over the last 60 frames. Frame ms is derived as 1000 / fps (canonical,
// matches the canonical formula to 2 decimals). Per-shader timings come
// from the host's get_technique_timing extension, already in nanoseconds.
void pl_draw_stats_panel(mfx::runtime *rt, ImVec2 origin, float width, float height)
{
    using namespace theme;
    ImDrawList *dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(origin, ImVec2(origin.x + width, origin.y + height), col::bg_app);

    ImGui::SetCursorScreenPos(origin);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::BeginChild("##mfx_stats_col", ImVec2(width, height), false,
                      ImGuiWindowFlags_NoBackground);

    const float pad     = 18.0f;
    const float strip_h = 44.0f;

    // ── Compact title strip ─────────────────────────────────────────
    {
        const ImVec2 ca(origin.x + pad, origin.y + pad);
        const ImVec2 cb(origin.x + width - pad, ca.y + strip_h);
        dl->AddRectFilled(ImVec2(ca.x, ca.y + 8), ImVec2(ca.x + 3, cb.y - 8),
                          col::accent, 1.5f);
        dl->AddText(ImVec2(ca.x + 16, ca.y + (strip_h - ImGui::GetTextLineHeight()) * 0.5f),
                    col::text_primary, "Performance");
    }

    // ── KPI tiles row ───────────────────────────────────────────────
    const float tiles_y = origin.y + pad + strip_h + 12.0f;
    const float tile_h  = 70.0f;
    const float gap     = 10.0f;
    const float tile_w  = (width - pad * 2.0f - gap * 3.0f) / 4.0f;

    const float fps   = ImGui::GetIO().Framerate;
    const float ms    = (fps > 0.0f) ? (1000.0f / fps) : 0.0f;
    int   active_n    = 0;
    uint64_t total_gpu_ns = 0;
    for (const auto &t : g_techs) {
        if (t.hidden || !t.enabled) continue;
        ++active_n;
        uint64_t cpu = 0, gpu = 0;
        rt_get_technique_timing(rt, t.handle, &cpu, &gpu);
        total_gpu_ns += gpu;
    }
    const float total_gpu_ms = total_gpu_ns / 1'000'000.0f;
    // % of the 60fps budget (16.67 ms) consumed by all active shaders.
    const float budget_pct   = (total_gpu_ms / 16.67f) * 100.0f;

    auto kpi_tile = [&](int idx, const char *label, const char *value, ImU32 vcol) {
        const ImVec2 ta(origin.x + pad + idx * (tile_w + gap), tiles_y);
        const ImVec2 tb(ta.x + tile_w, ta.y + tile_h);
        dl->AddRectFilled(ta, tb, col::bg_card, size::radius_card);
        dl->AddRect      (ta, tb, col::border_subtle, size::radius_card);
        dl->AddText(ImVec2(ta.x + 12, ta.y + 10), col::text_dimmest, label);
        dl->AddText(ImVec2(ta.x + 12, ta.y + 28), vcol, value);
    };
    char b_fps[32], b_ms[32], b_gpu[32], b_bud[32];
    snprintf(b_fps, sizeof(b_fps), "%.0f",       fps);
    snprintf(b_ms,  sizeof(b_ms),  "%.2f ms",    ms);
    snprintf(b_gpu, sizeof(b_gpu), "%.2f ms",    total_gpu_ms);
    snprintf(b_bud, sizeof(b_bud), "%d %%",      (int)(budget_pct + 0.5f));

    // Threshold colours: green when comfortable, yellow above target.
    const ImU32 fps_col = (fps   >= 55.0f) ? col::stat_fps : col::stat_lat;
    const ImU32 ms_col  = (ms    <= 18.0f) ? col::text_primary : col::stat_lat;
    const ImU32 gpu_col = (total_gpu_ms <= 5.0f) ? col::stat_fps : col::stat_lat;
    const ImU32 bud_col = (budget_pct  <= 60.0f) ? col::stat_fps : col::stat_lat;

    kpi_tile(0, "FPS",          b_fps, fps_col);
    kpi_tile(1, "FRAME TIME",   b_ms,  ms_col);
    kpi_tile(2, "GPU SHADERS",  b_gpu, gpu_col);
    kpi_tile(3, "60FPS BUDGET", b_bud, bud_col);

    // ── Frame-time graph ────────────────────────────────────────────
    // Walk the ring in one pass to compute Avg / 1% Low / Peak so the
    // graph card doubles as a stability readout.
    const float graph_y = tiles_y + tile_h + 14.0f;
    const float graph_h = 124.0f;
    const ImVec2 ga(origin.x + pad, graph_y);
    const ImVec2 gb(origin.x + width - pad, graph_y + graph_h);
    dl->AddRectFilled(ga, gb, col::bg_card, size::radius_card);
    dl->AddRect      (ga, gb, col::border_subtle, size::radius_card);
    dl->AddText(ImVec2(ga.x + 12, ga.y + 8), col::text_dimmest,
                "FRAME TIME  last 240 samples");

    // One pass over the ring buffer to compute Avg / Peak + the working
    // copy used to derive the 99th-percentile (a.k.a. "1 % low"  the
    // worst-1 % frame time, which is what people actually feel as a
    // stutter).
    std::vector<float> sorted;
    sorted.reserve(FT_SAMPLES);
    float sum_ms = 0.0f, max_ms = 16.7f, peak_ms = 0.0f;
    for (int i = 0; i < (int)FT_SAMPLES; ++i) {
        const float v = ft_ring_get(i);
        if (v <= 0.0f) continue;
        sorted.push_back(v);
        sum_ms += v;
        if (v > max_ms)  max_ms  = v;
        if (v > peak_ms) peak_ms = v;
    }
    const int   sample_count = (int)sorted.size();
    const float avg_ms       = (sample_count > 0) ? sum_ms / (float)sample_count : 0.0f;
    float onepct_low = 0.0f;
    if (!sorted.empty()) {
        std::sort(sorted.begin(), sorted.end());
        int idx = (int)(0.99 * sorted.size());
        if (idx >= (int)sorted.size()) idx = (int)sorted.size() - 1;
        onepct_low = sorted[idx];
    }

    // Plot bounds.
    const float plot_x0 = ga.x + 16.0f;
    const float plot_y0 = ga.y + 32.0f;
    const float plot_w  = (gb.x - 16.0f) - plot_x0;
    const float plot_h  = (gb.y - 28.0f) - plot_y0;

    // 60fps reference line at 16.7 ms.
    {
        const float ry = plot_y0 + plot_h - (16.7f / max_ms) * plot_h;
        dl->AddLine(ImVec2(plot_x0, ry), ImVec2(plot_x0 + plot_w, ry),
                    col::border_subtle, 1.0f);
        dl->AddText(ImVec2(plot_x0 + plot_w - 36.0f, ry - 14.0f),
                    col::text_dimmest, "60fps");
    }
    // History line + filled area underneath.
    for (int i = 1; i < (int)FT_SAMPLES; ++i) {
        const float x0 = plot_x0 + (i - 1) / float(FT_SAMPLES - 1) * plot_w;
        const float x1 = plot_x0 + (i)     / float(FT_SAMPLES - 1) * plot_w;
        const float v0 = ft_ring_get(i - 1), v1 = ft_ring_get(i);
        const float y0 = plot_y0 + plot_h - (v0 / max_ms) * plot_h;
        const float y1 = plot_y0 + plot_h - (v1 / max_ms) * plot_h;
        const float by = plot_y0 + plot_h;
        dl->AddQuadFilled(ImVec2(x0, y0), ImVec2(x1, y1),
                          ImVec2(x1, by), ImVec2(x0, by),
                          IM_COL32(0x4F, 0xC3, 0xF7, 26));
        dl->AddLine(ImVec2(x0, y0), ImVec2(x1, y1), col::accent, 1.6f);
    }
    // Avg / 1% Low / Peak readout  three figures across the bottom.
    {
        const float ry = gb.y - 22.0f;
        char l_avg[24], l_low[24], l_peak[24];
        snprintf(l_avg,  sizeof(l_avg),  "Avg %.2f ms",  avg_ms);
        snprintf(l_low,  sizeof(l_low),  "1%% low %.2f ms", onepct_low);
        snprintf(l_peak, sizeof(l_peak), "Peak %.2f ms", peak_ms);
        const float third = plot_w / 3.0f;
        dl->AddText(ImVec2(plot_x0,                    ry), col::text_dim,    l_avg);
        dl->AddText(ImVec2(plot_x0 + third,            ry), col::text_dim,    l_low);
        dl->AddText(ImVec2(plot_x0 + third * 2.0f,     ry), col::text_dim,    l_peak);
    }

    // ── Per-shader cost breakdown ───────────────────────────────────
    const float list_y = graph_y + graph_h + 14.0f;
    const ImVec2 la(origin.x + pad, list_y);
    const ImVec2 lb(origin.x + width - pad, origin.y + height - pad);
    if (lb.y > la.y + 60.0f)
    {
        dl->AddRectFilled(la, lb, col::bg_card, size::radius_card);
        dl->AddRect      (la, lb, col::border_subtle, size::radius_card);

        char hdr[64];
        snprintf(hdr, sizeof(hdr), "PER-SHADER COST  %d active", active_n);
        dl->AddText(ImVec2(la.x + 12, la.y + 8), col::text_dimmest, hdr);

        struct Row { const TechRow *t; uint64_t gpu; };
        std::vector<Row> rows; rows.reserve(g_techs.size());
        uint64_t max_gpu = 1;
        for (const auto &t : g_techs) {
            if (t.hidden || !t.enabled) continue;
            uint64_t cpu = 0, gpu = 0;
            rt_get_technique_timing(rt, t.handle, &cpu, &gpu);
            rows.push_back({ &t, gpu });
            if (gpu > max_gpu) max_gpu = gpu;
        }
        std::sort(rows.begin(), rows.end(),
                  [](const Row &a, const Row &b){ return a.gpu > b.gpu; });

        const float row_h  = 26.0f;
        const float head_h = 32.0f;
        ImGui::SetCursorScreenPos(ImVec2(la.x, la.y + head_h));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::BeginChild("##mfx_stats_breakdown",
                          ImVec2(lb.x - la.x, lb.y - la.y - head_h - 8.0f),
                          false, ImGuiWindowFlags_AlwaysVerticalScrollbar);
        const float name_x = la.x + 12.0f;
        const float bar_x0 = la.x + 200.0f;
        const float bar_x1 = lb.x - 130.0f;
        const float pct_x  = lb.x - 88.0f;
        const float ms_x   = lb.x - 14.0f;

        for (size_t i = 0; i < rows.size(); ++i) {
            const Row &r = rows[i];
            const ImVec2 ra = ImGui::GetCursorScreenPos();
            const float gpu_ms = r.gpu / 1'000'000.0f;
            const float pct    = (total_gpu_ns > 0)
                                 ? (float)((double)r.gpu / (double)total_gpu_ns) * 100.0f
                                 : 0.0f;
            const float frac   = (r.gpu > 0) ? (float)((double)r.gpu / (double)max_gpu) : 0.0f;

            // Name.
            dl->AddText(ImVec2(name_x, ra.y + (row_h - ImGui::GetTextLineHeight()) * 0.5f),
                        col::text_primary, r.t->name);
            // Bar  bg track + filled portion in accent (red if eating
            // most of the budget).
            const float by  = ra.y + row_h * 0.5f;
            const float bx1 = bar_x0 + (bar_x1 - bar_x0) * frac;
            dl->AddRectFilled(ImVec2(bar_x0, by - 5), ImVec2(bar_x1, by + 5),
                              col::bg_input, 5);
            dl->AddRectFilled(ImVec2(bar_x0, by - 5), ImVec2(bx1, by + 5),
                              (frac > 0.5f) ? col::stat_lat : col::accent, 5);
            // % of total.
            char pct_buf[16]; snprintf(pct_buf, sizeof(pct_buf), "%.0f%%", pct);
            const float pcw = ImGui::CalcTextSize(pct_buf).x;
            dl->AddText(ImVec2(pct_x - pcw, ra.y + (row_h - ImGui::GetTextLineHeight()) * 0.5f),
                        col::text_dim, pct_buf);
            // ms.
            char ms_buf[24]; snprintf(ms_buf, sizeof(ms_buf), "%.2f ms", gpu_ms);
            const float mw = ImGui::CalcTextSize(ms_buf).x;
            dl->AddText(ImVec2(ms_x - mw, ra.y + (row_h - ImGui::GetTextLineHeight()) * 0.5f),
                        col::text_primary, ms_buf);

            ImGui::Dummy(ImVec2(0, row_h));
        }
        if (rows.empty()) {
            ImGui::Dummy(ImVec2(0, 8));
            colored_text(col::text_dimmer,
                         "  No active shader. Enable a few on the left.");
        }
        ImGui::EndChild();
        ImGui::PopStyleVar();
    }

    // ── Compilation Errors ──────────────────────────────────────────
    // Display shaders that failed to compile
    const float errors_y = list_y + (lb.y - la.y) + 14.0f;
    const ImVec2 ea(origin.x + pad, errors_y);
    const ImVec2 eb(origin.x + width - pad, origin.y + height - pad);
    if (eb.y > ea.y + 60.0f)
    {
        // Count failed shaders (techniques that are hidden due to compilation failure)
        int failed_count = 0;
        for (const auto &t : g_techs) {
            if (t.hidden) failed_count++;
        }

        if (failed_count > 0)
        {
            dl->AddRectFilled(ea, eb, col::bg_card, size::radius_card);
            dl->AddRect      (ea, eb, IM_COL32(0xFF, 0x5F, 0x57, 0x88), size::radius_card);

            char hdr[64];
            snprintf(hdr, sizeof(hdr), "COMPILATION ERRORS  %d shader(s) failed", failed_count);
            dl->AddText(ImVec2(ea.x + 12, ea.y + 8), IM_COL32(0xFF, 0x5F, 0x57, 0xFF), hdr);

            const float err_row_h = 24.0f;
            const float err_head_h = 32.0f;
            ImGui::SetCursorScreenPos(ImVec2(ea.x, ea.y + err_head_h));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
            ImGui::BeginChild("##mfx_stats_errors",
                              ImVec2(eb.x - ea.x, eb.y - ea.y - err_head_h - 8.0f),
                              false, ImGuiWindowFlags_AlwaysVerticalScrollbar);

            for (const auto &t : g_techs) {
                if (!t.hidden) continue;
                const ImVec2 ra = ImGui::GetCursorScreenPos();
                
                // Error icon (red circle with X)
                const float icon_x = ea.x + 12.0f;
                const float icon_y = ra.y + err_row_h * 0.5f;
                dl->AddCircleFilled(ImVec2(icon_x, icon_y), 6.0f, IM_COL32(0xFF, 0x5F, 0x57, 0xFF));
                dl->AddText(ImVec2(icon_x - 3.0f, icon_y - 7.0f), IM_COL32(0xFF, 0xFF, 0xFF, 0xFF), "x");

                // Shader name
                dl->AddText(ImVec2(ea.x + 32.0f, ra.y + (err_row_h - ImGui::GetTextLineHeight()) * 0.5f),
                            col::text_primary, t.name);

                // Error message (generic for now, will be enhanced later)
                dl->AddText(ImVec2(ea.x + 280.0f, ra.y + (err_row_h - ImGui::GetTextLineHeight()) * 0.5f),
                            col::text_dim, "Failed to compile");

                ImGui::Dummy(ImVec2(0, err_row_h));
            }

            ImGui::EndChild();
            ImGui::PopStyleVar();
        }
    }

    ImGui::EndChild();
    ImGui::PopStyleVar();
}

// ── Right-panel: SETTINGS ──────────────────────────────────────────────────
//
// Auto-layout flow: every section stacks naturally, scrolls correctly.
// All settings are wired to the runtime via the host API bridge so edits
// actually take effect and get persisted.
void pl_draw_settings_panel(mfx::runtime *rt, ImVec2 origin, float width, float height)
{
    using namespace theme;

    // First-time sync from runtime
    settings_sync_from_runtime(rt);

    ImDrawList *dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(origin, ImVec2(origin.x + width, origin.y + height), col::bg_app);

    ImGui::SetCursorScreenPos(origin);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::BeginChild("##mfx_settings_col", ImVec2(width, height), false,
                      ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_AlwaysVerticalScrollbar);

    const float pad   = 18.0f;
    const float inner = width - pad * 2.0f;

    // ── Section helpers (auto-layout + channel split for correct z-order) ──
    ImDrawListSplitter splitter;
    float sec_x = 0, sec_top = 0;
    auto section_begin = [&](const char *title) {
        ImGui::Dummy(ImVec2(0, 10.0f));
        const ImVec2 cur = ImGui::GetCursorScreenPos();
        sec_x   = cur.x + pad;
        sec_top = cur.y;
        // Split: channel 0 = card bg (behind), channel 1 = content (front)
        splitter.Split(dl, 2);
        splitter.SetCurrentChannel(dl, 1);
        dl->AddText(ImVec2(sec_x, sec_top), col::text_dimmest, title);
        ImGui::SetCursorScreenPos(ImVec2(sec_x, sec_top + 22.0f));
    };
    auto section_end = [&]() {
        ImGui::Dummy(ImVec2(0, 4.0f));
        const ImVec2 cur = ImGui::GetCursorScreenPos();
        const float card_h = cur.y - sec_top + 8.0f;
        const ImVec2 sa(sec_x - 14.0f, sec_top - 8.0f);
        const ImVec2 sb(sec_x + inner, sec_top + card_h);
        // Draw card on channel 0 (behind all content)
        splitter.SetCurrentChannel(dl, 0);
        dl->AddRectFilled(sa, sb, col::bg_card, size::radius_card);
        dl->AddRect      (sa, sb, col::border_subtle, size::radius_card);
        splitter.Merge(dl);
        ImGui::Dummy(ImVec2(0, 2.0f));
    };

    // Reusable pill-shaped button
    auto pill_btn = [&](const char *id, const char *label, float bw, float bh = 30.0f) -> bool {
        const ImVec2 ba = ImGui::GetCursorScreenPos();
        const ImVec2 bb(ba.x + bw, ba.y + bh);
        ImGui::InvisibleButton(id, ImVec2(bw, bh));
        const bool hov = ImGui::IsItemHovered();
        const bool clk = ImGui::IsItemClicked();
        if (hov) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        dl->AddRectFilled(ba, bb, hov ? col::bg_card_hover : col::bg_app, bh * 0.5f);
        dl->AddRect      (ba, bb, hov ? col::border_accent : col::border_default, bh * 0.5f);
        const float lw = ImGui::CalcTextSize(label).x;
        dl->AddText(ImVec2((ba.x + bb.x - lw) * 0.5f,
                           (ba.y + bb.y - ImGui::GetTextLineHeight()) * 0.5f),
                    hov ? col::accent : col::text_primary, label);
        return clk;
    };

    // ── Title ────────────────────────────────────────────────────────
    {
        ImGui::Dummy(ImVec2(0, pad));
        const ImVec2 tp = ImGui::GetCursorScreenPos();
        dl->AddRectFilled(ImVec2(tp.x + pad, tp.y), ImVec2(tp.x + pad + 3, tp.y + 24),
                          col::accent_strong, 1.5f);
        dl->AddText(ImVec2(tp.x + pad + 14, tp.y + 2), col::text_primary, "Settings");
        ImGui::Dummy(ImVec2(0, 32.0f));
    }

    // ── PIPELINE ─────────────────────────────────────────────────────
    section_begin("PIPELINE");
    {
        const bool fx_on = rt->get_effects_state();
        const bool perf  = rt_get_performance_mode(rt);

        ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::SetCursorScreenPos(p);
        if (toggle_switch("##mfx_set_fx", fx_on, 44.0f, 24.0f))
            rt->set_effects_state(!fx_on);
        dl->AddText(ImVec2(p.x + 56.0f, p.y + 4.0f), col::text_primary, "Master enable");
        ImGui::Dummy(ImVec2(0, 30.0f));

        p = ImGui::GetCursorScreenPos();
        ImGui::SetCursorScreenPos(p);
        if (toggle_switch("##mfx_set_perf", perf, 44.0f, 24.0f))
            rt_set_performance_mode(rt, !perf);
        dl->AddText(ImVec2(p.x + 56.0f, p.y + 4.0f), col::text_primary, "Performance mode");
        ImGui::Dummy(ImVec2(0, 28.0f));
    }
    section_end();

    // ── LAYOUT ───────────────────────────────────────────────────────
    section_begin("LAYOUT");
    {
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const char *labels[2] = { "Dock left", "Dock right" };
        const float bw = (inner - 28.0f - 8.0f) * 0.5f, bh = 30.0f;
        for (int i = 0; i < 2; ++i) {
            const bool sel = (g_dock_side == DOCK_LEFT  && i == 0) ||
                             (g_dock_side == DOCK_RIGHT && i == 1);
            const ImVec2 ba(p.x + i * (bw + 8.0f), p.y);
            const ImVec2 bb(ba.x + bw, ba.y + bh);
            ImGui::SetCursorScreenPos(ba);
            char id[24]; snprintf(id, sizeof(id), "##mfx_set_dock_%d", i);
            ImGui::InvisibleButton(id, ImVec2(bw, bh));
            const bool hov = ImGui::IsItemHovered();
            if (hov) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            if (ImGui::IsItemClicked()) {
                g_dock_side = (i == 0) ? DOCK_LEFT : DOCK_RIGHT;
                g_force_size = true;
            }
            dl->AddRectFilled(ba, bb,
                              sel ? col::accent_subtle : (hov ? col::bg_card_hover : col::bg_app),
                              bh * 0.5f);
            dl->AddRect(ba, bb,
                        sel ? col::border_accent : col::border_default,
                        bh * 0.5f);
            const float lw = ImGui::CalcTextSize(labels[i]).x;
            dl->AddText(ImVec2((ba.x + bb.x - lw) * 0.5f,
                               (ba.y + bb.y - ImGui::GetTextLineHeight()) * 0.5f),
                        sel ? col::accent_strong : col::text_dim, labels[i]);
        }
        ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + bh + 4.0f));
        ImGui::Dummy(ImVec2(0, 4.0f));
    }
    section_end();

    // ── ACTIONS ──────────────────────────────────────────────────────
    section_begin("ACTIONS");
    {
        const float half = (inner - 28.0f - 8.0f) * 0.5f;
        if (pill_btn("##mfx_set_reload", "Reload all effects", half))
            rt_reload_all(rt);
        ImGui::SameLine(0, 8.0f);
        if (pill_btn("##mfx_set_reset_all", "Reset all uniforms", half))
        {
            rt->enumerate_uniform_variables(nullptr,
                [](api::effect_runtime *rtp, api::effect_uniform_variable v) {
                    rtp->reset_uniform_value(v);
                });
            g_last_change_time = ImGui::GetTime();
        }
        ImGui::Dummy(ImVec2(0, 4.0f));
    }
    section_end();

    // ── HOTKEYS (wired to runtime, with capture-on-click) ────────────
    section_begin("HOTKEYS");
    {
        // Hotkey data pointers for indexed access
        unsigned int *key_ptrs[3] = { g_key_overlay, g_key_screenshot, g_key_effects };
        const char   *key_labels[3] = { "Menu toggle", "Screenshot", "Effects toggle" };

        // Process key capture if active (modern ImGui key API)
        if (g_capturing_key >= 0) {
            if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
                g_capturing_key = -1; // cancel
            } else {
                // Scannable keys: ImGuiKey → VK mapping
                struct KeyMap { ImGuiKey ik; unsigned int vk; };
                static const KeyMap kmap[] = {
                    {ImGuiKey_Tab,VK_TAB},{ImGuiKey_LeftArrow,VK_LEFT},{ImGuiKey_RightArrow,VK_RIGHT},
                    {ImGuiKey_UpArrow,VK_UP},{ImGuiKey_DownArrow,VK_DOWN},
                    {ImGuiKey_PageUp,VK_PRIOR},{ImGuiKey_PageDown,VK_NEXT},
                    {ImGuiKey_Home,VK_HOME},{ImGuiKey_End,VK_END},
                    {ImGuiKey_Insert,VK_INSERT},{ImGuiKey_Delete,VK_DELETE},
                    {ImGuiKey_Backspace,VK_BACK},{ImGuiKey_Space,VK_SPACE},{ImGuiKey_Enter,VK_RETURN},
                    {ImGuiKey_PrintScreen,VK_SNAPSHOT},{ImGuiKey_Pause,VK_PAUSE},
                    {ImGuiKey_ScrollLock,VK_SCROLL},{ImGuiKey_NumLock,VK_NUMLOCK},{ImGuiKey_CapsLock,VK_CAPITAL},
                    {ImGuiKey_0,'0'},{ImGuiKey_1,'1'},{ImGuiKey_2,'2'},{ImGuiKey_3,'3'},{ImGuiKey_4,'4'},
                    {ImGuiKey_5,'5'},{ImGuiKey_6,'6'},{ImGuiKey_7,'7'},{ImGuiKey_8,'8'},{ImGuiKey_9,'9'},
                    {ImGuiKey_A,'A'},{ImGuiKey_B,'B'},{ImGuiKey_C,'C'},{ImGuiKey_D,'D'},{ImGuiKey_E,'E'},
                    {ImGuiKey_F,'F'},{ImGuiKey_G,'G'},{ImGuiKey_H,'H'},{ImGuiKey_I,'I'},{ImGuiKey_J,'J'},
                    {ImGuiKey_K,'K'},{ImGuiKey_L,'L'},{ImGuiKey_M,'M'},{ImGuiKey_N,'N'},{ImGuiKey_O,'O'},
                    {ImGuiKey_P,'P'},{ImGuiKey_Q,'Q'},{ImGuiKey_R,'R'},{ImGuiKey_S,'S'},{ImGuiKey_T,'T'},
                    {ImGuiKey_U,'U'},{ImGuiKey_V,'V'},{ImGuiKey_W,'W'},{ImGuiKey_X,'X'},{ImGuiKey_Y,'Y'},
                    {ImGuiKey_Z,'Z'},
                    {ImGuiKey_F1,VK_F1},{ImGuiKey_F2,VK_F2},{ImGuiKey_F3,VK_F3},{ImGuiKey_F4,VK_F4},
                    {ImGuiKey_F5,VK_F5},{ImGuiKey_F6,VK_F6},{ImGuiKey_F7,VK_F7},{ImGuiKey_F8,VK_F8},
                    {ImGuiKey_F9,VK_F9},{ImGuiKey_F10,VK_F10},{ImGuiKey_F11,VK_F11},{ImGuiKey_F12,VK_F12},
                    {ImGuiKey_Keypad0,VK_NUMPAD0},{ImGuiKey_Keypad1,VK_NUMPAD1},{ImGuiKey_Keypad2,VK_NUMPAD2},
                    {ImGuiKey_Keypad3,VK_NUMPAD3},{ImGuiKey_Keypad4,VK_NUMPAD4},{ImGuiKey_Keypad5,VK_NUMPAD5},
                    {ImGuiKey_Keypad6,VK_NUMPAD6},{ImGuiKey_Keypad7,VK_NUMPAD7},{ImGuiKey_Keypad8,VK_NUMPAD8},
                    {ImGuiKey_Keypad9,VK_NUMPAD9},
                };
                const ImGuiIO &io = ImGui::GetIO();
                for (const auto &km : kmap) {
                    if (ImGui::IsKeyPressed(km.ik, false)) {
                        unsigned int *dst = key_ptrs[g_capturing_key];
                        dst[0] = km.vk;
                        dst[1] = io.KeyCtrl  ? VK_CONTROL : 0;
                        dst[2] = io.KeyShift ? VK_SHIFT   : 0;
                        dst[3] = io.KeyAlt   ? VK_MENU    : 0;
                        g_capturing_key = -1;
                        settings_save(rt);
                        break;
                    }
                }
            }
        }

        const float btn_w = 120.0f, btn_h = 26.0f;
        for (int ki = 0; ki < 3; ++ki) {
            const ImVec2 p = ImGui::GetCursorScreenPos();
            dl->AddText(ImVec2(p.x, p.y + 4.0f), col::text_primary, key_labels[ki]);

            char display[64];
            const bool is_capturing = (g_capturing_key == ki);
            if (is_capturing)
                snprintf(display, sizeof(display), "Press a key...");
            else
                key_data_to_string(key_ptrs[ki], display, sizeof(display));

            const float btn_x = p.x + inner - btn_w - 28.0f;
            const ImVec2 ba(btn_x, p.y);
            const ImVec2 bb(btn_x + btn_w, p.y + btn_h);

            char id[48]; snprintf(id, sizeof(id), "##hk_%d", ki);
            ImGui::SetCursorScreenPos(ba);
            ImGui::InvisibleButton(id, ImVec2(btn_w, btn_h));
            const bool hov = ImGui::IsItemHovered();
            if (hov) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            if (ImGui::IsItemClicked())
                g_capturing_key = ki;

            const ImU32 bg_col = is_capturing ? col::accent_subtle :
                                 (hov ? col::bg_card_hover : col::bg_input);
            const ImU32 bd_col = is_capturing ? col::accent :
                                 (hov ? col::border_accent : col::border_default);
            dl->AddRectFilled(ba, bb, bg_col, 6.0f);
            dl->AddRect(ba, bb, bd_col, 6.0f);

            const float tw = ImGui::CalcTextSize(display).x;
            dl->AddText(ImVec2((ba.x + bb.x - tw) * 0.5f,
                               (ba.y + bb.y - ImGui::GetTextLineHeight()) * 0.5f),
                        is_capturing ? col::accent : col::text_primary, display);

            ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + btn_h + 6.0f));
        }
    }
    section_end();

    // ── SCREENSHOT (wired to runtime) ────────────────────────────────
    section_begin("SCREENSHOT");
    {
        const float field_w = inner - 28.0f;
        bool changed = false;

        ImGui::PushStyleColor(ImGuiCol_FrameBg, theme::to_vec4(col::bg_input));
        ImGui::PushStyleColor(ImGuiCol_Text, theme::to_vec4(col::text_primary));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10, 6));

        // Folder
        dl->AddText(ImGui::GetCursorScreenPos(), col::text_secondary, "Save folder");
        ImGui::Dummy(ImVec2(0, 18.0f));
        ImGui::SetNextItemWidth(field_w);
        if (ImGui::InputText("##ss_path", g_ss_path, sizeof(g_ss_path),
                             ImGuiInputTextFlags_EnterReturnsTrue))
            changed = true;
        if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
        ImGui::Dummy(ImVec2(0, 8.0f));

        // Filename pattern
        dl->AddText(ImGui::GetCursorScreenPos(), col::text_secondary, "Filename pattern");
        ImGui::Dummy(ImVec2(0, 18.0f));
        ImGui::SetNextItemWidth(field_w);
        if (ImGui::InputText("##ss_name", g_ss_name, sizeof(g_ss_name),
                             ImGuiInputTextFlags_EnterReturnsTrue))
            changed = true;
        if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
        ImGui::Dummy(ImVec2(0, 8.0f));

        // Format selector (BMP / PNG / JPEG)
        {
            dl->AddText(ImGui::GetCursorScreenPos(), col::text_secondary, "Format");
            ImGui::Dummy(ImVec2(0, 20.0f));
            const char *fmt_labels[3] = { "BMP", "PNG", "JPEG" };
            const float fbw = (field_w - 8.0f * 2.0f) / 3.0f;
            const ImVec2 base = ImGui::GetCursorScreenPos();
            for (int fi = 0; fi < 3; ++fi) {
                const bool sel = (g_ss_format == fi);
                const ImVec2 fa(base.x + fi * (fbw + 8.0f), base.y);
                const ImVec2 fb(fa.x + fbw, fa.y + 26.0f);
                ImGui::SetCursorScreenPos(fa);
                char fid[24]; snprintf(fid, sizeof(fid), "##ss_fmt_%d", fi);
                ImGui::InvisibleButton(fid, ImVec2(fbw, 26.0f));
                if (ImGui::IsItemHovered()) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                if (ImGui::IsItemClicked() && !sel) {
                    g_ss_format = fi;
                    changed = true;
                }
                dl->AddRectFilled(fa, fb,
                    sel ? col::accent_subtle : (ImGui::IsItemHovered() ? col::bg_card_hover : col::bg_app),
                    13.0f);
                dl->AddRect(fa, fb,
                    sel ? col::border_accent : col::border_default, 13.0f);
                const float tw = ImGui::CalcTextSize(fmt_labels[fi]).x;
                dl->AddText(ImVec2((fa.x + fb.x - tw) * 0.5f,
                                   (fa.y + fb.y - ImGui::GetTextLineHeight()) * 0.5f),
                            sel ? col::accent_strong : col::text_dim, fmt_labels[fi]);
            }
            ImGui::SetCursorScreenPos(ImVec2(base.x, base.y + 30.0f));
        }

        // Quality slider (only relevant for JPEG)
        if (g_ss_format == 2) {
            ImGui::Dummy(ImVec2(0, 4.0f));
            const ImVec2 p = ImGui::GetCursorScreenPos();
            dl->AddText(p, col::text_secondary, "JPEG quality");
            char pct[16]; snprintf(pct, sizeof(pct), "%d%%", g_ss_quality);
            const float pctw = ImGui::CalcTextSize(pct).x;
            dl->AddText(ImVec2(p.x + field_w - pctw, p.y), col::text_primary, pct);
            ImGui::Dummy(ImVec2(0, 20.0f));

            ImGui::PushStyleColor(ImGuiCol_SliderGrab, theme::to_vec4(col::accent));
            ImGui::SetNextItemWidth(field_w);
            if (ImGui::SliderInt("##ss_quality", &g_ss_quality, 1, 100, ""))
                changed = true;
            ImGui::PopStyleColor();
        }

        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor(2);

        if (changed) settings_save(rt);

        ImGui::Dummy(ImVec2(0, 4.0f));
    }
    section_end();

    // ── SHORTCUTS (reference only) ───────────────────────────────────
    section_begin("SHORTCUTS");
    {
        struct Sh { const char *k; const char *desc; };
        static const Sh shortcuts[] = {
            { "Ctrl+F", "Focus shader search" },
            { "Ctrl+R", "Reload effects"      },
            { "Space",  "Toggle selected"     },
            { "Tab",    "Cycle right panel"   },
            { "Esc",    "Clear / close"       },
        };
        for (int i = 0; i < (int)(sizeof(shortcuts) / sizeof(shortcuts[0])); ++i) {
            const ImVec2 p = ImGui::GetCursorScreenPos();
            dl->AddText(p, col::accent, shortcuts[i].k);
            dl->AddText(ImVec2(p.x + 80.0f, p.y), col::text_dim, shortcuts[i].desc);
            ImGui::Dummy(ImVec2(0, 18.0f));
        }
    }
    section_end();

    ImGui::Dummy(ImVec2(0, pad));
    ImGui::EndChild();
    ImGui::PopStyleVar();
}
