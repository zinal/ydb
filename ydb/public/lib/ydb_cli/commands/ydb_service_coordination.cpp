#include "ydb_service_coordination.h"
#include <ydb/public/sdk/cpp/include/ydb-cpp-sdk/client/coordination/coordination.h>
#include <ydb/public/lib/ydb_cli/common/normalize_path.h>

namespace NYdb {
namespace NConsoleClient {

TCommandCoordination::TCommandCoordination()
    : TClientCommandTree("coordination", {}, "Coordination service operations")
{
    AddCommand(std::make_unique<TCommandCoordCreate>());
    AddCommand(std::make_unique<TCommandCoordDrop>());
}

TCommandCoordCreate::TCommandCoordCreate()
    : TYdbSimpleCommand("create", {}, "Create coordination node")
{}

void TCommandCoordCreate::Config(TConfig& config) {
    TYdbSimpleCommand::Config(config);
    config.Opts->AddLongOption('n', "node-name", "Coordination node name")
        .Required().RequiredArgument("COORD-NODE").StoreResult(&NodeName);
    config.SetFreeArgsNum(0);
}

int TCommandCoordCreate::Run(TConfig& config) {
    NCoordination::TClient client(CreateDriver(config));
    AdjustPath(NodeName, config);
    auto result = client.CreateNode(
        NodeName
    ).GetValueSync();
    NStatusHelpers::ThrowOnErrorOrPrintIssues(result);
    return EXIT_SUCCESS;
}

TCommandCoordDrop::TCommandCoordDrop()
    : TYdbSimpleCommand("drop", {}, "Drop coordination node")
{}

void TCommandCoordDrop::Config(TConfig& config) {
    TYdbSimpleCommand::Config(config);
    config.Opts->AddLongOption('n', "node-name", "Coordination node name")
        .Required().RequiredArgument("COORD-NODE").StoreResult(&NodeName);
    config.SetFreeArgsNum(0);
}

int TCommandCoordDrop::Run(TConfig& config) {
    NCoordination::TClient client(CreateDriver(config));
    AdjustPath(NodeName, config);
    auto result = client.DropNode(
        NodeName
    ).GetValueSync();
    NStatusHelpers::ThrowOnErrorOrPrintIssues(result);
    return EXIT_SUCCESS;
}

}
}
