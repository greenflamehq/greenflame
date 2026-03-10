#include "greenflame_core/command.h"
#include "greenflame_core/modification_command.h"
#include "greenflame_core/undo_stack.h"

using namespace greenflame::core;

TEST(undo_stack, EmptyStack_CanUndoCanRedoFalse) {
    UndoStack stack;
    EXPECT_FALSE(stack.Can_undo());
    EXPECT_FALSE(stack.Can_redo());
    EXPECT_EQ(stack.Count(), 0u);
    EXPECT_EQ(stack.Index(), 0);
}

TEST(undo_stack, PushOne_CanUndoTrueCanRedoFalse) {
    int value = 0;
    UndoStack stack;
    stack.Push(std::make_unique<ModificationCommand<int>>(
        "set", [&](int v) { value = v; }, 0, 42));

    EXPECT_TRUE(stack.Can_undo());
    EXPECT_FALSE(stack.Can_redo());
    EXPECT_EQ(stack.Count(), 1u);
    EXPECT_EQ(stack.Index(), 1);
    EXPECT_EQ(value, 42);
}

TEST(undo_stack, Undo_CallsSetterWithBefore) {
    int value = 0;
    UndoStack stack;
    stack.Push(std::make_unique<ModificationCommand<int>>(
        "set", [&](int v) { value = v; }, 0, 42));

    stack.Undo();

    EXPECT_EQ(value, 0);
    EXPECT_FALSE(stack.Can_undo());
    EXPECT_TRUE(stack.Can_redo());
    EXPECT_EQ(stack.Index(), 0);
}

TEST(undo_stack, Redo_CallsSetterWithAfter) {
    int value = 0;
    UndoStack stack;
    stack.Push(std::make_unique<ModificationCommand<int>>(
        "set", [&](int v) { value = v; }, 0, 42));
    stack.Undo();

    stack.Redo();

    EXPECT_EQ(value, 42);
    EXPECT_TRUE(stack.Can_undo());
    EXPECT_FALSE(stack.Can_redo());
    EXPECT_EQ(stack.Index(), 1);
}

TEST(undo_stack, PushAfterUndo_ClearsRedo) {
    int value = 0;
    UndoStack stack;
    stack.Push(std::make_unique<ModificationCommand<int>>(
        "set1", [&](int v) { value = v; }, 0, 10));
    stack.Push(std::make_unique<ModificationCommand<int>>(
        "set2", [&](int v) { value = v; }, 10, 20));
    stack.Undo();
    EXPECT_TRUE(stack.Can_redo());

    stack.Push(std::make_unique<ModificationCommand<int>>(
        "set3", [&](int v) { value = v; }, 10, 30));

    EXPECT_FALSE(stack.Can_redo());
    EXPECT_EQ(stack.Count(), 2u);
    EXPECT_EQ(value, 30);
}

TEST(undo_stack, MultiplePushUndoRedo) {
    int value = 0;
    UndoStack stack;

    stack.Push(std::make_unique<ModificationCommand<int>>(
        "a", [&](int v) { value = v; }, 0, 1));
    stack.Push(std::make_unique<ModificationCommand<int>>(
        "b", [&](int v) { value = v; }, 1, 2));
    stack.Push(std::make_unique<ModificationCommand<int>>(
        "c", [&](int v) { value = v; }, 2, 3));

    EXPECT_EQ(value, 3);

    stack.Undo();
    EXPECT_EQ(value, 2);
    stack.Undo();
    EXPECT_EQ(value, 1);
    stack.Undo();
    EXPECT_EQ(value, 0);
    EXPECT_FALSE(stack.Can_undo());

    stack.Redo();
    EXPECT_EQ(value, 1);
    stack.Redo();
    EXPECT_EQ(value, 2);
    stack.Redo();
    EXPECT_EQ(value, 3);
    EXPECT_FALSE(stack.Can_redo());
}

TEST(undo_stack, Clear_EmptiesStack) {
    int value = 0;
    UndoStack stack;
    stack.Push(std::make_unique<ModificationCommand<int>>(
        "set", [&](int v) { value = v; }, 0, 42));

    stack.Clear();

    EXPECT_FALSE(stack.Can_undo());
    EXPECT_FALSE(stack.Can_redo());
    EXPECT_EQ(stack.Count(), 0u);
    EXPECT_EQ(stack.Index(), 0);
    EXPECT_EQ(value, 42);
}

TEST(undo_stack, UndoOnEmpty_NoOp) {
    UndoStack stack;
    stack.Undo();
    EXPECT_FALSE(stack.Can_undo());
    EXPECT_EQ(stack.Count(), 0u);
}

TEST(undo_stack, RedoOnEmpty_NoOp) {
    UndoStack stack;
    stack.Redo();
    EXPECT_FALSE(stack.Can_redo());
    EXPECT_EQ(stack.Count(), 0u);
}

TEST(undo_stack, ModificationCommand_UndoRedoApplyCorrectValues) {
    int value = 5;
    auto cmd = std::make_unique<ModificationCommand<int>>(
        "test", [&](int v) { value = v; }, 5, 10);

    cmd->Redo();
    EXPECT_EQ(value, 10);

    cmd->Undo();
    EXPECT_EQ(value, 5);

    cmd->Redo();
    EXPECT_EQ(value, 10);

    EXPECT_EQ(cmd->Description(), "test");
}

TEST(undo_stack, UndoLimit_DropsOldCommandsFromBottom) {
    int value = 0;
    UndoStack stack;
    stack.Set_undo_limit(2);

    stack.Push(std::make_unique<ModificationCommand<int>>(
        "a", [&](int v) { value = v; }, 0, 1));
    stack.Push(std::make_unique<ModificationCommand<int>>(
        "b", [&](int v) { value = v; }, 1, 2));
    stack.Push(std::make_unique<ModificationCommand<int>>(
        "c", [&](int v) { value = v; }, 2, 3));

    EXPECT_EQ(stack.Count(), 2u);
    EXPECT_EQ(stack.Index(), 2);

    stack.Undo();
    EXPECT_EQ(value, 2);
    stack.Undo();
    EXPECT_EQ(value, 1);
    EXPECT_FALSE(stack.Can_undo());
}
