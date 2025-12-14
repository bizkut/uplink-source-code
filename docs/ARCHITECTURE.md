# Source Code Architecture

Quick reference for navigating the Uplink source code.

## Core Classes

### App Layer (`src/app/`)
- `App` - Main application class
- `Game` - Game state management  
- `Options` - Settings/configuration
- `Globals` - Global constants and utilities

### Game Layer (`src/game/`)
- `ScriptLibrary` - Button click handlers (Script1-Script93)
- `Data` - In-game file/data representation

### Interface Layer (`src/interface/`)

#### Task Manager (`taskmanager/`)
Hacking tools the player runs:
- `PasswordBreaker` - Cracks passwords character by character
- `VoiceAnalyser` - Records and replays voice samples
- `FileCopier` - Downloads files from remote servers
- `LogDeleter` - Deletes access logs
- `Decrypter` / `Decypher` - Decrypt encrypted files

Each tool has:
- `::Tick(int n)` - Called each frame, does the work
- `::SetTarget()` - Sets what to hack
- `::CreateInterface()` / `RemoveInterface()` - UI management

#### Local Interface (`localinterface/`)
Player-side UI:
- `LanInterface` - LAN network visualization
- `PhoneDialler` - Connection bounce interface
- `HUD` elements

#### Remote Interface (`remoteinterface/`)
Server screen implementations (login screens, file servers, etc.)

### World Layer (`src/world/`)
- `World` - Contains all game entities
- `Player` - Player state, inventory, accounts
- `Computer` / `BankComputer` / `LanComputer` - Server types
- `BankAccount` - Bank account logic
- `RecordBank` - Database records

## Important Patterns

### Task Tick Loop
All hacking tools inherit from `UplinkTask` and implement:
```cpp
void ToolName::Tick(int n) {
    if (!IsInterfaceVisible()) return;
    
    // Check still connected for remote operations
    if (remotefile && !game->GetWorld()->GetPlayer()->IsConnected()) {
        SvbRemoveTask(pid);
        return;
    }
    
    // Do work based on progress
    for (int count = 0; count < n; ++count) {
        ++progress;
        if (progress >= numticksrequired) {
            // Complete
        }
    }
}
```

### Script Library
Button clicks call `ScriptLibrary::RunScript(index)` which dispatches:
- Script10 = Bank money transfer
- Script11 = Bank account creation
- Script12 = Stock account creation
- Script15-17 = Database searches

### EclGetButton Pattern
UI elements are accessed by name:
```cpp
Button *button = EclGetButton("buttonname 0 0");
char *value = button->caption;
```

## String Constants
Use constants from `recordbank.h`:
- `RECORDBANK_NAME`, `RECORDBANK_PASSWORD`, etc.

## Memory Management
Uses `mmgr.h` for memory tracking. Include at end of includes.
