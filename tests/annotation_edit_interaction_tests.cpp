#include "greenflame_core/annotation_edit_interaction.h"

using namespace greenflame::core;

namespace {

Annotation Make_stroke(uint64_t id, std::initializer_list<PointPx> points) {
    Annotation annotation{};
    annotation.id = id;
    annotation.data = FreehandStrokeAnnotation{
        .points = std::vector<PointPx>(points),
        .style = {},
    };
    return annotation;
}

Annotation Make_straight_highlighter(uint64_t id, PointPx start, PointPx end) {
    Annotation annotation{};
    annotation.id = id;
    annotation.data = FreehandStrokeAnnotation{
        .points = {start, end},
        .style = {},
        .freehand_tip_shape = FreehandTipShape::Square,
    };
    return annotation;
}

Annotation Make_line(uint64_t id, PointPx start, PointPx end,
                     int32_t width_px = StrokeStyle::kDefaultWidthPx,
                     bool arrow_head = false) {
    Annotation annotation{};
    annotation.id = id;
    annotation.data = LineAnnotation{
        .start = start,
        .end = end,
        .style = {.width_px = width_px},
        .arrow_head = arrow_head,
    };
    return annotation;
}

Annotation Make_rectangle(uint64_t id, RectPx outer_bounds, int32_t width_px,
                          bool filled = false) {
    Annotation annotation{};
    annotation.id = id;
    annotation.data = RectangleAnnotation{
        .outer_bounds = outer_bounds,
        .style = {.width_px = width_px},
        .filled = filled,
    };
    return annotation;
}

Annotation Make_ellipse(uint64_t id, RectPx outer_bounds, int32_t width_px,
                        bool filled = false) {
    Annotation annotation{};
    annotation.id = id;
    annotation.data = EllipseAnnotation{
        .outer_bounds = outer_bounds,
        .style = {.width_px = width_px},
        .filled = filled,
    };
    return annotation;
}

Annotation Make_obfuscate(uint64_t id, RectPx bounds, int32_t block_size) {
    Annotation annotation{};
    annotation.id = id;
    annotation.data = ObfuscateAnnotation{
        .bounds = bounds.Normalized(),
        .block_size = block_size,
    };
    return annotation;
}

Annotation Make_bubble(uint64_t id, PointPx center, int32_t diameter_px) {
    Annotation annotation{};
    annotation.id = id;
    annotation.data = BubbleAnnotation{
        .center = center,
        .diameter_px = diameter_px,
        .color = RGB(0x88, 0xcc, 0x44),
        .font_choice = TextFontChoice::Sans,
        .counter_value = 1,
    };
    return annotation;
}

class RecordingEditInteractionHost final : public IAnnotationEditInteractionHost {
  public:
    [[nodiscard]] Annotation const *
    Annotation_at(size_t index) const noexcept override {
        if (index >= annotations.size()) {
            return nullptr;
        }
        return &annotations[index];
    }

    void Update_annotation_at(size_t index, Annotation annotation,
                              std::span<const uint64_t> selection) override {
        selected_annotation_ids =
            AnnotationSelection(selection.begin(), selection.end());
        selected_annotation_id = selection.size() == 1
                                     ? std::optional<uint64_t>{selection.front()}
                                     : std::nullopt;
        if (index >= annotations.size()) {
            return;
        }
        annotations[index] = std::move(annotation);
    }

    std::vector<Annotation> annotations = {};
    AnnotationSelection selected_annotation_ids = {};
    std::optional<uint64_t> selected_annotation_id = std::nullopt;
};

} // namespace

TEST(annotation_edit_interaction, HitTest_PrefersSelectedLineHandleOverBody) {
    Annotation const line = Make_line(1, {40, 40}, {80, 50}, 6);
    std::vector<Annotation> const annotations = {line};

    EXPECT_EQ(Hit_test_annotation_edit_target(&annotations[0], annotations, {40, 40}),
              (std::optional<AnnotationEditTarget>{
                  AnnotationEditTarget{1, AnnotationEditTargetKind::LineStartHandle}}));
    EXPECT_EQ(Hit_test_annotation_edit_target(&annotations[0], annotations, {60, 45}),
              (std::optional<AnnotationEditTarget>{
                  AnnotationEditTarget{1, AnnotationEditTargetKind::Body}}));
}

TEST(annotation_edit_interaction, MoveInteraction_ProducesUndoableCommandData) {
    RecordingEditInteractionHost host;
    host.annotations.push_back(Make_stroke(7, {{40, 40}, {60, 40}}));

    std::unique_ptr<IAnnotationEditInteraction> interaction =
        Create_annotation_edit_interaction(
            AnnotationEditTarget{7, AnnotationEditTargetKind::Body}, 0,
            host.annotations[0], {50, 40});
    ASSERT_NE(interaction, nullptr);
    EXPECT_TRUE(interaction->Is_move_drag());

    EXPECT_TRUE(interaction->Update(host, {70, 60}));
    EXPECT_EQ(host.selected_annotation_id, std::optional<uint64_t>{7});
    EXPECT_EQ(std::get<FreehandStrokeAnnotation>(host.annotations[0].data).points[0],
              (PointPx{60, 60}));

    std::optional<AnnotationEditCommandData> const command = interaction->Commit();
    ASSERT_TRUE(command.has_value());
    if (!command.has_value()) {
        return;
    }
    AnnotationEditCommandData const &command_value = *command;
    EXPECT_EQ(command_value.description, "Move annotation");
    EXPECT_EQ(command_value.selection_before, (AnnotationSelection{7}));
    EXPECT_EQ(command_value.selection_after, (AnnotationSelection{7}));
    EXPECT_EQ(std::get<FreehandStrokeAnnotation>(command_value.annotation_after.data)
                  .points[0],
              (PointPx{60, 60}));
}

TEST(annotation_edit_interaction,
     MoveInteraction_PreservesArrowHeadCoverageWhenMovingArrow) {
    RecordingEditInteractionHost host;
    host.annotations.push_back(Make_line(9, {10, 10}, {50, 10}, 4, true));

    std::unique_ptr<IAnnotationEditInteraction> interaction =
        Create_annotation_edit_interaction(
            AnnotationEditTarget{9, AnnotationEditTargetKind::Body}, 0,
            host.annotations[0], {30, 10});
    ASSERT_NE(interaction, nullptr);

    EXPECT_TRUE(interaction->Update(host, {50, 25}));
    {
        auto const &moved_line = std::get<LineAnnotation>(host.annotations[0].data);
        EXPECT_TRUE(moved_line.arrow_head);
        EXPECT_EQ(moved_line.start, (PointPx{30, 25}));
        EXPECT_EQ(moved_line.end, (PointPx{70, 25}));
    }
    EXPECT_TRUE(Annotation_hits_point(host.annotations[0], {70, 25}));
    EXPECT_TRUE(Annotation_hits_point(host.annotations[0], {60, 22}));
}

TEST(annotation_edit_interaction,
     LineEndpointInteraction_CancelRestoresOriginalLineAndExposesHandle) {
    RecordingEditInteractionHost host;
    Annotation const original = Make_line(1, {40, 40}, {90, 40}, 6);
    host.annotations.push_back(original);

    std::unique_ptr<IAnnotationEditInteraction> interaction =
        Create_annotation_edit_interaction(
            AnnotationEditTarget{1, AnnotationEditTargetKind::LineEndHandle}, 0,
            original, {90, 40});
    ASSERT_NE(interaction, nullptr);
    EXPECT_FALSE(interaction->Is_move_drag());
    EXPECT_EQ(interaction->Active_handle(), std::optional<AnnotationEditHandleKind>{
                                                AnnotationEditHandleKind::LineEnd});

    EXPECT_TRUE(interaction->Update(host, {120, 70}));
    EXPECT_EQ(std::get<LineAnnotation>(host.annotations[0].data).end,
              (PointPx{120, 70}));

    EXPECT_TRUE(interaction->Cancel(host));
    EXPECT_EQ(host.annotations[0], original);
    EXPECT_EQ(host.selected_annotation_id, std::optional<uint64_t>{1});
}

TEST(annotation_edit_interaction,
     LineEndpointInteraction_PreservesArrowHeadWhenUpdatingArrow) {
    RecordingEditInteractionHost host;
    Annotation const original = Make_line(2, {40, 40}, {90, 40}, 6, true);
    host.annotations.push_back(original);

    std::unique_ptr<IAnnotationEditInteraction> interaction =
        Create_annotation_edit_interaction(
            AnnotationEditTarget{2, AnnotationEditTargetKind::LineEndHandle}, 0,
            original, {90, 40});
    ASSERT_NE(interaction, nullptr);
    EXPECT_EQ(interaction->Active_handle(), std::optional<AnnotationEditHandleKind>{
                                                AnnotationEditHandleKind::LineEnd});

    EXPECT_TRUE(interaction->Update(host, {120, 70}));
    EXPECT_TRUE(std::get<LineAnnotation>(host.annotations[0].data).arrow_head);
    EXPECT_EQ(std::get<LineAnnotation>(host.annotations[0].data).end,
              (PointPx{120, 70}));
}

TEST(annotation_edit_interaction,
     HitTest_PrefersSelectedRectangleHandlesAndFallsBackToBody) {
    Annotation const rectangle =
        Make_rectangle(3, RectPx::From_ltrb(40, 40, 81, 81), 4);
    std::vector<Annotation> const annotations = {rectangle};

    EXPECT_EQ(Hit_test_annotation_edit_target(&annotations[0], annotations, {40, 40}),
              (std::optional<AnnotationEditTarget>{AnnotationEditTarget{
                  3, AnnotationEditTargetKind::RectangleTopLeftHandle}}));
    EXPECT_EQ(Hit_test_annotation_edit_target(&annotations[0], annotations, {48, 40}),
              (std::optional<AnnotationEditTarget>{
                  AnnotationEditTarget{3, AnnotationEditTargetKind::Body}}));
    EXPECT_FALSE(Annotation_hits_point(annotations[0], {60, 60}));
    EXPECT_EQ(Hit_test_annotation_edit_target(&annotations[0], annotations, {60, 60}),
              (std::optional<AnnotationEditTarget>{
                  AnnotationEditTarget{3, AnnotationEditTargetKind::Body}}));
}

TEST(annotation_edit_interaction,
     RectangleResizeInteraction_CancelRestoresOriginalRectangleAndExposesHandle) {
    RecordingEditInteractionHost host;
    Annotation const original =
        Make_rectangle(8, RectPx::From_ltrb(40, 40, 81, 81), 5, true);
    host.annotations.push_back(original);

    std::unique_ptr<IAnnotationEditInteraction> interaction =
        Create_annotation_edit_interaction(
            AnnotationEditTarget{8, AnnotationEditTargetKind::RectangleRightHandle}, 0,
            original, {80, 60});
    ASSERT_NE(interaction, nullptr);
    EXPECT_EQ(interaction->Active_handle(),
              std::optional<AnnotationEditHandleKind>{
                  AnnotationEditHandleKind::RectangleRight});

    EXPECT_TRUE(interaction->Update(host, {95, 60}));
    EXPECT_EQ(std::get<RectangleAnnotation>(host.annotations[0].data).outer_bounds,
              (RectPx::From_ltrb(40, 40, 96, 81)));

    EXPECT_TRUE(interaction->Cancel(host));
    EXPECT_EQ(host.annotations[0], original);
    EXPECT_EQ(host.selected_annotation_id, std::optional<uint64_t>{8});
}

TEST(annotation_edit_interaction,
     HitTest_PrefersSelectedEllipseHandlesAndFallsBackToBody) {
    Annotation const ellipse =
        Make_ellipse(14, RectPx::From_ltrb(40, 40, 81, 81), 4, true);
    std::vector<Annotation> const annotations = {ellipse};

    EXPECT_EQ(Hit_test_annotation_edit_target(&annotations[0], annotations, {40, 40}),
              (std::optional<AnnotationEditTarget>{AnnotationEditTarget{
                  14, AnnotationEditTargetKind::RectangleTopLeftHandle}}));
    EXPECT_EQ(Hit_test_annotation_edit_target(&annotations[0], annotations, {60, 60}),
              (std::optional<AnnotationEditTarget>{
                  AnnotationEditTarget{14, AnnotationEditTargetKind::Body}}));
}

TEST(annotation_edit_interaction,
     EllipseResizeInteraction_CancelRestoresOriginalEllipseAndExposesHandle) {
    RecordingEditInteractionHost host;
    Annotation const original =
        Make_ellipse(15, RectPx::From_ltrb(40, 40, 81, 81), 5, true);
    host.annotations.push_back(original);

    std::unique_ptr<IAnnotationEditInteraction> interaction =
        Create_annotation_edit_interaction(
            AnnotationEditTarget{15, AnnotationEditTargetKind::RectangleRightHandle}, 0,
            original, {80, 60});
    ASSERT_NE(interaction, nullptr);
    EXPECT_EQ(interaction->Active_handle(),
              std::optional<AnnotationEditHandleKind>{
                  AnnotationEditHandleKind::RectangleRight});

    EXPECT_TRUE(interaction->Update(host, {95, 60}));
    EXPECT_EQ(std::get<EllipseAnnotation>(host.annotations[0].data).outer_bounds,
              (RectPx::From_ltrb(40, 40, 96, 81)));

    std::optional<AnnotationEditCommandData> const command = interaction->Commit();
    ASSERT_TRUE(command.has_value());
    if (!command.has_value()) {
        return;
    }
    EXPECT_EQ(command.value().description, "Resize ellipse annotation");
}

TEST(annotation_edit_interaction,
     HitTest_PrefersSelectedObfuscateHandlesAndFallsBackToBody) {
    Annotation const obfuscate =
        Make_obfuscate(16, RectPx::From_ltrb(40, 40, 81, 81), 4);
    std::vector<Annotation> const annotations = {obfuscate};

    EXPECT_EQ(Hit_test_annotation_edit_target(&annotations[0], annotations, {40, 40}),
              (std::optional<AnnotationEditTarget>{AnnotationEditTarget{
                  16, AnnotationEditTargetKind::RectangleTopLeftHandle}}));
    EXPECT_EQ(Hit_test_annotation_edit_target(&annotations[0], annotations, {60, 60}),
              (std::optional<AnnotationEditTarget>{
                  AnnotationEditTarget{16, AnnotationEditTargetKind::Body}}));
}

TEST(annotation_edit_interaction,
     ObfuscateResizeInteraction_CommitUsesObfuscateDescription) {
    RecordingEditInteractionHost host;
    Annotation const original =
        Make_obfuscate(17, RectPx::From_ltrb(40, 40, 81, 81), 4);
    host.annotations.push_back(original);

    std::unique_ptr<IAnnotationEditInteraction> interaction =
        Create_annotation_edit_interaction(
            AnnotationEditTarget{17, AnnotationEditTargetKind::RectangleRightHandle}, 0,
            original, {80, 60});
    ASSERT_NE(interaction, nullptr);
    EXPECT_EQ(interaction->Active_handle(),
              std::optional<AnnotationEditHandleKind>{
                  AnnotationEditHandleKind::RectangleRight});

    EXPECT_TRUE(interaction->Update(host, {95, 60}));
    EXPECT_EQ(std::get<ObfuscateAnnotation>(host.annotations[0].data).bounds,
              (RectPx::From_ltrb(40, 40, 96, 81)));

    std::optional<AnnotationEditCommandData> const command = interaction->Commit();
    ASSERT_TRUE(command.has_value());
    if (!command.has_value()) {
        return;
    }
    EXPECT_EQ(command.value().description, "Resize obfuscate annotation");
}

TEST(annotation_edit_interaction,
     HitTest_PrefersSelectedFreehandStrokeHandlesOverBody) {
    // Horizontal stroke from {40,40} to {200,40}; midpoint {120,40} is on body.
    Annotation const hl = Make_straight_highlighter(5, {40, 40}, {200, 40});
    std::vector<Annotation> const annotations = {hl};

    EXPECT_EQ(Hit_test_annotation_edit_target(&annotations[0], annotations, {40, 40}),
              (std::optional<AnnotationEditTarget>{AnnotationEditTarget{
                  5, AnnotationEditTargetKind::FreehandStrokeStartHandle}}));
    EXPECT_EQ(Hit_test_annotation_edit_target(&annotations[0], annotations, {200, 40}),
              (std::optional<AnnotationEditTarget>{AnnotationEditTarget{
                  5, AnnotationEditTargetKind::FreehandStrokeEndHandle}}));
    EXPECT_EQ(Hit_test_annotation_edit_target(&annotations[0], annotations, {120, 40}),
              (std::optional<AnnotationEditTarget>{
                  AnnotationEditTarget{5, AnnotationEditTargetKind::Body}}));
}

TEST(annotation_edit_interaction, HitTest_FreehandStroke_NoHandlesForRoundTip) {
    // Round-tip 2-point freehand should NOT offer endpoint handles.
    Annotation const stroke = Make_stroke(6, {{40, 40}, {200, 40}});
    std::vector<Annotation> const annotations = {stroke};

    // Cursor at start endpoint should fall through to body hit test.
    EXPECT_EQ(Hit_test_annotation_edit_target(&annotations[0], annotations, {40, 40}),
              (std::optional<AnnotationEditTarget>{
                  AnnotationEditTarget{6, AnnotationEditTargetKind::Body}}));
}

TEST(annotation_edit_interaction, HitTest_SelectedBubbleBodyUsesSelectionFrameBounds) {
    Annotation const bubble = Make_bubble(18, {50, 50}, 20);
    std::vector<Annotation> const annotations = {bubble};

    EXPECT_FALSE(Annotation_hits_point(annotations[0], {40, 40}));
    EXPECT_TRUE(Annotation_selection_frame_bounds(annotations[0]).Contains({40, 40}));
    EXPECT_EQ(Hit_test_annotation_edit_target(&annotations[0], annotations, {40, 40}),
              (std::optional<AnnotationEditTarget>{
                  AnnotationEditTarget{18, AnnotationEditTargetKind::Body}}));
}

TEST(annotation_edit_interaction,
     HitTest_FreehandStroke_NoHandlesForMoreThanTwoPoints) {
    // Square-tip freehand with 3 points should NOT offer endpoint handles.
    Annotation annotation{};
    annotation.id = 7;
    annotation.data = FreehandStrokeAnnotation{
        .points = {{40, 40}, {120, 40}, {200, 40}},
        .style = {},
        .freehand_tip_shape = FreehandTipShape::Square,
    };
    std::vector<Annotation> const annotations = {annotation};

    EXPECT_EQ(Hit_test_annotation_edit_target(&annotations[0], annotations, {40, 40}),
              (std::optional<AnnotationEditTarget>{
                  AnnotationEditTarget{7, AnnotationEditTargetKind::Body}}));
}

TEST(annotation_edit_interaction,
     FreehandStrokeEndpointInteraction_StartHandleMovesStartPoint) {
    RecordingEditInteractionHost host;
    Annotation const original = Make_straight_highlighter(10, {40, 40}, {200, 40});
    host.annotations.push_back(original);

    std::unique_ptr<IAnnotationEditInteraction> interaction =
        Create_annotation_edit_interaction(
            AnnotationEditTarget{10,
                                 AnnotationEditTargetKind::FreehandStrokeStartHandle},
            0, original, {40, 40});
    ASSERT_NE(interaction, nullptr);
    EXPECT_FALSE(interaction->Is_move_drag());
    EXPECT_EQ(interaction->Active_handle(),
              std::optional<AnnotationEditHandleKind>{
                  AnnotationEditHandleKind::FreehandStrokeStart});

    EXPECT_TRUE(interaction->Update(host, {20, 60}));
    auto const &fh = std::get<FreehandStrokeAnnotation>(host.annotations[0].data);
    EXPECT_EQ(fh.points[0], (PointPx{20, 60}));  // start moved
    EXPECT_EQ(fh.points[1], (PointPx{200, 40})); // end unchanged
}

TEST(annotation_edit_interaction,
     FreehandStrokeEndpointInteraction_EndHandleMovesEndPoint) {
    RecordingEditInteractionHost host;
    Annotation const original = Make_straight_highlighter(11, {40, 40}, {200, 40});
    host.annotations.push_back(original);

    std::unique_ptr<IAnnotationEditInteraction> interaction =
        Create_annotation_edit_interaction(
            AnnotationEditTarget{11, AnnotationEditTargetKind::FreehandStrokeEndHandle},
            0, original, {200, 40});
    ASSERT_NE(interaction, nullptr);
    EXPECT_EQ(interaction->Active_handle(),
              std::optional<AnnotationEditHandleKind>{
                  AnnotationEditHandleKind::FreehandStrokeEnd});

    EXPECT_TRUE(interaction->Update(host, {220, 80}));
    auto const &fh = std::get<FreehandStrokeAnnotation>(host.annotations[0].data);
    EXPECT_EQ(fh.points[0], (PointPx{40, 40}));  // start unchanged
    EXPECT_EQ(fh.points[1], (PointPx{220, 80})); // end moved
}

TEST(annotation_edit_interaction,
     FreehandStrokeEndpointInteraction_CancelRestoresOriginalAndExposesHandle) {
    RecordingEditInteractionHost host;
    Annotation const original = Make_straight_highlighter(12, {40, 40}, {200, 40});
    host.annotations.push_back(original);

    std::unique_ptr<IAnnotationEditInteraction> interaction =
        Create_annotation_edit_interaction(
            AnnotationEditTarget{12, AnnotationEditTargetKind::FreehandStrokeEndHandle},
            0, original, {200, 40});
    ASSERT_NE(interaction, nullptr);

    EXPECT_TRUE(interaction->Update(host, {220, 80}));
    EXPECT_EQ(std::get<FreehandStrokeAnnotation>(host.annotations[0].data).points[1],
              (PointPx{220, 80}));

    EXPECT_TRUE(interaction->Cancel(host));
    EXPECT_EQ(host.annotations[0], original);
    EXPECT_EQ(host.selected_annotation_id, std::optional<uint64_t>{12});
}

TEST(annotation_edit_interaction,
     FreehandStrokeEndpointInteraction_CommitProducesUndoCommandData) {
    RecordingEditInteractionHost host;
    Annotation const original = Make_straight_highlighter(13, {40, 40}, {200, 40});
    host.annotations.push_back(original);

    std::unique_ptr<IAnnotationEditInteraction> interaction =
        Create_annotation_edit_interaction(
            AnnotationEditTarget{13,
                                 AnnotationEditTargetKind::FreehandStrokeStartHandle},
            0, original, {40, 40});
    ASSERT_NE(interaction, nullptr);

    EXPECT_TRUE(interaction->Update(host, {20, 20}));

    std::optional<AnnotationEditCommandData> const command = interaction->Commit();
    ASSERT_TRUE(command.has_value());
    if (!command.has_value()) {
        return;
    }
    AnnotationEditCommandData const &command_value = *command;
    EXPECT_EQ(command_value.description, "Edit highlighter annotation");
    EXPECT_EQ(command_value.selection_before, (AnnotationSelection{13}));
    EXPECT_EQ(command_value.selection_after, (AnnotationSelection{13}));
    EXPECT_EQ(std::get<FreehandStrokeAnnotation>(command_value.annotation_before.data)
                  .points[0],
              (PointPx{40, 40}));
    EXPECT_EQ(std::get<FreehandStrokeAnnotation>(command_value.annotation_after.data)
                  .points[0],
              (PointPx{20, 20}));
}

TEST(annotation_edit_interaction, HitTest_MultiSelectionBodyUsesSelectionBounds) {
    std::vector<Annotation> const annotations = {
        Make_rectangle(1, RectPx::From_ltrb(40, 40, 81, 81), 4),
        Make_rectangle(2, RectPx::From_ltrb(120, 40, 161, 81), 4),
    };
    AnnotationSelection const selected_ids = {1, 2};
    RectPx const selection_bounds = RectPx::From_ltrb(40, 40, 161, 81);

    EXPECT_EQ(Hit_test_annotation_edit_target(selected_ids, annotations,
                                              selection_bounds, {100, 60}),
              (std::optional<AnnotationEditTarget>{
                  AnnotationEditTarget{0, AnnotationEditTargetKind::SelectionBody}}));
}

TEST(annotation_edit_interaction,
     RectangleHandleTargets_CreateMatchingHandleInteractions) {
    Annotation const rectangle =
        Make_rectangle(21, RectPx::From_ltrb(40, 40, 81, 81), 4);
    std::vector<Annotation> const annotations = {rectangle};

    struct Case final {
        PointPx cursor = {};
        AnnotationEditTargetKind target_kind = AnnotationEditTargetKind::Body;
        AnnotationEditHandleKind handle_kind = AnnotationEditHandleKind::LineStart;
    };

    std::array<Case, 8> const cases = {{
        {{40, 40},
         AnnotationEditTargetKind::RectangleTopLeftHandle,
         AnnotationEditHandleKind::RectangleTopLeft},
        {{60, 40},
         AnnotationEditTargetKind::RectangleTopHandle,
         AnnotationEditHandleKind::RectangleTop},
        {{80, 40},
         AnnotationEditTargetKind::RectangleTopRightHandle,
         AnnotationEditHandleKind::RectangleTopRight},
        {{80, 60},
         AnnotationEditTargetKind::RectangleRightHandle,
         AnnotationEditHandleKind::RectangleRight},
        {{80, 80},
         AnnotationEditTargetKind::RectangleBottomRightHandle,
         AnnotationEditHandleKind::RectangleBottomRight},
        {{60, 80},
         AnnotationEditTargetKind::RectangleBottomHandle,
         AnnotationEditHandleKind::RectangleBottom},
        {{40, 80},
         AnnotationEditTargetKind::RectangleBottomLeftHandle,
         AnnotationEditHandleKind::RectangleBottomLeft},
        {{40, 60},
         AnnotationEditTargetKind::RectangleLeftHandle,
         AnnotationEditHandleKind::RectangleLeft},
    }};

    for (Case const &test_case : cases) {
        std::optional<AnnotationEditTarget> const target =
            Hit_test_annotation_edit_target(&annotations[0], annotations,
                                            test_case.cursor);
        ASSERT_TRUE(target.has_value());
        EXPECT_EQ(target->annotation_id, 21u);
        EXPECT_EQ(target->kind, test_case.target_kind);

        std::unique_ptr<IAnnotationEditInteraction> interaction =
            Create_annotation_edit_interaction(*target, 0, rectangle, test_case.cursor);
        ASSERT_NE(interaction, nullptr);
        EXPECT_EQ(interaction->Active_handle(),
                  std::optional<AnnotationEditHandleKind>{test_case.handle_kind});
    }
}

TEST(annotation_edit_interaction,
     CreateSelectionMoveInteraction_RejectsSelectionsSmallerThanTwo) {
    std::vector<Annotation> const annotations = {
        Make_rectangle(1, RectPx::From_ltrb(40, 40, 81, 81), 4),
    };
    AnnotationSelection const selection_ids = {1};

    EXPECT_EQ(
        Create_selection_move_edit_interaction(annotations, selection_ids, {40, 40}),
        nullptr);
}

TEST(annotation_edit_interaction,
     SelectionMoveInteraction_PreviewsCommitAllAndCancelTrackAllAnnotations) {
    RecordingEditInteractionHost host;
    Annotation const first = Make_rectangle(1, RectPx::From_ltrb(40, 40, 81, 81), 4);
    Annotation const second = Make_ellipse(2, RectPx::From_ltrb(120, 50, 171, 101), 6);
    host.annotations = {first, second};
    AnnotationSelection const selection_ids = {1, 2};

    std::unique_ptr<IAnnotationEditInteraction> interaction =
        Create_selection_move_edit_interaction(host.annotations, selection_ids,
                                               {60, 60});
    ASSERT_NE(interaction, nullptr);
    EXPECT_TRUE(interaction->Is_move_drag());

    EXPECT_TRUE(interaction->Update(host, {90, 100}));
    EXPECT_EQ(host.selected_annotation_id, std::nullopt);
    EXPECT_EQ(host.selected_annotation_ids, selection_ids);
    EXPECT_EQ(std::get<RectangleAnnotation>(host.annotations[0].data).outer_bounds,
              (RectPx::From_ltrb(70, 80, 111, 121)));
    EXPECT_EQ(std::get<EllipseAnnotation>(host.annotations[1].data).outer_bounds,
              (RectPx::From_ltrb(150, 90, 201, 141)));

    std::vector<AnnotationEditPreview> const previews = interaction->Previews();
    ASSERT_EQ(previews.size(), 2u);
    EXPECT_EQ(previews[0].index, 0u);
    EXPECT_EQ(previews[0].annotation_before, first);
    EXPECT_EQ(previews[0].annotation_after, host.annotations[0]);
    EXPECT_EQ(previews[1].index, 1u);
    EXPECT_EQ(previews[1].annotation_before, second);
    EXPECT_EQ(previews[1].annotation_after, host.annotations[1]);

    std::vector<AnnotationEditCommandData> const commands = interaction->Commit_all();
    ASSERT_EQ(commands.size(), 2u);
    EXPECT_EQ(commands[0].description, "Move annotations");
    EXPECT_EQ(commands[0].selection_before, selection_ids);
    EXPECT_EQ(commands[0].selection_after, selection_ids);
    EXPECT_EQ(commands[0].annotation_before, first);
    EXPECT_EQ(commands[0].annotation_after, host.annotations[0]);
    EXPECT_EQ(commands[1].description, "Move annotations");
    EXPECT_EQ(commands[1].selection_before, selection_ids);
    EXPECT_EQ(commands[1].selection_after, selection_ids);
    EXPECT_EQ(commands[1].annotation_before, second);
    EXPECT_EQ(commands[1].annotation_after, host.annotations[1]);

    EXPECT_TRUE(interaction->Cancel(host));
    EXPECT_EQ(host.annotations[0], first);
    EXPECT_EQ(host.annotations[1], second);
    EXPECT_EQ(host.selected_annotation_ids, selection_ids);
}
