#include "ydb_service_coordination.h"
#include <ydb/public/sdk/cpp/include/ydb-cpp-sdk/client/coordination/coordination.h>
#include <ydb/public/lib/ydb_cli/common/normalize_path.h>

namespace NYdb {
namespace NConsoleClient {

class TCommandCoordCreate : public TYdbSimpleCommand {
public:
    TCommandCoordCreate()
        : TYdbSimpleCommand("create", {}, "Create coordination node") {
    }

    void Config(TConfig& config) override {
        config.SetFreeArgsNum(1);
        SetFreeArgTitle(0, "<path>", "Path to create");
    }

    int Run(TConfig& config) override {
        NodeName = config.ParseResult->GetFreeArgs().at(0);
        AdjustPath(NodeName, config);
        NCoordination::TClient client(CreateDriver(config));
        auto result = client.CreateNode(NodeName).GetValueSync();
        NStatusHelpers::ThrowOnErrorOrPrintIssues(result);
        return EXIT_SUCCESS;
    }

private:
    TString NodeName;
};

class TCommandCoordAlter : public TYdbSimpleCommand {
public:
    TCommandCoordAlter()
        : TYdbSimpleCommand("alter", {}, "Alter coordination node") {
    }

    void Config(TConfig& config) override {
        config.SetFreeArgsNum(1);
        SetFreeArgTitle(0, "<path>", "Path to alter");
    }

    int Run(TConfig& config) override {
        NodeName = config.ParseResult->GetFreeArgs().at(0);
        AdjustPath(NodeName, config);
        return EXIT_SUCCESS;
    }

private:
    TString NodeName;
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
