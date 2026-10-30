#include "enhancements.h"

#include <libultraship/bridge/consolevariablebridge.h>

extern "C" int syNetLobbyIsRoomOpen(void);
extern "C" int np_match_cheats(void);

extern "C" {
    int port_cheat_unlock_all()        { return CVarGetInteger("gCheats.UnlockAll", 0); }
    int port_cheat_unlock_luigi()      { return CVarGetInteger("gCheats.UnlockLuigi", 0); }
    int port_cheat_unlock_ness()       { return CVarGetInteger("gCheats.UnlockNess", 0); }
    int port_cheat_unlock_captain()    { return CVarGetInteger("gCheats.UnlockCaptain", 0); }
    int port_cheat_unlock_purin()      { return CVarGetInteger("gCheats.UnlockPurin", 0); }
    int port_cheat_unlock_inishie()    { return CVarGetInteger("gCheats.UnlockInishie", 0); }
    int port_cheat_unlock_soundtest()  { return CVarGetInteger("gCheats.UnlockSoundTest", 0); }
    int port_cheat_unlock_itemswitch() { return CVarGetInteger("gCheats.UnlockItemSwitch", 0); }

    // Any unlock cheat on? (folded into the netplay settings hash)
    int port_cheat_unlock_all_active() {
        return port_cheat_unlock_all() | port_cheat_unlock_luigi() | port_cheat_unlock_ness() |
               port_cheat_unlock_captain() | port_cheat_unlock_purin() | port_cheat_unlock_inishie() |
               port_cheat_unlock_soundtest() | port_cheat_unlock_itemswitch();
    }

    // Gameplay cheats. Offline they come from the local CVars. In an online room the host's
    // choice (sent with the match config, identical on every peer) is used instead and the local
    // CVars are ignored, so nobody desyncs; everything switches off when the room closes.
    static int cheat(int bit, const char* cvar) {
        if (syNetLobbyIsRoomOpen()) return (np_match_cheats() & bit) != 0;
        return CVarGetInteger("gCheats.Gameplay", 0) && CVarGetInteger(cvar, 0);
    }
    int port_cheat_gameplay_enabled()  { return syNetLobbyIsRoomOpen() ? (np_match_cheats() != 0) : CVarGetInteger("gCheats.Gameplay", 0); }
    int port_cheat_infinite_jumps()    { return cheat(1,  "gCheats.InfiniteJumps"); }
    int port_cheat_infinite_shield()   { return cheat(2,  "gCheats.InfiniteShield"); }
    int port_cheat_zero_percent()      { return cheat(4,  "gCheats.ZeroPercent"); }
    int port_cheat_infinite_stocks()   { return cheat(8,  "gCheats.InfiniteStocks"); }
    int port_cheat_enemy_999()         { return cheat(16, "gCheats.Enemy999"); }
}
