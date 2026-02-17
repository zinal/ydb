#include "ydb_service_coordination.h"
#include <ydb/public/sdk/cpp/include/ydb-cpp-sdk/client/coordination/coordination.h>
#include <ydb/public/lib/ydb_cli/common/normalize_path.h>

namespace NYdb {
namespace NConsoleClient {

using NCoordination::EConsistencyMode;
using NCoordination::ERateLimiterCountersMode;

static EConsistencyMode ParseConsistencyMode(const TString& v) {
    if (v == TString("STRICT") || v==TString("STRICT_MODE")) {
        return EConsistencyMode::STRICT_MODE;
    }
    if (v == TString("RELAXED") || v==TString("RELAXED_MODE")) {
        return EConsistencyMode::RELAXED_MODE;
    }
    if (v == TString("UNSET")) {
        return EConsistencyMode::UNSET;
    }
    throw yexception() << "Illegal consistency mode: " << v;
}

static ERateLimiterCountersMode ParseRateLimiterCountersMode(const TString& v) {
    if (v == TString("AGGREGATED")) {
        return ERateLimiterCountersMode::AGGREGATED;
    }
    if (v == TString("DETAILED")) {
        return ERateLimiterCountersMode::DETAILED;
    }
    if (v == TString("UNSET")) {
        return ERateLimiterCountersMode::UNSET;
    }
    throw yexception() << "Illegal rate limiter counters mode: " << v;
}

struct TCoordNodeSettings {
    TString SelfCheckPeriod;
    TString SessionGracePeriod;
    TString ReadConsistencyMode;
    TString AttachConsistencyMode;
    TString RateLimiterCountersMode;

    void Config(TYdbSimpleCommand::TConfig& config) {
        config.Opts->AddLongOption("self-check-period", "Self-check period. "
                                   "Supports time units (e.g., '5s', '1m'). Plain number interpreted as milliseconds.")
            .DefaultValue("").StoreResult(&SelfCheckPeriod);
        config.Opts->AddLongOption("session-grace-period", "Session grace period. "
                                   "Supports time units (e.g., '5s', '1m'). Plain number interpreted as milliseconds.")
            .DefaultValue("").StoreResult(&SessionGracePeriod);
        config.Opts->AddLongOption("read-consistency-mode", "Read consistency mode. "
                                   "STRICT, RELAXED or UNSET.")
            .DefaultValue("").StoreResult(&ReadConsistencyMode);
        config.Opts->AddLongOption("attach-consistency-mode", "Attach consistency mode. "
                                   "STRICT, RELAXED or UNSET.")
            .DefaultValue("").StoreResult(&AttachConsistencyMode);
        config.Opts->AddLongOption("rate-limiter-counters-mode", "Rate limiter counters mode. "
                                   "AGGREGATED, DETAILED or UNSET.")
            .DefaultValue("").StoreResult(&RateLimiterCountersMode);
    }

    template<typename TSettings>
    void Convert(TSettings& settings) {
        if (SelfCheckPeriod.empty()) {
            settings.SelfCheckPeriod_.reset();
        } else {
            settings.SelfCheckPeriod_ = ParseDurationMilliseconds(SelfCheckPeriod);
        }
        if (SessionGracePeriod.empty()) {
            settings.SessionGracePeriod_.reset();
        } else {
            settings.SessionGracePeriod_ = ParseDurationMilliseconds(SessionGracePeriod);
        }
        if (ReadConsistencyMode.empty()) {
            settings.ReadConsistencyMode_ = EConsistencyMode::UNSET;
        } else {
            settings.ReadConsistencyMode_ = ParseConsistencyMode(ReadConsistencyMode);
        }
        if (AttachConsistencyMode.empty()) {
            settings.AttachConsistencyMode_ = EConsistencyMode::UNSET;
        } else {
            settings.AttachConsistencyMode_ = ParseConsistencyMode(AttachConsistencyMode);
        }
        if (RateLimiterCountersMode.empty()) {
            settings.RateLimiterCountersMode_ = ERateLimiterCountersMode::UNSET;
        } else {
            settings.RateLimiterCountersMode_ = ParseRateLimiterCountersMode(RateLimiterCountersMode);
        }
    }
};


class TCommandCoordCreate : public TYdbSimpleCommand {
public:
    TCommandCoordCreate()
        : TYdbSimpleCommand("create", {}, "Create coordination node") {
    }

    void Config(TConfig& config) override {
        config.SetFreeArgsNum(1);
        SetFreeArgTitle(0, "<path>", "Path to create");
        ParsedSettings.Config(config);
    }

    void Parse(TConfig& config) override {
        TYdbSimpleCommand::Parse(config);

        NodeName = config.ParseResult->GetFreeArgs().at(0);
        AdjustPath(NodeName, config);

        ParsedSettings.Convert(Settings);
    }

    int Run(TConfig& config) override {
        NCoordination::TClient client(CreateDriver(config));
        auto result = client.CreateNode(NodeName, Settings).GetValueSync();
        NStatusHelpers::ThrowOnErrorOrPrintIssues(result);
        return EXIT_SUCCESS;
    }

private:
    TString NodeName;
    NCoordination::TCreateNodeSettings Settings;
    TCoordNodeSettings ParsedSettings;
};

class TCommandCoordAlter : public TYdbSimpleCommand {
public:
    TCommandCoordAlter()
        : TYdbSimpleCommand("alter", {}, "Alter coordination node") {
    }

    void Config(TConfig& config) override {
        config.SetFreeArgsNum(1);
        SetFreeArgTitle(0, "<path>", "Path to alter");
        ParsedSettings.Config(config);
    }

    void Parse(TConfig& config) override {
        TYdbSimpleCommand::Parse(config);

        NodeName = config.ParseResult->GetFreeArgs().at(0);
        AdjustPath(NodeName, config);

        ParsedSettings.Convert(Settings);
    }

    int Run(TConfig& config) override {
        NCoordination::TClient client(CreateDriver(config));
        auto result = client.AlterNode(NodeName, Settings).GetValueSync();
        NStatusHelpers::ThrowOnErrorOrPrintIssues(result);
        return EXIT_SUCCESS;
    }

private:
    TString NodeName;
    NCoordination::TAlterNodeSettings Settings;
    TCoordNodeSettings ParsedSettings;
};

class TCommandCoordDrop : public TYdbSimpleCommand {
public:
    TCommandCoordDrop()
        : TYdbSimpleCommand("drop", {}, "Drop coordination node") {
    }

    void Config(TConfig& config) override {
        config.SetFreeArgsNum(1);
        SetFreeArgTitle(0, "<path>", "Path to drop");
    }

    int Run(TConfig& config) override {
        NodeName = config.ParseResult->GetFreeArgs().at(0);
        AdjustPath(NodeName, config);
        NCoordination::TClient client(CreateDriver(config));
        auto result = client.DropNode(NodeName).GetValueSync();
        NStatusHelpers::ThrowOnErrorOrPrintIssues(result);
        return EXIT_SUCCESS;
    }

private:
    TString NodeName;
};

class TCommandCoordShow : public TYdbSimpleCommand {
public:
    TCommandCoordShow()
        : TYdbSimpleCommand("show", {}, "Display information about the coordination node") {
    }

    void Config(TConfig& config) override {
        config.SetFreeArgsNum(1);
        SetFreeArgTitle(0, "<path>", "Path to create");
    }

    int Run(TConfig& config) override {
        NodeName = config.ParseResult->GetFreeArgs().at(0);
        AdjustPath(NodeName, config);
        return EXIT_SUCCESS;
    }

private:
    TString NodeName;
};

class TCommandSemaCreate : public TYdbSimpleCommand {
public:
    TCommandSemaCreate()
        : TYdbSimpleCommand("create", {}, "Create semaphore") {
    }

    void Config(TConfig& config) override {
        config.SetFreeArgsNum(1);
        SetFreeArgTitle(0, "<name>", "Semaphore name to create");
    }

    int Run(TConfig& config) override {
        SemaName = config.ParseResult->GetFreeArgs().at(0);
        return EXIT_SUCCESS;
    }

private:
    TString NodeName;
    TString SemaName;
};

class TCommandSemaDrop : public TYdbSimpleCommand {
public:
    TCommandSemaDrop()
        : TYdbSimpleCommand("drop", {}, "Drop semaphore") {
    }

    void Config(TConfig& config) override {
        config.SetFreeArgsNum(1);
        SetFreeArgTitle(0, "<name>", "Semaphore name to drop");
    }

    int Run(TConfig& config) override {
        SemaName = config.ParseResult->GetFreeArgs().at(0);
        return EXIT_SUCCESS;
    }

private:
    TString NodeName;
    TString SemaName;
};

class TCommandSemaShow : public TYdbSimpleCommand {
public:
    TCommandSemaShow()
        : TYdbSimpleCommand("show", {}, "Display semaphore information") {
    }

    void Config(TConfig& config) override {
        config.SetFreeArgsNum(1);
        SetFreeArgTitle(0, "<name>", "Semaphore name to display");
    }

    int Run(TConfig& config) override {
        SemaName = config.ParseResult->GetFreeArgs().at(0);
        return EXIT_SUCCESS;
    }

private:
    TString NodeName;
    TString SemaName;
};

class TCommandCoordSema : public TClientCommandTree {
public:
    TCommandCoordSema()
        : TClientCommandTree("semaphore", {}, "Semaphore operations") {
        AddCommand(std::make_unique<TCommandSemaCreate>());
        AddCommand(std::make_unique<TCommandSemaDrop>());
        AddCommand(std::make_unique<TCommandSemaShow>());
    }
};

class TCommandCoordNode : public TClientCommandTree {
public:
    TCommandCoordNode()
        : TClientCommandTree("node", {}, "Coordination node operations") {
        AddCommand(std::make_unique<TCommandCoordCreate>());
        AddCommand(std::make_unique<TCommandCoordAlter>());
        AddCommand(std::make_unique<TCommandCoordDrop>());
        AddCommand(std::make_unique<TCommandCoordShow>());
    }
};

TCommandCoordination::TCommandCoordination()
    : TClientCommandTree("coordination", {}, "Coordination service operations")
{
    AddCommand(std::make_unique<TCommandCoordSema>());
    AddCommand(std::make_unique<TCommandCoordNode>());
}

}
}
