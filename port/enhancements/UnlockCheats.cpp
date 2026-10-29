#include "enhancements.h"

#include <libultraship/bridge/consolevariablebridge.h>

extern "C" int syNetLobbyIsRoomOpen(void);

extern "C" {
    int port_cheat_unlock_all()        { return CVarGetInteger("gCheats.UnlockAll", 0); }
    int port_cheat_unlock_luigi()      { return CVarGetInteger("gCheats.UnlockLuigi", 0); }
    int port_cheat_unlock_ness()       { return CVarGetInteger("gCheats.UnlockNess", 0); }
    int port_cheat_unlock_captain()    { return CVarGetInteger("gCheats.UnlockCaptain", 0); }
    int port_cheat_unlock_purin()      { return CVarGetInteger("gCheats.UnlockPurin", 0); }
    int port_cheat_unlock_inishie()    { return CVarGetInteger("gCheats.UnlockInishie", 0); }
    int port_cheat_unlock_soundtest()  { return CVarGetInteger("gCheats.UnlockSoundTest", 0); }
    int port_cheat_unlock_itemswitch() { return CVarGetInteger("gCheats.UnlockItemSwitch", 0); }

    // Gameplay cheats are local-only: they would desync an online match, so they
    // switch themselves off while an online room is open.
    int port_cheat_gameplay_enabled()  { return CVarGetInteger("gCheats.Gameplay", 0) && !syNetLobbyIsRoomOpen(); }
    int port_cheat_infinite_jumps()    { return CVarGetInteger("gCheats.InfiniteJumps", 0); }
    int port_cheat_infinite_shield()   { return CVarGetInteger("gCheats.InfiniteShield", 0); }
    int port_cheat_zero_percent()      { return CVarGetInteger("gCheats.ZeroPercent", 0); }
    int port_cheat_infinite_stocks()   { return CVarGetInteger("gCheats.InfiniteStocks", 0); }
    int port_cheat_enemy_999()         { return CVarGetInteger("gCheats.Enemy999", 0); }
}
