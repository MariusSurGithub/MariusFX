// ============================================================================ // ui_pipeline.inl - included by ui.cpp inside namespace mariusfx::ui::{anonymous}. // This is not a stand-alone translation unit. It exists only as a logical // module to keep ui.cpp browsable. Do not compile or include directly. // ============================================================================ 
bool pl_draw_tech_row(mfx::runtime *rt, int idx, float width,
                      bool selected, bool allow_drag)
{
    using namespace theme;
    ImDrawList *dl = ImGui::GetWindowDrawList();
    TechRow &t = g_techs[idx];

    const float row_h = 52.0f;
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const ImVec2 a   = pos;
    const ImVec2 b(pos.x + width, pos.y + row_h);

    // Toggle geometry computed first so the row's invisible button can
    // exclude it cleanly (no overlap, no priority weirdness).
    const float sw_w = 44.0f, sw_h = 24.0f;
    const float sw_x = b.x - 16.0f - sw_w;
    const float sw_y = (a.y + b.y) * 0.5f - sw_h * 0.5f;
    const float row_clickable_w = sw_x - a.x - 8.0f;

    // ── Drag-handle hit area (left 22 px) ──────────────────────────
    // Active rows expose a dedicated grab zone for the ⠿ glyph. Pressing
    // anywhere in this zone starts the custom drag IMMEDIATELY (no
    // movement threshold) and assigns g_drag_src to this row. This is
    // separate from the row body button below, so clicking the body
    // still selects without ever risking accidental drag.
    const float dh_w = allow_drag ? 22.0f : 0.0f;
    if (allow_drag)
    {
        ImGui::SetCursorScreenPos(ImVec2(a.x, a.y));
        char hid[64]; snprintf(hid, sizeof(hid), "##mfx_dh_%d", idx);
        ImGui::InvisibleButton(hid, ImVec2(dh_w, row_h));
        const bool dh_hov = ImGui::IsItemHovered();
        if (dh_hov || g_drag_src == idx)
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        if (ImGui::IsItemActivated()) {
            g_drag_src     = idx;
            g_drag_grab_dy = ImGui::GetIO().MousePos.y - a.y;
        }
    }

    // ── Row body hit area (rest of the row, minus the drag handle and
    //    the toggle column) ────────────────────────────────────────────
    const float body_x = a.x + dh_w;
    const float body_w = row_clickable_w - dh_w;
    char rid[200]; snprintf(rid, sizeof(rid), "##mfx_row_%d_%s_%s", idx, t.effect_short, t.name);
    ImGui::SetCursorScreenPos(ImVec2(body_x, a.y));
    ImGui::InvisibleButton(rid, ImVec2(body_w, row_h));
    const bool hov = ImGui::IsItemHovered();
    const bool clk = ImGui::IsItemClicked(ImGuiMouseButton_Left);
    if (hov && g_drag_src < 0) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

    // Track whether *any* row is currently being dragged + whether THIS
    // row is the source. Used below for hover suppression and source dim.
    const bool drag_active     = (g_drag_src >= 0);
    const bool is_drag_source  = (g_drag_src == idx);

    // ── Right-click context menu ────────────────────────────────────────
    // ImGui::BeginPopupContextItem() picks up the row's invisible button
    // and tracks open/close state automatically. We just stash a deferred
    // CTX_ action so pl_draw_pipeline can apply it after the loop.
    char popup_id[32]; snprintf(popup_id, sizeof(popup_id), "##mfx_ctx_%d", idx);
    if (ImGui::BeginPopupContextItem(popup_id))
    {
        ImGui::TextDisabled("%s", t.name);
        ImGui::Separator();
        if (ImGui::MenuItem(t.enabled ? "Disable" : "Enable", "Space")) {
            rt->set_technique_state(t.handle, !t.enabled);
        }
        if (ImGui::MenuItem("Move to top")) {
            g_ctx_action_idx = idx; g_ctx_action = CTX_MOVE_TOP;
        }
        if (ImGui::MenuItem("Move to bottom")) {
            g_ctx_action_idx = idx; g_ctx_action = CTX_MOVE_BOTTOM;
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Reset uniforms")) {
            g_ctx_action_idx = idx; g_ctx_action = CTX_RESET_UNIFORMS;
        }
        if (ImGui::MenuItem("Open .fx in explorer")) {
            g_ctx_action_idx = idx; g_ctx_action = CTX_OPEN_FX;
        }
        if (ImGui::MenuItem("Copy name to clipboard")) {
            g_ctx_action_idx = idx; g_ctx_action = CTX_COPY_NAME;
        }
        ImGui::EndPopup();
    }

    // Background: selection wins over hover; hover is suppressed while a
    // drag is in progress so the insertion line is the only visual focus.
    if (selected)
        dl->AddRectFilled(a, b, col::accent_subtle, 4.0f);
    else if (hov && !drag_active)
        dl->AddRectFilled(a, b, col::bg_card_hover, 4.0f);
    if (selected)
        dl->AddRectFilled(ImVec2(a.x, a.y + 6.0f),
                          ImVec2(a.x + 3.0f, b.y - 6.0f),
                          col::accent, 2.0f);
    // Source row gets a dashed dim overlay so the user sees "this is the
    // one I'm dragging"  the preview tooltip travels with the cursor,
    // but the row in place still needs a visual cue.
    if (is_drag_source)
        dl->AddRectFilled(a, b, IM_COL32(0, 0, 0, 0x70), 4.0f);

    float ix = a.x + 14.0f;
    const float cy = (a.y + b.y) * 0.5f;

    // Drag handle  shown ONLY when this row is in the Active section
    // (allow_drag) AND the technique is enabled. Both conditions matter:
    // we want a perfectly aligned single column even for active-but-
    // disabled-momentarily rows.
    if (allow_drag) {
        glyph_drag_handle(dl, ImVec2(ix + 4.0f, cy),
                          hov ? col::text_dim : col::text_dimmest);
        ix += 22.0f;
    } else {
        ix += 4.0f;
    }

    // Perf badge first so we know how much horizontal room the name has.
    char perf_buf[32] = "";
    float perf_w = 0.0f;
    if (t.enabled) {
        uint64_t cpu_ns = 0, gpu_ns = 0;
        rt_get_technique_timing(rt, t.handle, &cpu_ns, &gpu_ns);
        const float total_ms = (cpu_ns + gpu_ns) / 1'000'000.0f;
        snprintf(perf_buf, sizeof(perf_buf), "%.2f ms", total_ms);
        perf_w = ImGui::CalcTextSize(perf_buf).x;
    }

    // Name (primary) + effect file (small, dim)  clipped so a long name
    // (e.g. "MartysMods_AntiAliasing") never crashes into the perf badge
    // or the toggle. The clip rect ends 8px before the badge starts.
    const float name_clip_right =
        (perf_w > 0.0f) ? (sw_x - 12.0f - perf_w - 8.0f)
                        : (sw_x - 8.0f);
    // Show the effect file as a subtitle ONLY when it's different from
    // the technique name  otherwise we'd render "QuantV / QuantV"
    // which is just visual noise.
    const bool show_subtitle = std::strcmp(t.name, t.effect_short) != 0;

    // Match-highlight: when the user has typed a search query, the
    // matched substring is rendered in accent over the regular text colour.
    const ImU32 base_col = t.enabled ? col::text_primary : col::text_dim;
    const char *needle   = g_toolbar_search[0] ? g_toolbar_search : nullptr;

    dl->PushClipRect(ImVec2(ix, a.y), ImVec2(name_clip_right, b.y), true);
    if (show_subtitle) {
        draw_text_with_match(dl, ImVec2(ix, cy - 14.0f), t.name,
                             needle, base_col, col::accent);
        draw_text_with_match(dl, ImVec2(ix, cy + 2.0f), t.effect_short,
                             needle, col::text_dimmest, col::accent);
    } else {
        draw_text_with_match(dl, ImVec2(ix, cy - ImGui::GetTextLineHeight() * 0.5f),
                             t.name, needle, base_col, col::accent);
    }
    dl->PopClipRect();

    // Tooltip with the full name when truncated by the clip rect.
    if (hov) {
        const float name_w = ImGui::CalcTextSize(t.name).x;
        if (ix + name_w > name_clip_right) {
            if (show_subtitle) ImGui::SetTooltip("%s\n%s", t.name, t.effect_short);
            else               ImGui::SetTooltip("%s", t.name);
        }
    }

    // Perf badge between the name and the toggle (only when enabled).
    if (perf_buf[0]) {
        const float total_ms = (float)atof(perf_buf);
        const ImU32 bc = (total_ms > 2.0f) ? col::stat_lat : col::stat_fps;
        dl->AddText(ImVec2(sw_x - 12.0f - perf_w, cy - ImGui::GetTextLineHeight() * 0.5f),
                    bc, perf_buf);
    }

    // Toggle switch on the right.
    ImGui::SetCursorScreenPos(ImVec2(sw_x, sw_y));
    char tid[200]; snprintf(tid, sizeof(tid), "##mfx_tog_%d_%s_%s", idx, t.effect_short, t.name);
    if (toggle_switch(tid, t.enabled, sw_w, sw_h)) {
        rt->set_technique_state(t.handle, !t.enabled);
        // When the user enables a shader from a disabled row, auto-select
        // it so its parameters appear immediately on the right.
        if (!t.enabled)
            make_key(g_selected_key, sizeof(g_selected_key), t.effect_full, t.name);
    }

    // Hairline separator at the bottom of the row.
    dl->AddLine(ImVec2(a.x + 8.0f, b.y), ImVec2(b.x - 8.0f, b.y),
                col::border_subtle, 1.0f);

    // ── CRITICAL: force the ImGui cursor back to (pos.x, pos.y + row_h)
    //    before returning. Inside the row we used SetCursorScreenPos to
    //    place the toggle at sw_y = pos.y + 14; toggle_switch() calls
    //    InvisibleButton(sw_h = 24), which leaves the cursor at
    //    (line_start, sw_y + sw_h) = (line_start, pos.y + 38)  14 px
    //    SHORT of the row's actual bottom. Without this reset, every
    //    subsequent row would render 14 px too high and visually
    //    overlap the previous one. This is the fix for the screenshot
    //    where QuantV / Per_Weather_LUT looked smashed together.
    ImGui::SetCursorScreenPos(ImVec2(pos.x, pos.y + row_h));

    return clk;
}

// ── Shader list column (left side)  unified, searchable ───────────────────
void pl_draw_pipeline(mfx::runtime *rt, ImVec2 origin, float width, float height)
{
    using namespace theme;
    ImDrawList *dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(origin, ImVec2(origin.x + width, origin.y + height), col::bg_app);
    dl->AddLine(ImVec2(origin.x + width - 1.0f, origin.y),
                ImVec2(origin.x + width - 1.0f, origin.y + height),
                col::border_subtle);

    // Search predicate  case-insensitive on either name or effect file.
    // The filter-chip predicate (Active / Inactive) is applied as a
    // visibility filter inside the unified row loop further below.
    auto match_search = [&](const TechRow &t) {
        if (!g_toolbar_search[0]) return true;
        return icase_contains(t.name,         g_toolbar_search) ||
               icase_contains(t.effect_short, g_toolbar_search);
    };

    int n_enabled = 0, n_disabled = 0;
    for (const auto &t : g_techs) {
        if (t.hidden) continue;
        if (t.enabled) ++n_enabled; else ++n_disabled;
    }

    ImGui::SetCursorScreenPos(origin);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::BeginChild("##mfx_pl_col", ImVec2(width, height), false,
                      ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_AlwaysVerticalScrollbar);

    // ── Search bar (sticky, top of column) ──────────────────────────
    ImGui::Dummy(ImVec2(0, 12));
    {
        const float side_pad = 12.0f;
        const float box_h = 34.0f;
        const float box_w = width - 2 * side_pad;
        const ImVec2 sa(origin.x + side_pad, ImGui::GetCursorScreenPos().y);
        const ImVec2 sb(sa.x + box_w, sa.y + box_h);
        const float scy = (sa.y + sb.y) * 0.5f;

        dl->AddRectFilled(sa, sb, col::bg_card, 8.0f);
        dl->AddRect      (sa, sb, col::border_subtle, 8.0f);
        glyph_search(dl, ImVec2(sa.x + 14.0f, scy), col::text_dim);

        ImGui::SetCursorScreenPos(ImVec2(sa.x + 28.0f, sa.y + 7.0f));
        ImGui::PushStyleColor(ImGuiCol_FrameBg,        ImVec4(0,0,0,0));
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0,0,0,0));
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive,  ImVec4(0,0,0,0));
        ImGui::PushStyleColor(ImGuiCol_Text,           to_vec4(col::text_primary));
        ImGui::PushItemWidth(box_w - 38.0f - 24.0f);
        if (g_focus_search_next) {
            ImGui::SetKeyboardFocusHere();
            g_focus_search_next = false;
        }
        ImGui::InputTextWithHint("##mfx_pl_search", "Search shaders   Ctrl+F",
                                 g_toolbar_search, IM_ARRAYSIZE(g_toolbar_search));
        ImGui::PopItemWidth();
        ImGui::PopStyleColor(4);

        if (g_toolbar_search[0]) {
            const float xs = 18.0f;
            const ImVec2 xa(sb.x - xs - 6.0f, scy - xs * 0.5f);
            const ImVec2 xb(xa.x + xs,        xa.y + xs);
            ImGui::SetCursorScreenPos(xa);
            ImGui::InvisibleButton("##mfx_pl_search_clear", ImVec2(xs, xs));
            const bool xhov = ImGui::IsItemHovered();
            if (xhov) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            if (ImGui::IsItemClicked()) g_toolbar_search[0] = '\0';
            dl->AddCircleFilled(ImVec2((xa.x + xb.x) * 0.5f, (xa.y + xb.y) * 0.5f),
                                xs * 0.5f, xhov ? col::bg_card_hover : col::bg_app, 12);
            glyph_x(dl, ImVec2((xa.x + xb.x) * 0.5f, (xa.y + xb.y) * 0.5f),
                    4.0f, xhov ? col::accent : col::text_dim);
        }
        ImGui::Dummy(ImVec2(0, box_h + 10.0f));
    }

    // ── Filter chips (All / Active / Inactive) ──────────────────────
    {
        const float side_pad = 12.0f;
        const float chip_h = 26.0f;
        const float chip_y = ImGui::GetCursorScreenPos().y;
        const float strip_w = width - 2 * side_pad;
        // Three equal segments, joined visually as a single pill strip.
        const float each = (strip_w - 2 * 4.0f) / 3.0f;
        struct Chip { Pill v; const char *label; int count; };
        const Chip chips[3] = {
            { PILL_ALL,      "All",      n_enabled + n_disabled },
            { PILL_ACTIVE,   "Active",   n_enabled              },
            { PILL_INACTIVE, "Inactive", n_disabled             },
        };
        for (int i = 0; i < 3; ++i) {
            const Chip &c = chips[i];
            const ImVec2 ca(origin.x + side_pad + i * (each + 4.0f), chip_y);
            const ImVec2 cb(ca.x + each, ca.y + chip_h);
            ImGui::SetCursorScreenPos(ca);
            char id_[32]; snprintf(id_, sizeof(id_), "##mfx_chip_%d", i);
            ImGui::InvisibleButton(id_, ImVec2(each, chip_h));
            const bool chov = ImGui::IsItemHovered();
            const bool cact = (g_active_pill == c.v);
            if (chov) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            if (ImGui::IsItemClicked()) g_active_pill = c.v;

            dl->AddRectFilled(ca, cb,
                              cact ? col::accent_subtle
                                   : (chov ? col::bg_card_hover : col::bg_card),
                              6.0f);
            dl->AddRect      (ca, cb,
                              cact ? col::border_accent : col::border_subtle,
                              6.0f);

            char lbl[64]; snprintf(lbl, sizeof(lbl), "%s  %d", c.label, c.count);
            const float lw = ImGui::CalcTextSize(lbl).x;
            dl->AddText(ImVec2((ca.x + cb.x - lw) * 0.5f,
                               (ca.y + cb.y - ImGui::GetTextLineHeight()) * 0.5f),
                        cact ? col::accent_strong : col::text_dim, lbl);
        }
        ImGui::Dummy(ImVec2(0, chip_h + 8.0f));
    }

    // ── "Group active on top" action button ─────────────────────────
    // Single-shot explicit reorder: brings every currently-enabled
    // technique to the head of the pipeline, preserving relative order
    // within both groups. Disabled (greyed out, non-clickable) when no
    // shader is enabled OR when the pipeline is already grouped.
    {
        const float side_pad = 12.0f;
        const float btn_h    = 28.0f;
        const float btn_y    = ImGui::GetCursorScreenPos().y;
        const float btn_w    = width - 2 * side_pad;
        const ImVec2 ba(origin.x + side_pad, btn_y);
        const ImVec2 bb(ba.x + btn_w, ba.y + btn_h);

        // Detect "already grouped": every enabled tech comes before
        // every disabled one in g_techs. If so, the button is a no-op
        // and we render it as disabled so the user knows.
        bool already_grouped = true;
        {
            bool seen_disabled = false;
            for (const auto &t : g_techs) {
                if (t.hidden) continue;
                if (!t.enabled) { seen_disabled = true; }
                else if (seen_disabled) { already_grouped = false; break; }
            }
        }
        const bool can_group = (n_enabled > 0) && !already_grouped;

        ImGui::SetCursorScreenPos(ba);
        ImGui::InvisibleButton("##mfx_pl_group_active", ImVec2(btn_w, btn_h));
        const bool ghov = can_group && ImGui::IsItemHovered();
        const bool gclk = can_group && ImGui::IsItemClicked();
        if (ghov) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        if (gclk) promote_all_enabled(rt);

        const ImU32 bg_col  = !can_group ? col::bg_card
                            :  ghov      ? col::accent_subtle
                                         : col::bg_card;
        const ImU32 brd_col = !can_group ? col::border_subtle
                            :  ghov      ? col::border_accent
                                         : col::border_default;
        const ImU32 txt_col = !can_group ? col::text_dimmer
                            :  ghov      ? col::accent_strong
                                         : col::text_secondary;
        dl->AddRectFilled(ba, bb, bg_col, 6.0f);
        dl->AddRect      (ba, bb, brd_col, 6.0f);

        // Up-arrow glyph + label, centred together.
        const char *lbl = already_grouped ? "Active shaders are at the top"
                                          : "Group active shaders on top";
        const float lh   = ImGui::GetTextLineHeight();
        const float lw   = ImGui::CalcTextSize(lbl).x;
        const float gly_w = 10.0f;
        const float total_w = gly_w + 8.0f + lw;
        const float lx = (ba.x + bb.x - total_w) * 0.5f;
        const float ly = (ba.y + bb.y - lh) * 0.5f;
        // Compact up-chevron glyph: two short strokes forming "^".
        const float gcx = lx + gly_w * 0.5f;
        const float gcy = ly + lh * 0.5f;
        dl->AddLine(ImVec2(gcx - 5.0f, gcy + 3.0f),
                    ImVec2(gcx,        gcy - 3.0f), txt_col, 1.6f);
        dl->AddLine(ImVec2(gcx,        gcy - 3.0f),
                    ImVec2(gcx + 5.0f, gcy + 3.0f), txt_col, 1.6f);
        dl->AddText(ImVec2(lx + gly_w + 8.0f, ly), txt_col, lbl);

        ImGui::Dummy(ImVec2(0, btn_h + 12.0f));
    }

    // ── Unified pipeline list ────────────────────────────────────────
    // Single ordered list in PIPELINE ORDER  disabled techniques stay
    // exactly where they were when toggled off so the user can flip a
    // shader on/off in place for trivial before/after comparison without
    // hunting for it in an alphabetical "Available" bucket.
    //
    // Filter chips (All / Active / Inactive) and the search box act as
    // visibility predicates  they never reorder. This keeps the drag-
    // drop reorder semantically correct: dropping below "Bloom" always
    // means "after Bloom in the pipeline", regardless of which chip the
    // user has selected.
    //
    // During an active drag we additionally:
    //   1. SKIP the source row entirely so its slot collapses.
    //   2. RENDER a highlighted "drop slot" before whichever row was the
    //      previous frame's target. The slot is row-tall, accent-tinted
    //      and stamped with chevrons + "Drop here" so the user can read
    //      the final layout without releasing.
    //   3. While the loop runs we recompute frame_target live based on
    //      the cursor's y position vs the visible rows. That value is
    //      what the post-loop commit uses; g_drag_target_idx merely
    //      stores it for the NEXT frame's visual displacement (the one-
    //      frame visual lag is imperceptible at 60 fps).
    const int sel_idx     = find_selected_index();
    bool      any_visible = false;
    const int prev_target = g_drag_target_idx;
    int       frame_target = -1;
    const float my        = ImGui::GetIO().MousePos.y;

    auto draw_drop_slot = [&](int idx_for_label) {
        // 52 px-tall accent slot with a centred chevron + "Drop here"
        // call-out. Rendered IN-FLOW so the rows below shift down by
        // exactly row_h, giving the user a real preview of the post-
        // commit layout rather than an abstract line.
        const ImVec2 sp = ImGui::GetCursorScreenPos();
        const float slot_h = 52.0f;
        const ImVec2 sa(sp.x + 10.0f, sp.y + 4.0f);
        const ImVec2 sb(sp.x + width - 10.0f, sp.y + slot_h - 4.0f);
        dl->AddRectFilled(sa, sb, col::accent_subtle, 8.0f);
        dl->AddRect      (sa, sb, col::border_accent, 8.0f);
        const float scy = (sa.y + sb.y) * 0.5f;
        // Label: where in the pipeline the row will land.
        char tag[32];
        if (idx_for_label < 0)
            snprintf(tag, sizeof(tag), "Drop here  end of pipeline");
        else
            snprintf(tag, sizeof(tag), "Drop here  slot #%d", idx_for_label + 1);
        const float lw = ImGui::CalcTextSize(tag).x;
        dl->AddText(ImVec2((sa.x + sb.x - lw) * 0.5f,
                           scy - ImGui::GetTextLineHeight() * 0.5f),
                    col::accent_strong, tag);
        ImGui::Dummy(ImVec2(0, slot_h));
    };

    for (int i = 0; i < (int)g_techs.size(); ++i)
    {
        const TechRow &t = g_techs[i];
        if (t.hidden) continue;
        if (!match_search(t)) continue;
        // Filter chips act as visibility only  they never reorder.
        if (g_active_pill == PILL_ACTIVE   && !t.enabled) continue;
        if (g_active_pill == PILL_INACTIVE &&  t.enabled) continue;

        // Insert the drop slot BEFORE this row if it was last frame's
        // target. Suppressed when the slot would land immediately above
        // the source  that's a no-op and the user shouldn't see a
        // misleading "drop here" affordance.
        const bool show_slot_here =
            g_drag_src >= 0 &&
            i == prev_target &&
            i != g_drag_src &&
            i != g_drag_src + 1;
        if (show_slot_here)
            draw_drop_slot(i);

        // Source row stays out of the visible list entirely while in
        // flight  its slot collapses, its content is shown by the
        // floating ghost in the post-loop overlay.
        if (g_drag_src == i) continue;

        // Live target computation for THIS frame: the first row whose
        // upper-mid is below the cursor wins. Since rows render in
        // visual order, the first match is the correct insertion point.
        if (g_drag_src >= 0 && frame_target == -1) {
            const float row_top = ImGui::GetCursorScreenPos().y;
            if (my < row_top + 26.0f /* row_h * 0.5 */)
                frame_target = i;
        }

        any_visible = true;
        if (pl_draw_tech_row(rt, i, width, i == sel_idx, /*allow_drag*/ true))
            make_key(g_selected_key, sizeof(g_selected_key),
                     t.effect_full, t.name);
    }

    // Cursor is below every visible row → target is the past-end slot.
    if (g_drag_src >= 0 && frame_target == -1)
        frame_target = (int)g_techs.size();
    // End-of-list drop slot (the cursor is dragged past the last row).
    if (g_drag_src >= 0 &&
        prev_target == (int)g_techs.size() &&
        prev_target != g_drag_src + 1)
        draw_drop_slot(-1);
    // Persist for the next frame's visual displacement.
    if (g_drag_src >= 0)
        g_drag_target_idx = frame_target;
    else
        g_drag_target_idx = -1;

    if (!any_visible) {
        ImGui::Dummy(ImVec2(0, 28));
        const char *l1 = g_toolbar_search[0]
                         ? "No shaders match the search."
                         : (g_active_pill == PILL_ACTIVE
                            ? "No active shaders."
                            : g_active_pill == PILL_INACTIVE
                              ? "All shaders are active."
                              : "No shaders available.");
        const char *l2 = g_toolbar_search[0]
                         ? "Try a different keyword or clear the filter."
                         : "Use the chips above to switch the view.";
        for (int k = 0; k < 2; ++k) {
            const char *t   = k == 0 ? l1 : l2;
            const ImU32  c2 = k == 0 ? col::text_muted : col::text_dimmer;
            const ImVec2 cp = ImGui::GetCursorScreenPos();
            const float  lw = ImGui::CalcTextSize(t).x;
            dl->AddText(ImVec2(origin.x + (width - lw) * 0.5f, cp.y), c2, t);
            ImGui::Dummy(ImVec2(0, ImGui::GetTextLineHeight() + 4.0f));
        }
    }

    ImGui::Dummy(ImVec2(0, 28));
    ImGui::EndChild();
    ImGui::PopStyleVar();

    // ── Custom drag overlay + commit ─────────────────────────────────
    // Runs AFTER all rows have been drawn so we have a complete
    // g_drag_rows snapshot. We:
    //   1. Cancel the drag if Esc is pressed or g_drag_src is now stale.
    //   2. Compute the target insertion slot based on the cursor's y
    //      relative to row midpoints.
    //   3. Render the floating ghost card at the cursor position.
    //   4. Render the 2 px insertion line + dot caps at the target slot.
    //   5. On mouse-up, commit the reorder via reorder_techniques().
    if (g_drag_src >= 0)
    {
        // Defensive: source row may have disappeared (effect reload, etc.)
        if (g_drag_src >= (int)g_techs.size() || g_techs[g_drag_src].hidden) {
            g_drag_src = -1;
        }
        // Cancel on Esc.
        else if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            g_drag_src = -1;
        }
        else {
            const ImGuiIO &io = ImGui::GetIO();
            const float my = io.MousePos.y;

            // The target was already computed during the row loop and
            // stashed in g_drag_target_idx. We just need a handle on it
            // here for the no-op check + the final commit.
            int target_idx = g_drag_target_idx;
            if (target_idx < 0) target_idx = g_drag_src + 1; // safety: stay put
            const bool no_op = (target_idx == g_drag_src ||
                                target_idx == g_drag_src + 1);

            // Floating ghost card  a slim chip with the drag-handle
            // glyph + the technique name. Anchored so the cursor sits
            // at the same vertical offset within the chip as it did on
            // the source row when the drag began. The drop-slot
            // rendered IN-FLOW above already provides the "where will
            // it land" feedback; the ghost answers "what am I moving".
            const TechRow &src = g_techs[g_drag_src];
            const float ghost_h = 36.0f;
            const float ghost_w = std::min(260.0f,
                                           ImGui::CalcTextSize(src.name).x + 56.0f);
            const ImVec2 ga(io.MousePos.x + 14.0f,
                            my - g_drag_grab_dy +
                            (52.0f - ghost_h) * 0.5f);
            const ImVec2 gb(ga.x + ghost_w, ga.y + ghost_h);
            dl->AddRectFilled(ImVec2(ga.x + 2, ga.y + 4),
                              ImVec2(gb.x + 2, gb.y + 4),
                              IM_COL32(0, 0, 0, 90), 6.0f);
            dl->AddRectFilled(ga, gb, col::bg_card, 6.0f);
            dl->AddRect      (ga, gb, col::border_accent, 6.0f);
            const float gcy = (ga.y + gb.y) * 0.5f;
            glyph_drag_handle(dl, ImVec2(ga.x + 14.0f, gcy), col::accent);
            dl->PushClipRect(ImVec2(ga.x + 28.0f, ga.y),
                             ImVec2(gb.x - 8.0f,  gb.y), true);
            dl->AddText(ImVec2(ga.x + 28.0f, gcy - ImGui::GetTextLineHeight() * 0.5f),
                        col::text_primary, src.name);
            // No-op state: tell the user nothing will happen if they
            // release here. Saves them an "oh that didn't work" beat.
            if (no_op) {
                ImGui::GetWindowDrawList()->AddText(
                    ImVec2(ga.x + 28.0f, gcy + 2.0f),
                    col::text_dimmer, "release = stay in place");
            }
            dl->PopClipRect();

            // Commit on mouse release. We treat "button no longer held"
            // as the release condition  handles the case where the
            // user released outside the panel window.
            if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
            {
                if (!no_op) {
                    std::vector<effect_technique> order;
                    order.reserve(g_techs.size());
                    for (const auto &t : g_techs) order.push_back(t.handle);
                    const effect_technique moved = order[g_drag_src];
                    order.erase(order.begin() + g_drag_src);
                    int insert_at = target_idx;
                    if (target_idx > g_drag_src) insert_at -= 1;
                    if (insert_at < 0) insert_at = 0;
                    if (insert_at > (int)order.size()) insert_at = (int)order.size();
                    order.insert(order.begin() + insert_at, moved);
                    rt->reorder_techniques(order.size(), order.data());
                }
                g_drag_src        = -1;
                g_drag_target_idx = -1;
            }
        }
    }

    // ── Commit any pending context-menu action ───────────────────────
    // Each branch is responsible for resetting the action pair to NONE so
    // it doesn't fire repeatedly. Bounds-checked against the live g_techs
    // size since refresh_tech_list may have shrunk it.
    if (g_ctx_action != CTX_NONE && g_ctx_action_idx >= 0 &&
        g_ctx_action_idx < (int)g_techs.size())
    {
        const TechRow &row = g_techs[g_ctx_action_idx];
        switch (g_ctx_action) {
        case CTX_MOVE_TOP:
        case CTX_MOVE_BOTTOM: {
            std::vector<effect_technique> order;
            order.reserve(g_techs.size());
            for (const auto &t : g_techs) order.push_back(t.handle);
            const effect_technique moved = order[g_ctx_action_idx];
            order.erase(order.begin() + g_ctx_action_idx);
            if (g_ctx_action == CTX_MOVE_TOP) order.insert(order.begin(), moved);
            else                              order.push_back(moved);
            rt->reorder_techniques(order.size(), order.data());
            break;
        }
        case CTX_RESET_UNIFORMS: {
            // Walk every uniform belonging to this technique's effect and
            // reset it. We can't filter by technique directly  uniforms
            // are scoped to the .fx file  but resetting the whole effect
            // is the closest match and is what the upstream "Reset to
            // default" already does.
            rt->enumerate_uniform_variables(row.effect_full,
                [](effect_runtime *r, effect_uniform_variable v) {
                    r->reset_uniform_value(v);
                });
            break;
        }
        case CTX_OPEN_FX: {
            // Open the parent folder of the .fx file in Explorer. Using
            // ShellExecuteA so the user lands on the file selected in
            // Windows Explorer when available.
            char path_arg[600];
            snprintf(path_arg, sizeof(path_arg), "/select,\"%s\"", row.effect_full);
            ShellExecuteA(nullptr, "open", "explorer.exe", path_arg, nullptr, SW_SHOWNORMAL);
            break;
        }
        case CTX_COPY_NAME:
            ImGui::SetClipboardText(row.name);
            break;
        default: break;
        }
    }
    g_ctx_action_idx = -1;
    g_ctx_action     = CTX_NONE;
}

// ── Parameter editor (right side) ──────────────────────────────────────────
//
// Layout (top-down, all dimensions from L::):
//
//   ┌─ panel_pad ────────────────────────────────────────────────────────┐
//   │  ┌── header card (pe_header_h) ────────────────────────────────┐   │
//   │  │  Name (16pt)                              GPU 0.43 / CPU   │   │
//   │  │  effect.fx                                       [ toggle ] │   │
//   │  └────────────────────────────────────────────────────────────┘   │
//   │  ┌── body toolbar (pe_bar_h) ──────────────────────────────────┐   │
//   │  │   3 categories  24 parameters         [▾ all] [▴ all] [↺]  │   │
//   │  └────────────────────────────────────────────────────────────┘   │
//   │  ┌── scroll body ──────────────────────────────────────────────┐   │
//   │  │  ▾ CATEGORY                                                │   │
//   │  │   Param Name           [ slider ───────── ]    1.00   ↺    │   │
//   │  │   Param Name           [ slider ───────── ]    0.50   ↺    │   │
//   │  │  ▾ ANOTHER CATEGORY                                         │   │
//   │  │   ...                                                       │   │
//   │  └────────────────────────────────────────────────────────────┘   │
//   └────────────────────────────────────────────────────────────────────┘