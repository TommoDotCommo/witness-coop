#pragma once
// Who opens first when the game starts (co-op standard K1, A5), game-free: the proxy calls Decide() at start-up, launcher/shell_test.cpp tests it.
// Pressing Play in Steam starts witness64_d3d11.exe with Steam's environment (SteamAppId / SteamGameId). Co-op lives in the launcher, so a game Steam
// started on its own hands over to witness_launcher.exe (which inherits Steam's environment, so the whole session stays inside the one Steam launch)
// and closes before it opens a window. A game the launcher started carries WITNESS_COOP_LAUNCHER in its environment and never hands over (no loop).
// A Steam invite (+connect_lobby <id>) always goes to the launcher: the lobby lives there. "Show launcher at start" off (show_launcher=0) lets Steam's
// Play start the game directly; holding Shift while it starts brings the launcher back.
namespace handover {
struct Inputs {
    bool launcherExists = false;      // witness_launcher.exe beside the game
    bool launcherSession = false;     // WITNESS_COOP_LAUNCHER is set: the launcher started this game
    bool steamLaunch = false;         // SteamAppId or SteamGameId is set: Steam started this game
    bool connectLobby = false;        // +connect_lobby <id> on the command line (a Steam invite)
    bool showLauncher = true;         // show_launcher (default 1)
    bool shiftHeld = false;           // Shift held while the game starts
};
enum Decision { kStay, kToLauncher, kToLauncherJoin };
inline Decision Decide(const Inputs& in) {
#ifndef HANDOVER_MUT_IGNORE_SESSION
    if (in.launcherSession) return kStay;      // the launcher's own game: handing it back would loop
#endif
    if (!in.launcherExists) return kStay;
    if (in.connectLobby) return kToLauncherJoin;
    if (!in.steamLaunch) return kStay;         // started some other way (a test tool, a shortcut to the exe): the game alone
    if (!in.showLauncher && !in.shiftHeld) return kStay;
    return kToLauncher;
}
constexpr const wchar_t* kSessionEnv = L"WITNESS_COOP_LAUNCHER";
}  // namespace handover
