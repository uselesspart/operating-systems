#pragma once

#include <cstddef>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace diskmond {


class WatchTree {
  public:
    
    bool addRoot(int wd, const std::string& path);
    
    bool addChild(int wd, int parentWd, const std::string& name);

    bool contains(int wd) const;
    bool isRoot(int wd) const;
    std::optional<std::string> pathOf(int wd) const;
    std::optional<int> childOf(int parentWd, const std::string& name) const;
    std::optional<int> findRoot(const std::string& path) const;

    std::vector<int> roots() const;
    std::vector<int> ids() const;
    std::size_t size() const noexcept { return nodes_.size(); }
    std::size_t subtreeSize(int wd) const;

    
    bool move(int wd, int newParentWd, const std::string& newName);

    
    std::vector<int> removeSubtree(int wd);

    void clear() noexcept { nodes_.clear(); }

  private:
    
    struct Node {
        int parent = -1;
        std::string name;
        std::map<std::string, int> children;
    };

    void detach(int wd, const Node& node);
    std::vector<int> collectSubtree(int wd) const;

    std::unordered_map<int, Node> nodes_;
};

}  
