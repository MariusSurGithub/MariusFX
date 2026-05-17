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

    ImGui::EndChild();
    ImGui::PopStyleVar();
}

// ── Right-panel: SETTINGS ──────────────────────────────────────────────────
//
// MariusFX-only preferences. Every option here is owned by the pipeline
// editor — no embedded upstream tab. If the user ever needs the host's
// own configuration surface, the host's regular overlay hotkey still
// works alongside ours.
void pl_draw_settings_panel(mfx::runtime *rt, ImVec2 origin, float width, float height)
{
    using namespace theme;
    ImDrawList *dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(origin, ImVec2(origin.x + width, origin.y + height), col::bg_app);

    ImGui::SetCursorScreenPos(origin);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::BeginChild("##mfx_settings_col", ImVec2(width, height), false,
                      ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_AlwaysVerticalScrollbar);

    const float pad     = 18.0f;
    const float strip_h = 44.0f;

    // ── Compact title strip ─────────────────────────────────────────
    // Slimmer than the old big "header card with subtitle"  the panel's
    // role is already obvious from the toolbar icon that activated it.
    {
        const ImVec2 ca(origin.x + pad, origin.y + pad);
        const ImVec2 cb(origin.x + width - pad, ca.y + strip_h);
        dl->AddRectFilled(ImVec2(ca.x, ca.y + 8), ImVec2(ca.x + 3, cb.y - 8),
                          col::accent_strong, 1.5f);
        dl->AddText(ImVec2(ca.x + 16, ca.y + (strip_h - ImGui::GetTextLineHeight()) * 0.5f),
                    col::text_primary, "Settings");
    }

    // Helper for each section. Returns the y where content starts.
    auto section = [&](const char *title, float y, float h) -> ImVec2 {
        const ImVec2 sa(origin.x + pad, y);
        const ImVec2 sb(origin.x + width - pad, y + h);
        dl->AddRectFilled(sa, sb, col::bg_card, size::radius_card);
        dl->AddRect      (sa, sb, col::border_subtle, size::radius_card);
        dl->AddText(ImVec2(sa.x + 14.0f, sa.y + 10.0f), col::text_dimmest, title);
        return ImVec2(sa.x + 14.0f, sa.y + 36.0f);
    };

    const float body_top = origin.y + pad + strip_h + 12.0f;

    // ── Pipeline ────────────────────────────────────────────────────
    // Two switches that govern *what* renders, not *how* it looks.
    {
        const float y = body_top;
        const float h = 96.0f;
        const ImVec2 cs = section("PIPELINE", y, h);
        const bool fx_on = rt->get_effects_state();
        const bool perf  = rt_get_performance_mode(rt);

        ImGui::SetCursorScreenPos(ImVec2(cs.x, cs.y));
        if (toggle_switch("##mfx_set_fx", fx_on, 44.0f, 24.0f))
            rt->set_effects_state(!fx_on);
        dl->AddText(ImVec2(cs.x + 56.0f, cs.y + 4.0f),
                    col::text_primary, "Master enable");
        dl->AddText(ImVec2(cs.x + 56.0f, cs.y + 22.0f),
                    col::text_dimmest, "Turn every shader on or off in one click.");

        ImGui::SetCursorScreenPos(ImVec2(cs.x, cs.y + 44.0f));
        if (toggle_switch("##mfx_set_perf", perf, 44.0f, 24.0f))
            rt_set_performance_mode(rt, !perf);
        dl->AddText(ImVec2(cs.x + 56.0f, cs.y + 48.0f),
                    col::text_primary, "Performance mode");
        dl->AddText(ImVec2(cs.x + 56.0f, cs.y + 66.0f),
                    col::text_dimmest, "Locks parameters. Small GPU win.");
    }

    // ── Layout ──────────────────────────────────────────────────────
    // Pure cosmetic  where the panel docks.
    {
        const float y = body_top + 96.0f + 12.0f;
        const float h = 72.0f;
        const ImVec2 cs = section("LAYOUT", y, h);

        const char *labels[2] = { "Dock left", "Dock right" };
        const float bw = 120.0f, bh = 30.0f;
        for (int i = 0; i < 2; ++i) {
            const bool sel = (g_dock_side == DOCK_LEFT  && i == 0) ||
                             (g_dock_side == DOCK_RIGHT && i == 1);
            const ImVec2 ba(cs.x + i * (bw + 8.0f), cs.y);
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
            dl->AddRect      (ba, bb,
                              sel ? col::border_accent : col::border_default,
                              bh * 0.5f);
            const float lw = ImGui::CalcTextSize(labels[i]).x;
            dl->AddText(ImVec2((ba.x + bb.x - lw) * 0.5f,
                               (ba.y + bb.y - ImGui::GetTextLineHeight()) * 0.5f),
                        sel ? col::accent_strong : col::text_dim, labels[i]);
        }
    }

    // ── Actions ─────────────────────────────────────────────────────
    // The two destructive ops that don't fit elsewhere: rebuild from
    // disk, and zero every uniform back to its declared default.
    {
        const float y = body_top + 96.0f + 12.0f + 72.0f + 12.0f;
        const float h = 80.0f;
        const ImVec2 cs = section("ACTIONS", y, h);

        auto pill_btn = [&](const char *id, const char *label, float bx,
                            float by, float bw, ImU32 base_col) -> bool {
            const ImVec2 ba(bx, by);
            const ImVec2 bb(bx + bw, by + 30.0f);
            ImGui::SetCursorScreenPos(ba);
            ImGui::InvisibleButton(id, ImVec2(bw, 30.0f));
            const bool hov = ImGui::IsItemHovered();
            const bool clk = ImGui::IsItemClicked();
            if (hov) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            dl->AddRectFilled(ba, bb, hov ? col::bg_card_hover : base_col, 15.0f);
            dl->AddRect      (ba, bb, hov ? col::border_accent : col::border_default, 15.0f);
            const float lw = ImGui::CalcTextSize(label).x;
            dl->AddText(ImVec2((ba.x + bb.x - lw) * 0.5f,
                               (ba.y + bb.y - ImGui::GetTextLineHeight()) * 0.5f),
                        hov ? col::accent : col::text_primary, label);
            return clk;
        };

        if (pill_btn("##mfx_set_reload",
                     "Reload all effects (Ctrl+R)",
                     cs.x, cs.y, 220.0f, col::bg_app))
            rt_reload_all(rt);

        if (pill_btn("##mfx_set_reset_all",
                     "Reset every uniform",
                     cs.x + 230.0f, cs.y, 180.0f, col::bg_app))
        {
            // Walk every loaded effect; reset all of its uniforms.
            rt->enumerate_uniform_variables(nullptr,
                [](api::effect_runtime *rtp, api::effect_uniform_variable v) {
                    rtp->reset_uniform_value(v);
                });
            g_last_change_time = ImGui::GetTime();
        }
    }

    // ── Hotkeys configuration ───────────────────────────────────────
    // Let the user rebind the three main hotkeys: menu toggle, screenshot,
    // and shader toggle (when menu is open).
    {
        const float y = body_top + 96.0f + 12.0f + 72.0f + 12.0f + 80.0f + 12.0f;
        const float h = 140.0f;
        const ImVec2 cs = section("HOTKEYS", y, h);

        auto hotkey_row = [&](const char *label, int *vk_ptr, float row_y) {
            dl->AddText(ImVec2(cs.x, row_y), col::text_primary, label);
            
            // Display current key name
            char key_name[64] = "None";
            if (*vk_ptr != 0) {
                // Simple mapping for common keys (extend as needed)
                switch (*vk_ptr) {
                    case VK_HOME:   snprintf(key_name, sizeof(key_name), "Home"); break;
                    case VK_F11:    snprintf(key_name, sizeof(key_name), "F11"); break;
                    case VK_SPACE:  snprintf(key_name, sizeof(key_name), "Space"); break;
                    case VK_F1:     snprintf(key_name, sizeof(key_name), "F1"); break;
                    case VK_F2:     snprintf(key_name, sizeof(key_name), "F2"); break;
                    case VK_F3:     snprintf(key_name, sizeof(key_name), "F3"); break;
                    case VK_F4:     snprintf(key_name, sizeof(key_name), "F4"); break;
                    case VK_F5:     snprintf(key_name, sizeof(key_name), "F5"); break;
                    case VK_F6:     snprintf(key_name, sizeof(key_name), "F6"); break;
                    case VK_F7:     snprintf(key_name, sizeof(key_name), "F7"); break;
                    case VK_F8:     snprintf(key_name, sizeof(key_name), "F8"); break;
                    case VK_F9:     snprintf(key_name, sizeof(key_name), "F9"); break;
                    case VK_F10:    snprintf(key_name, sizeof(key_name), "F10"); break;
                    case VK_F12:    snprintf(key_name, sizeof(key_name), "F12"); break;
                    case VK_INSERT: snprintf(key_name, sizeof(key_name), "Insert"); break;
                    case VK_DELETE: snprintf(key_name, sizeof(key_name), "Delete"); break;
                    case VK_END:    snprintf(key_name, sizeof(key_name), "End"); break;
                    default:
                        if (*vk_ptr >= 'A' && *vk_ptr <= 'Z')
                            snprintf(key_name, sizeof(key_name), "%c", (char)*vk_ptr);
                        else
                            snprintf(key_name, sizeof(key_name), "VK_%d", *vk_ptr);
                        break;
                }
            }
            
            const float btn_x = cs.x + 200.0f;
            const float btn_w = 120.0f;
            const ImVec2 ba(btn_x, row_y - 4.0f);
            const ImVec2 bb(btn_x + btn_w, row_y + 20.0f);
            
            char id[32];
            snprintf(id, sizeof(id), "##hotkey_%s", label);
            ImGui::SetCursorScreenPos(ba);
            ImGui::InvisibleButton(id, ImVec2(btn_w, 24.0f));
            const bool hov = ImGui::IsItemHovered();
            if (hov) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            
            dl->AddRectFilled(ba, bb, hov ? col::bg_card_hover : col::bg_input, 4.0f);
            dl->AddRect(ba, bb, col::border_default, 4.0f);
            
            const float tw = ImGui::CalcTextSize(key_name).x;
            dl->AddText(ImVec2((ba.x + bb.x - tw) * 0.5f, row_y),
                        col::text_primary, key_name);
            
            // TODO: Implement key capture on click (for now just display)
        };

        hotkey_row("Menu toggle", &g_hotkey_menu_toggle, cs.y);
        hotkey_row("Screenshot", &g_hotkey_screenshot, cs.y + 32.0f);
        hotkey_row("Shader toggle", &g_hotkey_shader_toggle, cs.y + 64.0f);
        
        dl->AddText(ImVec2(cs.x, cs.y + 100.0f), col::text_dimmest,
                    "Click a button to rebind (press Esc to cancel).");
    }

    // ── Screenshot settings ─────────────────────────────────────────
    {
        const float y = body_top + 96.0f + 12.0f + 72.0f + 12.0f + 80.0f + 12.0f + 140.0f + 12.0f;
        const float h = 160.0f;
        const ImVec2 cs = section("SCREENSHOT", y, h);

        dl->AddText(ImVec2(cs.x, cs.y), col::text_primary, "Folder");
        ImGui::SetCursorScreenPos(ImVec2(cs.x, cs.y + 20.0f));
        ImGui::SetNextItemWidth(width - pad * 2 - 28.0f);
        ImGui::PushStyleColor(ImGuiCol_FrameBg, theme::to_vec4(col::bg_input));
        ImGui::PushStyleColor(ImGuiCol_Text, theme::to_vec4(col::text_primary));
        ImGui::InputText("##ss_path", g_screenshot_path, sizeof(g_screenshot_path));
        ImGui::PopStyleColor(2);

        dl->AddText(ImVec2(cs.x, cs.y + 52.0f), col::text_primary, "Filename pattern");
        ImGui::SetCursorScreenPos(ImVec2(cs.x, cs.y + 72.0f));
        ImGui::SetNextItemWidth(width - pad * 2 - 28.0f);
        ImGui::PushStyleColor(ImGuiCol_FrameBg, theme::to_vec4(col::bg_input));
        ImGui::PushStyleColor(ImGuiCol_Text, theme::to_vec4(col::text_primary));
        ImGui::InputText("##ss_filename", g_screenshot_filename, sizeof(g_screenshot_filename));
        ImGui::PopStyleColor(2);

        dl->AddText(ImVec2(cs.x, cs.y + 104.0f), col::text_primary, "JPEG quality");
        ImGui::SetCursorScreenPos(ImVec2(cs.x + 120.0f, cs.y + 100.0f));
        ImGui::SetNextItemWidth(200.0f);
        ImGui::PushStyleColor(ImGuiCol_FrameBg, theme::to_vec4(col::bg_input));
        ImGui::PushStyleColor(ImGuiCol_SliderGrab, theme::to_vec4(col::accent));
        ImGui::SliderInt("##ss_quality", &g_screenshot_quality, 1, 100, "%d%%");
        ImGui::PopStyleColor(2);
        
        dl->AddText(ImVec2(cs.x, cs.y + 132.0f), col::text_dimmest,
                    "Use %Y%m%d_%H%M%S for date/time in filename.");
    }

    // ── Shortcuts cheat-sheet ───────────────────────────────────────
    {
        const float y = body_top + 96.0f + 12.0f + 72.0f + 12.0f + 80.0f + 12.0f + 140.0f + 12.0f + 160.0f + 12.0f;
        const float h = 124.0f;
        const ImVec2 cs = section("KEYBOARD SHORTCUTS", y, h);

        struct Sh { const char *k; const char *desc; };
        static const Sh shortcuts[] = {
            { "Ctrl+F",  "Focus shader search" },
            { "↑ / ↓",   "Navigate shaders"    },
            { "Space",   "Toggle selected"     },
            { "Tab",     "Cycle right panel"   },
            { "Esc",     "Clear / close"       },
            { "Ctrl+R",  "Reload effects"      },
        };
        const float sh_y0 = cs.y;
        const float line_h = 16.0f;
        for (int i = 0; i < (int)(sizeof(shortcuts) / sizeof(shortcuts[0])); ++i) {
            const int col_idx = i % 2;
            const int row_    = i / 2;
            const float x  = cs.x + col_idx * (width * 0.5f - pad);
            const float y2 = sh_y0 + row_ * line_h;
            dl->AddText(ImVec2(x,           y2), col::accent,        shortcuts[i].k);
            dl->AddText(ImVec2(x + 70.0f,   y2), col::text_dim,      shortcuts[i].desc);
        }
    }

    ImGui::Dummy(ImVec2(0, body_top - origin.y + 96.0f + 12.0f + 72.0f + 12.0f + 80.0f + 12.0f + 140.0f + 12.0f + 160.0f + 12.0f + 124.0f + pad));
    ImGui::EndChild();
    ImGui::PopStyleVar();
}
