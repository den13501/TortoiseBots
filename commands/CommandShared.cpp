#include "CommandShared.h"

#include "../runtime/BotManager.h"
#include "BotCommandContext.h"

// pi-lens-ignore: clang:pp_file_not_found
#include "Chat.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "ObjectAccessor.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "ObjectMgr.h"
// pi-lens-ignore: clang:pp_file_not_found
#include "WorldSession.h"

#include <mutex>
#include <string>
#include <unordered_map>

// pi-lens-ignore: clang:pp_file_not_found
#include "playerbot/PlayerbotAIConfig.h"

namespace TortoiseBots {
namespace BotCommands {

std::string Trim(std::string value)
{
    size_t first = value.find_first_not_of(" \t");
    if (first == std::string::npos)
        return {};
    size_t last = value.find_last_not_of(" \t");
    return value.substr(first, last - first + 1);
}

Player* Requester(ChatHandler* handler)
{
    return handler && handler->GetSession() ? handler->GetSession()->GetPlayer() : nullptr;
}

TortoiseBots::RemoteCommander::FeatureGates CurrentCommanderGates()
{
    TortoiseBots::RemoteCommander::FeatureGates gates;
    gates.remoteCommander = sPlayerbotAIConfig.remoteCommander;
    gates.dungeonSelfClear = sPlayerbotAIConfig.dungeonSelfClear;
    gates.botLfgGroups = sPlayerbotAIConfig.botLfgGroups;
    return gates;
}

bool ResolveOwnedBot(ChatHandler* handler, char const* args, Player*& bot, BotRecord*& record, std::string& name)
{
    bot = nullptr;
    record = nullptr;
    name = Trim(args ? args : "");
    if (name.empty() || !normalizePlayerName(name))
        return false;

    bot = sObjectMgr.GetPlayer(name.c_str());
    if (!bot)
        bot = sObjectAccessor.FindPlayerByName(name.c_str());
    if (!bot)
        return false;

    record = BotManager::Instance().FindBot(bot->GetObjectGuid());
    if (!CanControlBot(Requester(handler), record))
        return false;

    return IsLiveHeadlessBot(bot, record);
}

std::string ProtocolSafe(std::string value)
{
    for (char& character : value)
    {
        if (character == '|' || character == '\r' || character == '\n')
            character = ' ';
    }
    return value;
}

std::string LocalizedAreaName(ChatHandler* handler, uint32_t areaId, std::string name)
{
    if (areaId && handler && handler->GetSession())
        sObjectMgr.GetAreaLocaleString(areaId, handler->GetSession()->GetSessionDbLocaleIndex(), &name);
    return name;
}

// The loot log stores only the English source name (bot_events.csv), so the
// translation goes through a name -> entry index built once from the templates.
// Same-named entries keep the first one that has a translation.
std::string LocalizedLootSourceName(int locIdx, std::string const& sourceType, std::string const& name)
{
    if (locIdx < 0 || name.empty())
        return name;
    static std::once_flag built;
    static std::unordered_map<std::string, uint32> creatures;
    static std::unordered_map<std::string, uint32> objects;
    std::call_once(built, []()
    {
        for (auto const& entry : sObjectMgr.GetCreatureInfoMap())
        {
            CreatureLocale const* loc = entry.second ? sObjectMgr.GetCreatureLocale(entry.first) : nullptr;
            bool translated = false;
            if (loc)
                for (std::string const& n : loc->Name)
                    translated = translated || !n.empty();
            if (translated)
                creatures.emplace(entry.second->name, entry.first);
        }
        for (auto const& entry : sObjectMgr.GetGameObjectInfoMap())
        {
            GameObjectLocale const* loc = sObjectMgr.GetGameObjectLocale(entry.first);
            bool translated = false;
            if (loc)
                for (std::string const& n : loc->Name)
                    translated = translated || !n.empty();
            if (translated)
                objects.emplace(entry.second.name, entry.first);
        }
    });
    if (sourceType == "corpse")
    {
        auto it = creatures.find(name);
        if (it != creatures.end())
            if (CreatureLocale const* loc = sObjectMgr.GetCreatureLocale(it->second))
                if (loc->Name.size() > size_t(locIdx) && !loc->Name[locIdx].empty())
                    return loc->Name[locIdx];
        return name;
    }
    auto it = objects.find(name);
    if (it != objects.end())
        if (GameObjectLocale const* loc = sObjectMgr.GetGameObjectLocale(it->second))
            if (loc->Name.size() > size_t(locIdx) && !loc->Name[locIdx].empty())
                return loc->Name[locIdx];
    return name;
}

void SendActionError(ChatHandler* handler, std::string const& intent,
    char const* code, char const* message)
{
    if (!handler)
        return;
    std::string safeMessage = ProtocolSafe(message ? message : "request rejected");
    handler->PSendSysMessage("TBM:ACTION_ERR|%s|%s|%s", ProtocolSafe(intent).c_str(),
        code ? code : "rejected", safeMessage.c_str());
    NoteCommanderOrderResult(handler, intent, false,
        std::string(code ? code : "rejected") + ": " + safeMessage);
}

void SendActionAck(ChatHandler* handler, std::string const& intent,
    std::string const& scope, uint32 count, std::string const& executor)
{
    if (!handler)
        return;
    std::string safeScope = ProtocolSafe(scope);
    std::string safeExecutor = executor.empty() ? "-" : ProtocolSafe(executor);
    handler->PSendSysMessage("TBM:ACTION_ACK|%s|%s|%u|%s",
        ProtocolSafe(intent).c_str(), safeScope.c_str(), count, safeExecutor.c_str());
    NoteCommanderOrderResult(handler, intent, true, safeScope + " " + std::to_string(count));
}

} // namespace BotCommands
} // namespace TortoiseBots
