#pragma once
// Minimal Windows x64 Steam ABI, not an SDK dependency (ported from the MDK2 co-op mod's bridge/steam_abi.h, MIT, same author, which is Win32).
// The game ships an old steam_api64.dll (SteamClient017, SteamNetworking005, no flat ISteamNetworkingMessages); its exported SteamClient() returns the installed
// Steam client's ISteamClient, which hands out current, versioned interfaces by name. Interface versions are requested explicitly; only the slots named here are used.
// Sources: Valve's public SDK headers and the Windows ABI wrappers in Proton's lsteamclient (github.com/ValveSoftware/Proton) and GameNetworkingSockets.
// x64 notes: one calling convention (`this` in rcx); a method that returns CSteamID writes it through a hidden pointer passed right after `this`;
// callback structs are packed to 8 on Windows.
#include <cstddef>
#include <cstdint>

namespace steamabi {
template <typename R, typename... A> inline R Call(void* obj, int slot, A... a) { return ((R(*)(void*, A...))(*(void***)obj)[slot])(obj, a...); }
inline uint64_t CallId(void* obj, int slot) { uint64_t r = 0; Call<uint64_t*>(obj, slot, &r); return r; }      // a method returning CSteamID, no arguments

constexpr uint32_t kAppId = 210970;      // The Witness
// interface versions (asked from the Steam client by name)
constexpr const char* kUser = "SteamUser023"; constexpr const char* kFriends = "SteamFriends017"; constexpr const char* kMatchmaking = "SteamMatchMaking009";
constexpr const char* kUtils = "SteamUtils010"; constexpr const char* kMessages = "SteamNetworkingMessages002";
enum Slot : int {
    SC_USER = 5, SC_FRIENDS = 8, SC_UTILS = 9, SC_MATCHMAKING = 10, SC_GENERIC = 12,                       // ISteamClient
    SU_LOGGED_ON = 1, SU_ID = 2,                                                                            // ISteamUser
    SF_PERSONA_NAME = 0, SF_FRIEND_COUNT = 3, SF_FRIEND_BY_INDEX = 4, SF_FRIEND_STATE = 6, SF_FRIEND_NAME = 7, SF_INVITE_DIALOG = 33, SF_PRESENCE = 43,   // ISteamFriends
    MM_CREATE = 13, MM_JOIN = 14, MM_LEAVE = 15, MM_INVITE = 16, MM_MEMBERS = 17, MM_MEMBER = 18, MM_DATA = 19, MM_SET_DATA = 20,
    MM_MEMBER_LIMIT = 32, MM_JOINABLE = 34, MM_OWNER = 35,                                                  // ISteamMatchmaking
    UT_APP_ID = 9, UT_COMPLETED = 11, UT_RESULT = 13, UT_OVERLAY = 17,                                       // ISteamUtils
    NM_SEND = 0, NM_RECEIVE = 1, NM_ACCEPT = 2, NM_CLOSE = 3, NM_INFO = 5,                                   // ISteamNetworkingMessages
};
enum Callback : int { CB_JOIN = 333, CB_PRESENCE_JOIN = 337, CB_ENTER = 504, CB_CHAT_UPDATE = 506, CB_CREATED = 513, CB_SESSION_REQUEST = 1251 };
enum : int { kLobbyFriendsOnly = 1, kFriendFlagImmediate = 4 };
enum : int { kSendUnreliable = 0, kSendNoNagle = 1, kSendAutoRestartBrokenSession = 32 };
// ESteamNetworkingConnectionState
enum : int { kConnNone = 0, kConnConnecting = 1, kConnFindingRoute = 2, kConnConnected = 3, kConnClosedByPeer = 4, kConnProblemLocally = 5 };
// EChatRoomEnterResponse
enum : int { kEnterSuccess = 1, kEnterDoesntExist = 2, kEnterNotAllowed = 3, kEnterFull = 4, kEnterError = 5, kEnterBanned = 6, kEnterLimited = 7 };

#pragma pack(push, 1)
struct Identity { int type = 0, size = 0; union { uint64_t id; unsigned char reserved[128]; } data{}; };      // SteamNetworkingIdentity
#pragma pack(pop)
inline Identity IdentityOf(uint64_t id) { Identity i; i.type = 16; i.size = 8; i.data.id = id; return i; }
inline uint64_t IdOf(const Identity& i) { return i.type == 16 && i.size == 8 ? i.data.id : 0; }

#pragma pack(push, 8)
struct Message {      // SteamNetworkingMessage_t
    void* data; int size; uint32_t connection; Identity identity; int64_t userData, received, number;
    void (*freeData)(Message*); void (*release)(Message*); int channel, flags; int64_t userData2;
};
struct Created { int result; uint64_t lobby; };                             // LobbyCreated_t
struct Enter { uint64_t lobby; uint32_t permissions; bool locked; uint32_t response; };      // LobbyEnter_t
struct JoinRequested { uint64_t lobby, friendId; };                         // GameLobbyJoinRequested_t
struct PresenceJoin { uint64_t friendId; char connect[256]; };              // GameRichPresenceJoinRequested_t
struct ChatUpdate { uint64_t lobby, changed, actor; uint32_t state; };      // LobbyChatUpdate_t
#pragma pack(pop)

// CCallbackBase: a vtable, a flags byte and the callback id. MSVC orders overloaded virtuals unpredictably for us (Run(void*) and Run(void*, bool, call)),
// so both slots point to one function that reads only (this, data): on x64 the caller cleans the stack, so the extra arguments do no harm.
struct CallbackBase;
struct CallbackVtable { void (*run)(CallbackBase*, void*); void (*run2)(CallbackBase*, void*); int (*size)(CallbackBase*); };
struct CallbackBase { const CallbackVtable* vtable; unsigned char flags; int id; };

static_assert(sizeof(void*) == 8, "the game is 64-bit");
static_assert(sizeof(Identity) == 136, "SteamNetworkingIdentity");
static_assert(offsetof(Message, identity) == 16 && offsetof(Message, release) == 184 && offsetof(Message, channel) == 192, "SteamNetworkingMessage_t");
static_assert(offsetof(Created, lobby) == 8 && sizeof(Enter) == 24 && offsetof(Enter, response) == 16, "lobby callbacks");
static_assert(offsetof(CallbackBase, id) == 12 && sizeof(CallbackBase) == 16, "CCallbackBase");
}  // namespace steamabi
