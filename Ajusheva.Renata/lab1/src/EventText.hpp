#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace diskmond {


enum class Action {
    Open,        
    Read,        
    Write,       
    Close,       
    CloseWrite,  
    Attrib,      
    Create,      
    Delete,      
    Rename,      
    Move,        
    MoveOut,     
    MoveIn,      
    DeleteSelf,  
    MoveSelf,    
    Unmount,     
};

const char* actionName(Action action);


std::optional<Action> actionForMask(std::uint32_t mask);


std::string formatEntry(Action action, bool isDirectory, const std::string& path,
                        const std::string& target = {});


std::string escapeForLog(const std::string& path);

}  
