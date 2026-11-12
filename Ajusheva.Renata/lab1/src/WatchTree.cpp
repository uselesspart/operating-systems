#include "WatchTree.hpp"

#include "PathUtil.hpp"

#include <algorithm>

namespace diskmond {

bool WatchTree::addRoot(int wd, const std::string& path) {
    if (contains(wd)) {
        return false;
    }
    nodes_[wd] = Node{-1, path, {}};
    return true;
}

bool WatchTree::addChild(int wd, int parentWd, const std::string& name) {
    const auto parent = nodes_.find(parentWd);
    if (contains(wd) || parent == nodes_.end() || name.empty()) {
        return false;
    }
    
    parent->second.children[name] = wd;
    nodes_[wd] = Node{parentWd, name, {}};
    return true;
}

bool WatchTree::contains(int wd) const {
    return nodes_.count(wd) > 0;
}

bool WatchTree::isRoot(int wd) const {
    const auto node = nodes_.find(wd);
    return node != nodes_.end() && node->second.parent < 0;
}

std::optional<std::string> WatchTree::pathOf(int wd) const {
    std::vector<const std::string*> names;
    int current = wd;
    
    for (std::size_t depth = 0; depth <= nodes_.size(); ++depth) {
        const auto node = nodes_.find(current);
        if (node == nodes_.end()) {
            return std::nullopt;  // узел оторван от дерева
        }
        names.push_back(&node->second.name);
        if (node->second.parent < 0) {
            std::string result = *names.back();
            for (auto name = names.rbegin() + 1; name != names.rend(); ++name) {
                result = path::join(result, **name);
            }
            return result;
        }
        current = node->second.parent;
    }
    return std::nullopt;
}

std::optional<int> WatchTree::childOf(int parentWd, const std::string& name) const {
    const auto parent = nodes_.find(parentWd);
    if (parent == nodes_.end()) {
        return std::nullopt;
    }
    const auto child = parent->second.children.find(name);
    if (child == parent->second.children.end()) {
        return std::nullopt;
    }
    return child->second;
}

std::optional<int> WatchTree::findRoot(const std::string& path) const {
    for (const auto& entry : nodes_) {
        if (entry.second.parent < 0 && entry.second.name == path) {
            return entry.first;
        }
    }
    return std::nullopt;
}

std::vector<int> WatchTree::roots() const {
    std::vector<int> result;
    for (const auto& entry : nodes_) {
        if (entry.second.parent < 0) {
            result.push_back(entry.first);
        }
    }
    std::sort(result.begin(), result.end());
    return result;
}

std::vector<int> WatchTree::ids() const {
    std::vector<int> result;
    result.reserve(nodes_.size());
    for (const auto& entry : nodes_) {
        result.push_back(entry.first);
    }
    std::sort(result.begin(), result.end());
    return result;
}

std::size_t WatchTree::subtreeSize(int wd) const {
    return collectSubtree(wd).size();
}

bool WatchTree::move(int wd, int newParentWd, const std::string& newName) {
    const auto node = nodes_.find(wd);
    if (node == nodes_.end() || node->second.parent < 0 || !contains(newParentWd) || newName.empty()) {
        return false;
    }
    
    const std::vector<int> subtree = collectSubtree(wd);
    if (std::find(subtree.begin(), subtree.end(), newParentWd) != subtree.end()) {
        return false;
    }

    detach(wd, node->second);
    node->second.parent = newParentWd;
    node->second.name = newName;
    nodes_[newParentWd].children[newName] = wd;
    return true;
}

std::vector<int> WatchTree::removeSubtree(int wd) {
    const auto node = nodes_.find(wd);
    if (node == nodes_.end()) {
        return {};
    }
    detach(wd, node->second);
    const std::vector<int> removed = collectSubtree(wd);
    for (const int id : removed) {
        nodes_.erase(id);
    }
    return removed;
}

void WatchTree::detach(int wd, const Node& node) {
    if (node.parent < 0) {
        return;
    }
    const auto parent = nodes_.find(node.parent);
    if (parent == nodes_.end()) {
        return;
    }
    const auto entry = parent->second.children.find(node.name);
    
    if (entry != parent->second.children.end() && entry->second == wd) {
        parent->second.children.erase(entry);
    }
}

std::vector<int> WatchTree::collectSubtree(int wd) const {
    std::vector<int> result;
    if (!contains(wd)) {
        return result;
    }
    std::vector<int> pending{wd};
    while (!pending.empty()) {
        const int current = pending.back();
        pending.pop_back();
        result.push_back(current);
        for (const auto& child : nodes_.at(current).children) {
           
            const auto childNode = nodes_.find(child.second);
            if (childNode != nodes_.end() && childNode->second.parent == current) {
                pending.push_back(child.second);
            }
        }
    }
    return result;
}

}  