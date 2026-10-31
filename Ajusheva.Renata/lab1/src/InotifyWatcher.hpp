#pragma once

#include "EventText.hpp"
#include "FileDescriptor.hpp"
#include "Settings.hpp"
#include "WatchTree.hpp"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>

namespace diskmond {


class InotifyWatcher {
  public:
    InotifyWatcher() = default;
    InotifyWatcher(const InotifyWatcher&) = delete;
    InotifyWatcher& operator=(const InotifyWatcher&) = delete;

    bool open(std::string& error);
    void close();
    int fd() const noexcept { return fd_.get(); }

    
    void apply(const Settings& settings);

    
    void consumeEvents();

    
    bool hasBacklog() const noexcept { return !queue_.empty(); }

    std::size_t directoryCount() const noexcept { return tree_.size(); }
    std::size_t rootCount() const { return tree_.roots().size(); }

  private:
    
    struct Event {
        int wd = -1;
        std::uint32_t mask = 0;
        std::uint32_t cookie = 0;
        std::string name;
        std::uint64_t seq = 0;
    };

    enum class ReadStatus { Read, Empty, Failed };

    
    enum class Scan { Configured, Created, Arrived };

    
    struct MoveSource {
        std::uint32_t cookie = 0;
        bool isDirectory = false;
        std::string path;
        std::optional<int> wd;  
    };

   
    using ListingKey = std::pair<int, std::string>;
    struct OwnListing {
        
        int pending = 0;
        
        std::uint64_t since = 0;
        
        int listedWd = -1;
    };

    ReadStatus readKernelQueue();
    void pullAvailableEvents();
    bool pairMayStillArrive() const;

    void dispatch(const Event& event);
    void onMovedTo(const Event& event, const std::string& path, bool isDirectory,
                   std::optional<MoveSource> from);
    void concludeMoveOut();
    bool consumeOwnListing(const Event& event);
    bool consumeExpectedCreate(int wd, const std::string& name);

    void attachTree(const std::string& path, int parentWd, const std::string& name, Scan reason);
    void forgetListing(const ListingKey& key);
    void detachTree(int wd);
    void detachAll();
    void rescan();
    void reportWatchFailure(const std::string& path, int error, bool isRoot);

    bool isFiltered(const std::string& path) const;
    void writeEntry(Action action, bool isDirectory, const std::string& path, const std::string& target = {});

    FileDescriptor fd_;
    WatchTree tree_;
    Settings settings_;
    std::deque<Event> queue_;
    std::uint64_t nextSeq_ = 0;
    std::optional<MoveSource> moveSource_;
    std::map<ListingKey, OwnListing> ownListings_;
    
    std::map<int, std::set<std::string>> expectedCreates_;
    bool watchLimitReported_ = false;
};

} 
