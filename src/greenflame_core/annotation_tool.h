#pragma once

#include "greenflame_core/annotation_raster.h"

namespace greenflame::core {

class UndoStack;

enum class AnnotationToolId : uint8_t {
    Freehand,
    Line,
};

enum class AnnotationToolbarGlyph : uint8_t {
    None = 0,
    Brush,
    Line,
};

struct AnnotationToolDescriptor final {
    AnnotationToolId id = AnnotationToolId::Freehand;
    std::wstring name = {};
    wchar_t hotkey = L'\0';
    std::wstring toolbar_label = {};
    AnnotationToolbarGlyph toolbar_glyph = AnnotationToolbarGlyph::None;
};

struct AnnotationToolbarButtonView final {
    AnnotationToolId id = AnnotationToolId::Freehand;
    std::wstring label = {};
    std::wstring tooltip = {};
    AnnotationToolbarGlyph glyph = AnnotationToolbarGlyph::None;
    bool active = false;
};

class IAnnotationToolHost {
  public:
    IAnnotationToolHost() = default;
    IAnnotationToolHost(IAnnotationToolHost const &) = default;
    IAnnotationToolHost &operator=(IAnnotationToolHost const &) = default;
    IAnnotationToolHost(IAnnotationToolHost &&) = default;
    IAnnotationToolHost &operator=(IAnnotationToolHost &&) = default;
    virtual ~IAnnotationToolHost() = default;

    [[nodiscard]] virtual StrokeStyle Current_stroke_style() const noexcept = 0;
    [[nodiscard]] virtual uint64_t Next_annotation_id() const noexcept = 0;
    [[nodiscard]] virtual std::vector<PointPx>
    Smooth_points(std::span<const PointPx> points) const = 0;
    virtual void Commit_new_annotation(UndoStack &undo_stack,
                                       Annotation annotation) = 0;
};

class IAnnotationTool {
  public:
    IAnnotationTool() = default;
    IAnnotationTool(IAnnotationTool const &) = default;
    IAnnotationTool &operator=(IAnnotationTool const &) = default;
    IAnnotationTool(IAnnotationTool &&) = default;
    IAnnotationTool &operator=(IAnnotationTool &&) = default;
    virtual ~IAnnotationTool() = default;

    [[nodiscard]] virtual AnnotationToolDescriptor const &
    Descriptor() const noexcept = 0;

    virtual void Reset() noexcept = 0;
    [[nodiscard]] virtual bool Has_active_gesture() const noexcept = 0;
    [[nodiscard]] virtual bool On_primary_press(IAnnotationToolHost &host,
                                                PointPx cursor) = 0;
    [[nodiscard]] virtual bool On_pointer_move(IAnnotationToolHost &host,
                                               PointPx cursor) = 0;
    [[nodiscard]] virtual bool On_primary_release(IAnnotationToolHost &host,
                                                  UndoStack &undo_stack) = 0;
    [[nodiscard]] virtual bool On_cancel(IAnnotationToolHost &host) = 0;
    [[nodiscard]] virtual Annotation const *
    Draft_annotation(IAnnotationToolHost const &host) const noexcept {
        (void)host;
        return nullptr;
    }
    [[nodiscard]] virtual std::span<const PointPx>
    Draft_freehand_points() const noexcept {
        return {};
    }
    [[nodiscard]] virtual std::optional<StrokeStyle>
    Draft_freehand_style(IAnnotationToolHost const &host) const noexcept {
        (void)host;
        return std::nullopt;
    }
    [[nodiscard]] virtual std::optional<double>
    Draft_line_angle_radians() const noexcept {
        return std::nullopt;
    }
    virtual void On_stroke_style_changed() noexcept {}
};

} // namespace greenflame::core
