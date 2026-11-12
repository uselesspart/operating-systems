#include "check.hpp"

#include "WatchTree.hpp"

#include <algorithm>

using namespace diskmond;

namespace {

std::vector<int> sorted(std::vector<int> values) {
    std::sort(values.begin(), values.end());
    return values;
}

std::vector<int> ints(std::initializer_list<int> values) {
    return std::vector<int>(values);
}


WatchTree sampleTree() {
    WatchTree tree;
    tree.addRoot(1, "/w");
    tree.addChild(2, 1, "a");
    tree.addChild(3, 2, "b");
    tree.addChild(4, 1, "c");
    return tree;
}

} 

TEST(tree_builds_paths) {
    const WatchTree tree = sampleTree();
    CHECK_EQ(tree.pathOf(1), "/w");
    CHECK_EQ(tree.pathOf(3), "/w/a/b");
    CHECK_EQ(tree.pathOf(4), "/w/c");
    CHECK(!tree.pathOf(99));
    CHECK(tree.isRoot(1));
    CHECK(!tree.isRoot(3));
    CHECK_EQ(tree.childOf(1, "a"), 2);
    CHECK(!tree.childOf(1, "missing"));
    CHECK_EQ(tree.size(), 4u);
    CHECK_EQ(tree.subtreeSize(2), 2u);
}

TEST(tree_rejects_bad_additions) {
    WatchTree tree = sampleTree();
    CHECK(!tree.addRoot(2, "/other"));  
    CHECK(!tree.addChild(5, 99, "x"));  
    CHECK(!tree.addChild(5, 1, ""));    
    CHECK_EQ(tree.size(), 4u);
}

TEST(tree_rename_updates_descendants) {
    WatchTree tree = sampleTree();
    CHECK(tree.move(2, 1, "renamed"));
    CHECK_EQ(tree.pathOf(3), "/w/renamed/b");  
    CHECK(!tree.childOf(1, "a"));
    CHECK_EQ(tree.childOf(1, "renamed"), 2);

    CHECK(tree.move(2, 4, "moved"));
    CHECK_EQ(tree.pathOf(3), "/w/c/moved/b");
    CHECK_EQ(tree.childOf(4, "moved"), 2);
}

TEST(tree_move_refuses_impossible) {
    WatchTree tree = sampleTree();
    CHECK(!tree.move(2, 3, "x"));   
    CHECK(!tree.move(1, 4, "x"));   
    CHECK(!tree.move(2, 99, "x"));  
    CHECK(!tree.move(99, 1, "x"));  
    CHECK_EQ(tree.pathOf(3), "/w/a/b");
}

TEST(tree_remove_subtree) {
    WatchTree tree = sampleTree();
    CHECK_EQ(sorted(tree.removeSubtree(2)), ints({2, 3}));
    CHECK(!tree.contains(2));
    CHECK(!tree.contains(3));
    CHECK(!tree.childOf(1, "a"));
    CHECK_EQ(tree.size(), 2u);
    CHECK(tree.removeSubtree(42).empty());
}

TEST(tree_name_reused_before_old_watch_is_gone) {
    
    WatchTree tree = sampleTree();
    CHECK(tree.addChild(7, 1, "a"));
    CHECK_EQ(tree.childOf(1, "a"), 7);
    CHECK_EQ(sorted(tree.removeSubtree(2)), ints({2, 3}));
    CHECK_EQ(tree.childOf(1, "a"), 7);
    CHECK_EQ(tree.pathOf(7), "/w/a");
}

TEST(tree_roots) {
    WatchTree tree = sampleTree();
    tree.addRoot(10, "/v");
    CHECK_EQ(tree.roots(), ints({1, 10}));
    CHECK_EQ(tree.findRoot("/v"), 10);
    CHECK(!tree.findRoot("/w/a"));
    CHECK_EQ(tree.ids(), ints({1, 2, 3, 4, 10}));
    tree.clear();
    CHECK_EQ(tree.size(), 0u);
}
