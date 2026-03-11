#include "network/multiplayerSystem.h"
#include "scene/entity.h"
#include <vector>
#include <unordered_map>
#include <chrono>
#include <cstring>
#include <random>
#include <algorithm>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifdef APIENTRY
#undef APIENTRY
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "Ws2_32.lib")
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif
#endif

namespace tsu {

namespace {

static constexpr uint32_t kMagic = 0x5453554E;
static constexpr uint8_t kMsgHello = 1;
static constexpr uint8_t kMsgSnapshot = 2;
static constexpr uint8_t kMsgInput = 3;
static constexpr uint8_t kMsgPing = 4;
static constexpr uint8_t kMsgPong = 5;

struct PacketHeader
{
    uint32_t magic = kMagic;
    uint8_t type = 0;
    uint8_t reservedA = 0;
    uint16_t reservedB = 0;
};

template<typename T>
static void Write(std::vector<uint8_t>& buf, const T& v)
{
    size_t at = buf.size();
    buf.resize(at + sizeof(T));
    std::memcpy(buf.data() + at, &v, sizeof(T));
}

static void WriteString(std::vector<uint8_t>& buf, const std::string& s)
{
    uint16_t len = (uint16_t)std::min<size_t>(s.size(), 1023);
    Write(buf, len);
    size_t at = buf.size();
    buf.resize(at + len);
    if (len > 0) std::memcpy(buf.data() + at, s.data(), len);
}

template<typename T>
static bool Read(const std::vector<uint8_t>& buf, size_t& at, T& out)
{
    if (at + sizeof(T) > buf.size()) return false;
    std::memcpy(&out, buf.data() + at, sizeof(T));
    at += sizeof(T);
    return true;
}

static bool ReadString(const std::vector<uint8_t>& buf, size_t& at, std::string& out)
{
    uint16_t len = 0;
    if (!Read(buf, at, len)) return false;
    if (at + len > buf.size()) return false;
    out.assign((const char*)buf.data() + at, (size_t)len);
    at += len;
    return true;
}

static uint64_t HashNick(const std::string& s)
{
    uint64_t h = 1469598103934665603ull;
    for (char c : s)
    {
        h ^= (uint8_t)c;
        h *= 1099511628211ull;
    }
    return h;
}

static uint64_t RandomBits()
{
    static std::mt19937_64 rng((uint64_t)std::chrono::high_resolution_clock::now().time_since_epoch().count());
    return rng();
}

struct ClientInfo
{
    sockaddr_in addr{};
    std::string nickname;
    uint64_t networkId = 0;
    int localEntity = -1;
};

struct RuntimeState
{
    bool init = false;
    bool running = false;
    bool host = true;
    SOCKET sock = INVALID_SOCKET;
    sockaddr_in serverAddr{};
    std::unordered_map<std::string, ClientInfo> clients;
    float tickAccum = 0.0f;
    float inputAccum = 0.0f;
    float pingAccum = 0.0f;
    float reconnectAccum = 0.0f;
    bool helloSent = false;
    bool connected = false;
    float pingMs = 0.0f;
    uint32_t pingSeq = 1;
    std::unordered_map<uint32_t, double> pingSentAt;
    int remotePlayerCount = 1;
    double now = 0.0;
    double lastPacketAt = 0.0;
};

static std::unordered_map<Scene*, RuntimeState> gStates;

static std::string AddrKey(const sockaddr_in& a)
{
    char ip[64] = {};
    inet_ntop(AF_INET, (void*)&a.sin_addr, ip, sizeof(ip));
    return std::string(ip) + ":" + std::to_string((int)ntohs(a.sin_port));
}

static bool StartSocket(RuntimeState& st, const MultiplayerManagerComponent& mm)
{
#ifdef _WIN32
    if (!st.init)
    {
        WSADATA wsa{};
        if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return false;
        st.init = true;
    }
    st.sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (st.sock == INVALID_SOCKET) return false;
    u_long nonBlock = 1;
    ioctlsocket(st.sock, FIONBIO, &nonBlock);
    st.host = (mm.Mode == MultiplayerMode::Host);
    if (st.host)
    {
        sockaddr_in local{};
        local.sin_family = AF_INET;
        local.sin_addr.s_addr = INADDR_ANY;
        local.sin_port = htons((u_short)mm.Port);
        if (bind(st.sock, (sockaddr*)&local, sizeof(local)) != 0) return false;
    }
    else
    {
        st.serverAddr = {};
        st.serverAddr.sin_family = AF_INET;
        inet_pton(AF_INET, mm.ServerAddress.c_str(), &st.serverAddr.sin_addr);
        st.serverAddr.sin_port = htons((u_short)mm.Port);
    }
    st.running = true;
    st.tickAccum = 0.0f;
    st.inputAccum = 0.0f;
    st.pingAccum = 0.0f;
    st.reconnectAccum = 0.0f;
    st.helloSent = false;
    st.connected = st.host;
    st.remotePlayerCount = st.host ? 1 : 0;
    st.pingMs = 0.0f;
    st.pingSentAt.clear();
    st.lastPacketAt = st.now;
    return true;
#else
    (void)st;
    (void)mm;
    return false;
#endif
}

static void StopSocket(RuntimeState& st)
{
#ifdef _WIN32
    if (st.sock != INVALID_SOCKET)
    {
        closesocket(st.sock);
        st.sock = INVALID_SOCKET;
    }
#endif
    st.running = false;
    st.clients.clear();
    st.connected = false;
    st.remotePlayerCount = 0;
    st.pingMs = 0.0f;
    st.pingSentAt.clear();
}

static uint64_t FindLocalNetworkId(const Scene& scene)
{
    for (int i = 0; i < (int)scene.MultiplayerControllers.size(); ++i)
    {
        const auto& mc = scene.MultiplayerControllers[i];
        if (mc.Active && mc.IsLocalPlayer && mc.NetworkId != 0)
            return mc.NetworkId;
    }
    return 0;
}

static int FindEntityByNetworkId(const Scene& scene, uint64_t nid)
{
    if (nid == 0) return -1;
    for (int i = 0; i < (int)scene.MultiplayerControllers.size(); ++i)
    {
        const auto& mc = scene.MultiplayerControllers[i];
        if (mc.Active && mc.NetworkId == nid) return i;
    }
    return -1;
}

static std::vector<uint8_t> BuildSnapshot(Scene& scene, bool withChannels, int playerCount)
{
    std::vector<uint8_t> buf;
    PacketHeader h;
    h.type = kMsgSnapshot;
    Write(buf, h);
    uint16_t pc = (uint16_t)std::max(1, playerCount);
    Write(buf, pc);
    uint16_t ec = (uint16_t)std::min<size_t>(scene.Transforms.size(), 2048);
    Write(buf, ec);
    for (uint16_t i = 0; i < ec; ++i)
    {
        Write(buf, i);
        bool alive = i < scene.EntityNames.size() && scene.EntityNames[i].rfind("__destroyed__", 0) != 0;
        uint8_t aliveByte = alive ? 1 : 0;
        Write(buf, aliveByte);
        uint8_t hasMc = (i < scene.MultiplayerControllers.size() && scene.MultiplayerControllers[i].Active) ? 1 : 0;
        Write(buf, hasMc);
        uint64_t ownerNid = (hasMc && i < scene.MultiplayerControllers.size()) ? scene.MultiplayerControllers[i].NetworkId : 0ull;
        Write(buf, ownerNid);
        const auto& t = scene.Transforms[i];
        Write(buf, t.Position.x); Write(buf, t.Position.y); Write(buf, t.Position.z);
        Write(buf, t.Rotation.x); Write(buf, t.Rotation.y); Write(buf, t.Rotation.z);
        Write(buf, t.Scale.x); Write(buf, t.Scale.y); Write(buf, t.Scale.z);
        std::string nm = (i < scene.EntityNames.size()) ? scene.EntityNames[i] : "Entity";
        std::string tg = (i < scene.EntityTags.size()) ? scene.EntityTags[i] : "";
        WriteString(buf, nm);
        WriteString(buf, tg);
    }
    uint16_t cc = withChannels ? (uint16_t)std::min<size_t>(scene.Channels.size(), 512) : 0;
    Write(buf, cc);
    for (uint16_t ci = 0; ci < cc; ++ci)
    {
        float v = scene.GetChannelFloat(ci, 0.0f);
        Write(buf, v);
    }
    return buf;
}

static void ApplySnapshot(Scene& scene, const std::vector<uint8_t>& pkt, RuntimeState& st)
{
    size_t at = sizeof(PacketHeader);
    uint16_t playerCount = 1;
    if (!Read(pkt, at, playerCount)) return;
    st.remotePlayerCount = std::max<int>(1, playerCount);
    uint16_t ec = 0;
    if (!Read(pkt, at, ec)) return;
    uint64_t localNid = FindLocalNetworkId(scene);
    for (uint16_t n = 0; n < ec; ++n)
    {
        uint16_t idx = 0;
        uint8_t aliveByte = 1;
        uint8_t hasMc = 0;
        uint64_t ownerNid = 0;
        float px, py, pz, rx, ry, rz, sx, sy, sz;
        if (!Read(pkt, at, idx)) return;
        if (!Read(pkt, at, aliveByte)) return;
        if (!Read(pkt, at, hasMc)) return;
        if (!Read(pkt, at, ownerNid)) return;
        if (!Read(pkt, at, px) || !Read(pkt, at, py) || !Read(pkt, at, pz)) return;
        if (!Read(pkt, at, rx) || !Read(pkt, at, ry) || !Read(pkt, at, rz)) return;
        if (!Read(pkt, at, sx) || !Read(pkt, at, sy) || !Read(pkt, at, sz)) return;
        std::string nm, tg;
        if (!ReadString(pkt, at, nm)) return;
        if (!ReadString(pkt, at, tg)) return;
        while (idx >= scene.Transforms.size())
            scene.CreateEntity("NetEntity");
        if (idx < scene.EntityNames.size())
            scene.EntityNames[idx] = nm;
        while (idx >= scene.EntityTags.size()) scene.EntityTags.push_back("");
        scene.EntityTags[idx] = tg;
        if (!aliveByte)
        {
            if (idx < scene.EntityNames.size() && scene.EntityNames[idx].rfind("__destroyed__", 0) != 0)
                scene.EntityNames[idx] = "__destroyed__" + scene.EntityNames[idx];
            continue;
        }
        if (hasMc && idx < scene.MultiplayerControllers.size())
        {
            auto& mc = scene.MultiplayerControllers[idx];
            mc.Active = true;
            mc.Replicated = true;
            mc.NetworkId = ownerNid;
            mc.OwnedLocally = (ownerNid != 0 && ownerNid == localNid);
            mc.LastNetReceiveTime = (float)st.now;
            mc.LastNetUpdateAge = 0.0f;
        }
        if (idx < scene.Transforms.size())
        {
            bool localLocked = false;
            if (idx < scene.MultiplayerControllers.size())
            {
                const auto& mc = scene.MultiplayerControllers[idx];
                localLocked = (mc.Active && mc.IsLocalPlayer) || mc.OwnedLocally;
            }
            if (!localLocked)
            {
                scene.Transforms[idx].Position = {px, py, pz};
                scene.Transforms[idx].Rotation = {rx, ry, rz};
                scene.Transforms[idx].Scale = {sx, sy, sz};
            }
        }
    }
    uint16_t cc = 0;
    if (!Read(pkt, at, cc)) return;
    for (uint16_t ci = 0; ci < cc; ++ci)
    {
        float v = 0.0f;
        if (!Read(pkt, at, v)) return;
        scene.SetChannelFloat(ci, v);
    }
}

static std::vector<uint8_t> BuildHello(Scene& scene)
{
    std::vector<uint8_t> buf;
    PacketHeader h;
    h.type = kMsgHello;
    Write(buf, h);
    std::string nick = "Player";
    uint64_t nid = 0;
    for (int i = 0; i < (int)scene.MultiplayerControllers.size(); ++i)
    {
        const auto& mc = scene.MultiplayerControllers[i];
        if (mc.Active && mc.IsLocalPlayer)
        {
            nick = mc.Nickname;
            nid = mc.NetworkId;
            break;
        }
    }
    WriteString(buf, nick);
    Write(buf, nid);
    return buf;
}

static std::vector<uint8_t> BuildInput(Scene& scene)
{
    std::vector<uint8_t> buf;
    PacketHeader h;
    h.type = kMsgInput;
    Write(buf, h);
    uint64_t localNid = 0;
    int16_t ent = -1;
    glm::vec3 p{0}, r{0}, s{1};
    for (int i = 0; i < (int)scene.MultiplayerControllers.size(); ++i)
    {
        const auto& mc = scene.MultiplayerControllers[i];
        if (mc.Active && mc.IsLocalPlayer && mc.SyncTransform && i < (int)scene.Transforms.size())
        {
            localNid = mc.NetworkId;
            ent = (int16_t)i;
            p = scene.Transforms[i].Position;
            r = scene.Transforms[i].Rotation;
            s = scene.Transforms[i].Scale;
            break;
        }
    }
    Write(buf, localNid);
    Write(buf, ent);
    Write(buf, p.x); Write(buf, p.y); Write(buf, p.z);
    Write(buf, r.x); Write(buf, r.y); Write(buf, r.z);
    Write(buf, s.x); Write(buf, s.y); Write(buf, s.z);
    uint16_t cc = (uint16_t)std::min<size_t>(scene.Channels.size(), 128);
    Write(buf, cc);
    for (uint16_t i = 0; i < cc; ++i)
    {
        Write(buf, scene.GetChannelFloat(i, 0.0f));
    }
    return buf;
}

static void ApplyInput(Scene& scene, const std::vector<uint8_t>& pkt)
{
    size_t at = sizeof(PacketHeader);
    uint64_t nid = 0;
    int16_t ent = -1;
    float px, py, pz, rx, ry, rz, sx, sy, sz;
    if (!Read(pkt, at, nid)) return;
    if (!Read(pkt, at, ent)) return;
    if (!Read(pkt, at, px) || !Read(pkt, at, py) || !Read(pkt, at, pz)) return;
    if (!Read(pkt, at, rx) || !Read(pkt, at, ry) || !Read(pkt, at, rz)) return;
    if (!Read(pkt, at, sx) || !Read(pkt, at, sy) || !Read(pkt, at, sz)) return;
    int target = ent;
    if (nid != 0)
    {
        int byOwner = FindEntityByNetworkId(scene, nid);
        if (byOwner >= 0) target = byOwner;
        if (target < 0 || target >= (int)scene.Transforms.size())
        {
            Entity e = scene.CreateEntity("RemotePlayer");
            target = (int)e.GetID();
        }
        if (target >= 0 && target < (int)scene.MultiplayerControllers.size())
        {
            auto& mc = scene.MultiplayerControllers[target];
            mc.Active = true;
            mc.NetworkId = nid;
            mc.Replicated = true;
            mc.OwnedLocally = false;
        }
    }
    if (target >= 0 && target < (int)scene.Transforms.size())
    {
        scene.Transforms[target].Position = {px, py, pz};
        scene.Transforms[target].Rotation = {rx, ry, rz};
        scene.Transforms[target].Scale = {sx, sy, sz};
    }
    uint16_t cc = 0;
    if (!Read(pkt, at, cc)) return;
    for (uint16_t i = 0; i < cc; ++i)
    {
        float v = 0.0f;
        if (!Read(pkt, at, v)) return;
        scene.SetChannelFloat(i, v);
    }
}

static bool ParsePacket(const std::vector<uint8_t>& pkt, PacketHeader& h)
{
    if (pkt.size() < sizeof(PacketHeader)) return false;
    std::memcpy(&h, pkt.data(), sizeof(PacketHeader));
    return h.magic == kMagic;
}

static std::vector<uint8_t> BuildPing(uint32_t seq)
{
    std::vector<uint8_t> buf;
    PacketHeader h;
    h.type = kMsgPing;
    Write(buf, h);
    Write(buf, seq);
    return buf;
}

static std::vector<uint8_t> BuildPong(uint32_t seq)
{
    std::vector<uint8_t> buf;
    PacketHeader h;
    h.type = kMsgPong;
    Write(buf, h);
    Write(buf, seq);
    return buf;
}

} // namespace

uint64_t MultiplayerSystem::MakeNetworkId(const std::string& nickname)
{
    return HashNick(nickname) ^ RandomBits();
}

bool MultiplayerSystem::IsConnected(const Scene& scene)
{
    auto it = gStates.find((Scene*)&scene);
    if (it == gStates.end()) return false;
    return it->second.connected;
}

bool MultiplayerSystem::IsHost(const Scene& scene)
{
    auto it = gStates.find((Scene*)&scene);
    if (it == gStates.end()) return false;
    return it->second.host;
}

int MultiplayerSystem::GetPlayerCount(const Scene& scene)
{
    auto it = gStates.find((Scene*)&scene);
    if (it == gStates.end()) return 1;
    return std::max(1, it->second.remotePlayerCount);
}

float MultiplayerSystem::GetPingMs(const Scene& scene)
{
    auto it = gStates.find((Scene*)&scene);
    if (it == gStates.end()) return 0.0f;
    return it->second.pingMs;
}

void MultiplayerSystem::Update(Scene& scene, float dt)
{
    double nowSec = std::chrono::duration<double>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    for (auto& mc : scene.MultiplayerControllers)
    {
        if (mc.Active && mc.NetworkId == 0)
            mc.NetworkId = MakeNetworkId(mc.Nickname);
        if (mc.Active && mc.Replicated && mc.LastNetReceiveTime >= 0.0f)
            mc.LastNetUpdateAge = (float)std::max(0.0, nowSec - mc.LastNetReceiveTime);
    }

    MultiplayerManagerComponent* mgr = nullptr;
    for (int i = 0; i < (int)scene.MultiplayerManagers.size(); ++i)
    {
        auto& mm = scene.MultiplayerManagers[i];
        if (mm.Active && mm.Enabled)
        {
            mgr = &mm;
            break;
        }
    }
    if (!mgr) return;

    RuntimeState& st = gStates[&scene];
    st.now = nowSec;
    if (mgr->RequestStop)
    {
        StopSocket(st);
        mgr->Running = false;
        mgr->RequestStop = false;
    }
    if (mgr->RequestStartHost)
    {
        mgr->Mode = MultiplayerMode::Host;
        mgr->Running = true;
        mgr->RequestStartHost = false;
        if (st.running) StopSocket(st);
    }
    if (mgr->RequestStartClient)
    {
        mgr->Mode = MultiplayerMode::Client;
        mgr->Running = true;
        mgr->RequestStartClient = false;
        if (st.running) StopSocket(st);
    }
    if (st.running && (!mgr->Enabled || !mgr->Active || !mgr->Running))
    {
        StopSocket(st);
    }
    if (!st.running && (mgr->AutoStart || mgr->Running))
    {
        if (StartSocket(st, *mgr))
            mgr->Running = true;
    }
    mgr->RuntimeState = st.running ? (st.host ? "Hosting" : (st.connected ? "Connected" : "Connecting")) : "Stopped";
    mgr->Connected = st.connected;
    mgr->ConnectedPlayers = st.host ? std::max(1, (int)st.clients.size() + 1) : std::max(0, st.remotePlayerCount);
    mgr->EstimatedPingMs = st.pingMs;
    mgr->TimeSinceLastPacket = st.lastPacketAt > 0.0 ? (float)std::max(0.0, st.now - st.lastPacketAt) : 0.0f;
    if (!st.running) return;

    char recvBuf[65536];
    for (;;)
    {
        sockaddr_in from{};
        int fromLen = sizeof(from);
        int n = recvfrom(st.sock, recvBuf, sizeof(recvBuf), 0, (sockaddr*)&from, &fromLen);
        if (n <= 0) break;
        std::vector<uint8_t> pkt((uint8_t*)recvBuf, (uint8_t*)recvBuf + n);
        PacketHeader h{};
        if (!ParsePacket(pkt, h)) continue;
        st.lastPacketAt = st.now;
        if (st.host)
        {
            if (h.type == kMsgHello)
            {
                size_t at = sizeof(PacketHeader);
                std::string nick;
                uint64_t nid = 0;
                if (!ReadString(pkt, at, nick)) continue;
                if (!Read(pkt, at, nid)) continue;
                std::string key = AddrKey(from);
                bool isNew = (st.clients.find(key) == st.clients.end() || st.clients[key].networkId == 0);
                auto& ci = st.clients[key];
                ci.addr = from;
                ci.nickname = nick;
                ci.networkId = nid;
                st.remotePlayerCount = std::max(1, (int)st.clients.size() + 1);

                // Assign this client's networkId to the first pre-spawned non-local slot
                // that hasn't been claimed yet (NetworkId == 0).
                if (isNew && nid != 0 && ci.localEntity < 0)
                {
                    for (int i = 0; i < (int)scene.MultiplayerControllers.size(); ++i)
                    {
                        auto& mc = scene.MultiplayerControllers[i];
                        if (mc.Active && !mc.IsLocalPlayer && mc.NetworkId == 0)
                        {
                            mc.NetworkId = nid;
                            mc.Nickname  = nick;
                            ci.localEntity = i;
                            break;
                        }
                    }
                }

                auto snap = BuildSnapshot(scene, mgr->ReplicateChannels, st.remotePlayerCount);
                sendto(st.sock, (const char*)snap.data(), (int)snap.size(), 0, (sockaddr*)&from, sizeof(from));
            }
            else if (h.type == kMsgInput)
            {
                ApplyInput(scene, pkt);
            }
            else if (h.type == kMsgPing)
            {
                size_t at = sizeof(PacketHeader);
                uint32_t seq = 0;
                if (!Read(pkt, at, seq)) continue;
                auto pong = BuildPong(seq);
                sendto(st.sock, (const char*)pong.data(), (int)pong.size(), 0, (sockaddr*)&from, sizeof(from));
            }
        }
        else
        {
            if (h.type == kMsgSnapshot)
            {
                ApplySnapshot(scene, pkt, st);
                st.connected = true;
                st.helloSent = true;
            }
            else if (h.type == kMsgPong)
            {
                size_t at = sizeof(PacketHeader);
                uint32_t seq = 0;
                if (!Read(pkt, at, seq)) continue;
                auto it = st.pingSentAt.find(seq);
                if (it != st.pingSentAt.end())
                {
                    st.pingMs = (float)((st.now - it->second) * 1000.0);
                    st.pingSentAt.erase(it);
                }
            }
        }
    }

    st.tickAccum += dt;
    if (st.host)
    {
        float step = 1.0f / std::max(2.0f, mgr->SnapshotRate);
        if (st.tickAccum >= step)
        {
            st.tickAccum = 0.0f;
            st.remotePlayerCount = std::max(1, (int)st.clients.size() + 1);
            auto snap = BuildSnapshot(scene, mgr->ReplicateChannels, st.remotePlayerCount);
            for (auto& kv : st.clients)
            {
                auto& ci = kv.second;
                sendto(st.sock, (const char*)snap.data(), (int)snap.size(), 0, (sockaddr*)&ci.addr, sizeof(ci.addr));
            }
        }
        st.connected = true;
    }
    else
    {
        if (st.connected && (st.now - st.lastPacketAt) > 3.0)
        {
            st.connected = false;
            st.helloSent = false;
            st.remotePlayerCount = 0;
        }
        st.reconnectAccum += dt;
        bool canRetry = mgr->AutoReconnect || !st.helloSent;
        if (!st.connected && canRetry && (!st.helloSent || st.reconnectAccum >= std::max(0.2f, mgr->ReconnectDelay)))
        {
            auto hello = BuildHello(scene);
            sendto(st.sock, (const char*)hello.data(), (int)hello.size(), 0, (sockaddr*)&st.serverAddr, sizeof(st.serverAddr));
            st.helloSent = true;
            st.reconnectAccum = 0.0f;
        }
        st.inputAccum += dt;
        if (st.connected && st.inputAccum >= 0.033f)
        {
            st.inputAccum = 0.0f;
            auto inp = BuildInput(scene);
            sendto(st.sock, (const char*)inp.data(), (int)inp.size(), 0, (sockaddr*)&st.serverAddr, sizeof(st.serverAddr));
        }
        st.pingAccum += dt;
        if (st.connected && st.pingAccum >= 1.0f)
        {
            st.pingAccum = 0.0f;
            uint32_t seq = st.pingSeq++;
            st.pingSentAt[seq] = st.now;
            auto ping = BuildPing(seq);
            sendto(st.sock, (const char*)ping.data(), (int)ping.size(), 0, (sockaddr*)&st.serverAddr, sizeof(st.serverAddr));
        }
    }
    mgr->RuntimeState = st.running ? (st.host ? "Hosting" : (st.connected ? "Connected" : "Connecting")) : "Stopped";
    mgr->Connected = st.connected;
    mgr->ConnectedPlayers = st.host ? std::max(1, (int)st.clients.size() + 1) : std::max(0, st.remotePlayerCount);
    mgr->EstimatedPingMs = st.pingMs;
    mgr->TimeSinceLastPacket = st.lastPacketAt > 0.0 ? (float)std::max(0.0, st.now - st.lastPacketAt) : 0.0f;
}

} // namespace tsu
