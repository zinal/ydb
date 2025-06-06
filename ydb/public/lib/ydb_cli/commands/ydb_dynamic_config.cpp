#include "ydb_dynamic_config.h"

#include <ydb/public/sdk/cpp/include/ydb-cpp-sdk/client/draft/ydb_dynamic_config.h>
#include <ydb/public/sdk/cpp/include/ydb-cpp-sdk/client/config/config.h>
#include <ydb/library/yaml_config/public/yaml_config.h>
#include <library/cpp/json/json_value.h>
#include <library/cpp/json/json_writer.h>

#include <openssl/sha.h>

#include <util/folder/path.h>
#include <util/string/hex.h>
#include <algorithm>

using namespace NKikimr;

namespace NYdb::NConsoleClient::NDynamicConfig {

TString WrapYaml(const TString& yaml) {
    auto doc = NFyaml::TDocument::Parse(yaml);

    TStringStream out;
    out << (doc.HasExplicitDocumentStart() ? "" : "---\n")
        << doc << (yaml[yaml.size() - 1] != '\n' ? "\n" : "");

    return out.Str();
}

TString WrapStaticConfig(const TString& yaml) {
    auto newDoc = NFyaml::TDocument::Parse("---\nmetadata: {}\nconfig: {}\n");
    auto inputDoc = NFyaml::TDocument::Parse(yaml);

    auto configNode = inputDoc.Root().Copy(newDoc);
    newDoc.Root().Map().pair_at("config").SetValue(configNode.Ref());

    auto metadataNode = newDoc.Root().Map().pair_at("metadata").Value().Map();
    metadataNode.Append(newDoc.Buildf("kind"), newDoc.Buildf("MainConfig"));
    metadataNode.Append(newDoc.Buildf("cluster"), newDoc.Buildf("\"\""));
    metadataNode.Append(newDoc.Buildf("version"), newDoc.Buildf("0"));

    return TString(newDoc.EmitToCharArray().get());
}

TCommandConfig::TCommandConfig(
        bool useLegacyApi,
        TCommandFlagsOverrides commandFlagsOverrides,
        bool allowEmptyDatabase)
    : TClientCommandTree("config", {}, "Dynamic config")
    , CommandFlagsOverrides(commandFlagsOverrides)
{
    AddCommand(std::make_unique<TCommandConfigFetch>(useLegacyApi, allowEmptyDatabase));
    AddCommand(std::make_unique<TCommandConfigReplace>(useLegacyApi, allowEmptyDatabase));
    AddCommand(std::make_unique<TCommandConfigResolve>());
    AddCommand(std::make_unique<TCommandGenerateDynamicConfig>(allowEmptyDatabase));
    AddCommand(std::make_unique<TCommandVersionDynamicConfig>(allowEmptyDatabase));
}

TCommandConfig::TCommandConfig(
    bool useLegacyApi,
    bool allowEmptyDatabase)
    : TCommandConfig(useLegacyApi, TCommandFlagsOverrides{}, allowEmptyDatabase)
{}

void TCommandConfig::PropagateFlags(const TCommandFlags& flags) {
    TClientCommand::PropagateFlags(flags);

    if (CommandFlagsOverrides.OnlyExplicitProfile) {
        OnlyExplicitProfile = *CommandFlagsOverrides.OnlyExplicitProfile;
    }

    if (CommandFlagsOverrides.Dangerous) {
        Dangerous = *CommandFlagsOverrides.Dangerous;
    }

    for (auto& [_, cmd] : SubCommands) {
        cmd->PropagateFlags(TCommandFlags{.Dangerous = Dangerous, .OnlyExplicitProfile = OnlyExplicitProfile});
    }
}

TCommandConfigFetch::TCommandConfigFetch(
        bool useLegacyApi,
        bool allowEmptyDatabase)
    : TYdbReadOnlyCommand("fetch", {"get", "dump"}, "Fetch main dynamic-config")
    , UseLegacyApi(useLegacyApi)
    , AllowEmptyDatabase(allowEmptyDatabase)
{
}

void TCommandConfigFetch::Config(TConfig& config) {
    TYdbCommand::Config(config);
    config.Opts->AddLongOption("output-directory", "Directory to save config(s)")
        .RequiredArgument("[directory]").StoreResult(&OutDir);
    config.Opts->AddLongOption("strip-metadata", "Strip metadata from config")
        .StoreTrue(&StripMetadata);
    config.Opts->AddLongOption("dedicated-storage-section", "Fetch dedicated storage section")
        .StoreTrue(&DedicatedStorageSection);
    config.Opts->AddLongOption("dedicated-cluster-section", "Fetch dedicated cluster section")
        .StoreTrue(&DedicatedClusterSection);
    config.Opts->AddLongOption("v2-internal-state", "Fetch all managed internal sections in v1 format (in order to downgrade)")
        .StoreTrue(&FetchInternalState);
    config.Opts->AddLongOption("v2-explicit-sections", "Add explicit sections to control managed entities manually")
        .StoreTrue(&FetchExplicitSections);
    config.SetFreeArgsNum(0);

    config.AllowEmptyDatabase = AllowEmptyDatabase;
    config.Opts->MutuallyExclusive("output-directory", "strip-metadata");
}

void TCommandConfigFetch::Parse(TConfig& config) {
    TClientCommand::Parse(config);

    if (FetchInternalState && FetchExplicitSections) {
        ythrow yexception() << "Can't specify both --v2-internal-state and --v2-explicit-sections at the same time.";
    } else if (UseLegacyApi && (FetchInternalState || FetchExplicitSections)) {
        ythrow yexception() << "--v2-internal-state and --v2-explicit-sections can't be used with legacy API.";
    }
}

int TCommandConfigFetch::Run(TConfig& config) {
    if (AllowEmptyDatabase) {
        // explicitly clear database to get cluster database
        // in `ydb admin cluster config fetch` even if
        // some database is set by mistake
        config.Database.clear();
    }
    auto driver = std::make_unique<NYdb::TDriver>(CreateDriver(config));
    auto client = NYdb::NConfig::TConfigClient(*driver);

    NYdb::NConfig::TFetchConfigResult result(TStatus(EStatus::CLIENT_CALL_UNIMPLEMENTED, {}), {});

    if (!UseLegacyApi) {
        NYdb::NConfig::TFetchAllConfigsSettings settings;
        if (FetchInternalState) {
            settings.Transform(NConfig::EFetchAllConfigsTransform::ADD_BLOB_STORAGE_AND_DOMAINS_CONFIG);
        } else if (FetchExplicitSections) {
            settings.Transform(NConfig::EFetchAllConfigsTransform::ADD_EXPLICIT_SECTIONS);
        }
        result = client.FetchAllConfigs(settings).GetValueSync();
    }

    // if the new Config API is not supported, fallback to the old DynamicConfig API
    if (result.GetStatus() == EStatus::CLIENT_CALL_UNIMPLEMENTED || result.GetStatus() == EStatus::UNSUPPORTED) {
        Cerr << "Warning: Fallback to DynamicConfig API" << Endl;

        if (FetchInternalState || FetchExplicitSections) {
            Cerr << "Error: --v2-internal-state and --v2-explicit-sections can't be used with legacy API." << Endl;
            return EXIT_FAILURE;
        }

        auto client = NYdb::NDynamicConfig::TDynamicConfigClient(*driver);
        auto result = client.GetConfig().GetValueSync();
        NStatusHelpers::ThrowOnErrorOrPrintIssues(result);

        auto cfg = TString{result.GetConfig()};

        if (cfg) {
            auto metadata = NYamlConfig::GetMainMetadata(cfg);

            if (StripMetadata) {
                cfg = NYamlConfig::StripMetadata(cfg);
            }
        } else {
            Cerr << "YAML config is absent on this cluster." << Endl;
            return EXIT_FAILURE;
        }

        if (!OutDir) {
            Cout << cfg << Endl;
        } else {
            TFsPath dir(OutDir);
            dir.MkDirs();
            auto filepath = (dir / "dynconfig.yaml");
            TFileOutput out(filepath);
            out << cfg;
        }
        return EXIT_SUCCESS;
    }

    // if the new Config API is supported, continue with the new API
    NStatusHelpers::ThrowOnErrorOrPrintIssues(result);
    TString clusterConfig;
    TString storageConfig;

    for (const auto& entry : result.GetConfigs()) {
        std::visit([&](auto&& arg) {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, NYdb::NConfig::TMainConfigIdentity>) {
                if (DedicatedClusterSection || !DedicatedStorageSection) {
                    clusterConfig = entry.Config;
                }
            } else if constexpr (std::is_same_v<T, NYdb::NConfig::TStorageConfigIdentity>) {
                if (DedicatedStorageSection || !DedicatedClusterSection) {
                    storageConfig = entry.Config;
                }
            }
        }, entry.Identity);
    }


    // TODO: rewrite it in proper way, without stdout and stderr confusion
    if (!clusterConfig.empty()) {
        try {
            clusterConfig = NYamlConfig::UpgradeMainConfigVersion(clusterConfig);
        } catch(...) {
            // it is better to return at least something
            // because for user it is the only way to get config
            // user will be unable to reupload this config without manual interaction
            // and will get attention that something went horribly wrong
            Cerr << "Unable to bump main config version, returning as-is" << Endl;
        }
        if (!storageConfig.empty() || DedicatedStorageSection) {
            Cerr << "cluster config: " << Endl;
        }
        Cout << clusterConfig << Endl;
    }

    if (!storageConfig.empty()) {
        try {
            storageConfig = NYamlConfig::UpgradeStorageConfigVersion(storageConfig);
        } catch(...) {
            // it is better to return at least something
            // because for user it is the only way to get config
            // user will be unable to reupload this config without manual interaction
            // and will get attention that something went horribly wrong
            Cerr << "Unable to bump storage config version, returning as-is" << Endl;
        }
        if (!clusterConfig.empty() || DedicatedClusterSection) {
            Cerr << "storage config:" << Endl;
        }
        Cout << storageConfig << Endl;
    }

    if (clusterConfig.empty() && storageConfig.empty()) {
        Cerr << "No config returned." << Endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

TCommandConfigReplace::TCommandConfigReplace(
        bool useLegacyApi,
        bool allowEmptyDatabase)
    : TYdbCommand("replace", {}, "Replace dynamic config")
    , UseLegacyApi(useLegacyApi)
    , IgnoreCheck(false)
    , AllowEmptyDatabase(allowEmptyDatabase)
{
}

void TCommandConfigReplace::Config(TConfig& config) {
    TYdbCommand::Config(config);
    config.Opts->AddLongOption('f', "filename", "Filename of the file containing configuration")
        .Required().RequiredArgument("[config.yaml]").StoreResult(&Filename);
    config.Opts->AddLongOption("ignore-local-validation", "Ignore local config applicability checks")
        .StoreTrue(&IgnoreCheck);
    config.Opts->AddLongOption("dry-run", "Check config applicability")
        .StoreTrue(&DryRun);
    config.Opts->AddLongOption("allow-unknown-fields", "Allow fields not present in config")
        .StoreTrue(&AllowUnknownFields);
    config.Opts->AddLongOption("force", "Ignore metadata on config replacement")
        .StoreTrue(&Force);
    config.AllowEmptyDatabase = AllowEmptyDatabase;
    config.SetFreeArgsNum(0);
}

void TCommandConfigReplace::Parse(TConfig& config) {
    TClientCommand::Parse(config);

    if (Filename == "") {
        ythrow yexception() << "Must specify non-empty -f (--filename)";
    }

    const auto configStr = Filename == "-" ? Cin.ReadAll() : TFileInput(Filename).ReadAll();

    DynamicConfig = configStr;

    if (!IgnoreCheck) {
        NYamlConfig::GetMainMetadata(configStr);
        auto tree = NFyaml::TDocument::Parse(configStr);
        const auto resolved = NYamlConfig::ResolveAll(tree);
        Y_UNUSED(resolved); // we can't check it better without ydbd
    }
}

int TCommandConfigReplace::Run(TConfig& config) {
    std::unique_ptr<NYdb::TDriver> driver = std::make_unique<NYdb::TDriver>(CreateDriver(config));
    auto client = NYdb::NConfig::TConfigClient(*driver);

    NYdb::NConfig::TReplaceConfigSettings settings;

    if (Force) {
        settings.BypassChecks();
    }

    if (DryRun) {
        settings.DryRun();
    }

    if (AllowUnknownFields) {
        settings.AllowUnknownFields();
    }

    auto status = TStatus(EStatus::CLIENT_CALL_UNIMPLEMENTED, {});

    if (!UseLegacyApi) {
        status = client.ReplaceConfig(DynamicConfig, settings).GetValueSync();
    }

    if (status.GetStatus() == EStatus::CLIENT_CALL_UNIMPLEMENTED || status.GetStatus() == EStatus::UNSUPPORTED) {
        Cerr << "Warning: Fallback to DynamicConfig API" << Endl;

        auto client = NYdb::NDynamicConfig::TDynamicConfigClient(*driver);

        status = [&]() {
            if (Force) {
                return client.SetConfig(
                    DynamicConfig,
                    DryRun,
                    AllowUnknownFields).GetValueSync();
            }

            return client.ReplaceConfig(
                DynamicConfig,
                DryRun,
                AllowUnknownFields).GetValueSync();
        }();
    }

    NStatusHelpers::ThrowOnErrorOrPrintIssues(status);

    if (!status.GetIssues()) {
        Cout << status << Endl;
    }

    return EXIT_SUCCESS;
}

TCommandConfigResolve::TCommandConfigResolve()
    : TYdbReadOnlyCommand("resolve", {}, "Resolve config")
{
}

void TCommandConfigResolve::Config(TConfig& config) {
    TYdbCommand::Config(config);
    config.Opts->AddLongOption("all", "Resolve for all combinations")
        .StoreTrue(&All);
    config.Opts->AddLongOption("label", "Labels for this node")
        .Optional().RequiredArgument("[LABEL=VALUE]")
        .GetOpt().KVHandler([this](TString key, TString val) {
            Labels[key] = val;
        });
    config.Opts->AddLongOption('f', "filename", "Filename of the file containing configuration to resolve")
        .Optional().RequiredArgument("[config.yaml]").StoreResult(&Filename);
    config.Opts->AddLongOption("directory", "Directory with config(s)")
        .Optional().RequiredArgument("[directory]").StoreResult(&Dir);
    config.Opts->AddLongOption("output-directory", "Directory to save config(s)")
        .Optional().RequiredArgument("[directory]").StoreResult(&OutDir);
    config.Opts->AddLongOption("from-cluster", "Fetch current config from cluster instead of the local file")
        .StoreTrue(&FromCluster);
    config.Opts->AddLongOption("remote-resolve", "Use resolver on cluster instead of built-in resolver")
        .StoreTrue(&RemoteResolve);
    config.Opts->AddLongOption("node-id", "Take labels from node with the specified id")
        .Optional().RequiredArgument("[node]").StoreResult(&NodeId);
    config.Opts->AddLongOption("skip-volatile", "Ignore volatile configs")
        .StoreTrue(&SkipVolatile);
    config.SetFreeArgsNum(0);
}

void TCommandConfigResolve::Parse(TConfig& config) {
    TClientCommand::Parse(config);

    if ((ui32(!Filename.empty()) + ui32(!Dir.empty()) + ui32(FromCluster)) != 1) {
        ythrow yexception() << "Must specify one of -f (--filename), --directory and --from-cluster";
    }

    if (All && (!Labels.empty() || NodeId)) {
        ythrow yexception() << "Should specify either --all or --label and --node-id";
    }
}

TString CalcHash(const TString& ser) {
    SHA_CTX ctx;
    SHA1_Init(&ctx);
    SHA1_Update(&ctx, ser.data(), ser.size());
    unsigned char sha1[SHA_DIGEST_LENGTH];
    SHA1_Final(sha1, &ctx);

    TString hex = HexEncode(TString(reinterpret_cast<const char*>(sha1), SHA_DIGEST_LENGTH));
    hex.to_lower();
    return hex;
}

TString LabelSetHash(const TSet<NYamlConfig::TNamedLabel>& labels) {
    TStringStream labelsStream;
    labelsStream << "[";
    for (const auto& label : labels) {
        labelsStream << "(" << label.Name << ":" << (label.Inv ? "-" : label.Value.Quote()) << "),";
    }
    labelsStream << "]";
    TString ser = labelsStream.Str();
    return CalcHash(ser);
}

TString ConfigHash(const NFyaml::TNodeRef& config) {
    TStringStream configStream;
    configStream << config;
    TString ser = configStream.Str();
    return "0_" + CalcHash(ser);
}

int TCommandConfigResolve::Run(TConfig& config) {
    auto driver = std::make_unique<NYdb::TDriver>(CreateDriver(config));
    auto client = NYdb::NDynamicConfig::TDynamicConfigClient(*driver);

    if (NodeId) {
        auto result = client.GetNodeLabels(NodeId).GetValueSync();
        NStatusHelpers::ThrowOnErrorOrPrintIssues(result);

        // TODO: maybe we should merge labels instead
        Labels = result.GetLabels();
    }

    TString configStr;
    std::map<uint64_t, std::string> volatileConfigStrs;

    if (!Filename.empty()) {
        configStr = TFileInput(Filename).ReadAll();
        // TODO: support multiple files (e.g. volatile and non-volatile)
    }

    if (!Dir.empty()) {
        auto dir = TFsPath(Dir);
        configStr = TFileInput(dir / "dynconfig.yaml").ReadAll();
        TVector<TFsPath> entries;
        dir.List(entries);
        for (auto& entry : entries) {
            if (entry.IsFile() && entry.GetName().StartsWith("volatile_") && entry.GetName().EndsWith(".yaml")) {
                auto volatileConfigStr = TFileInput(entry).ReadAll();
                auto metadata = NYamlConfig::GetVolatileMetadata(volatileConfigStr);
                volatileConfigStrs[metadata.Id.value()] = volatileConfigStr;
            }
        }
        // TODO: use kind to distinguish between config types
        // TODO: throw error on version mismatch
    }

    if (FromCluster) {
        auto result = client.GetConfig().GetValueSync();
        NStatusHelpers::ThrowOnErrorOrPrintIssues(result);

        configStr = result.GetConfig();
        volatileConfigStrs = result.GetVolatileConfigs();
    }

    auto tree = NFyaml::TDocument::Parse(configStr);

    if (!SkipVolatile) {
        for (auto& [_, cfgStr]: volatileConfigStrs) {
            auto volatileCfg = NFyaml::TDocument::Parse(TString{cfgStr});
            auto selectors = volatileCfg.Root().Map().at("selector_config");
            NYamlConfig::AppendVolatileConfigs(tree, selectors);
        }
    }

    TVector<NFyaml::TDocument> configs;

    if (!RemoteResolve) {
        if (All) {
            auto resolved = NYamlConfig::ResolveAll(tree);
            for (auto& [labelSets, config] : resolved.Configs) {
                auto doc = NFyaml::TDocument::Parse("---\nlabel_sets: []\nconfig: {}\n");

                auto node = config.second.Copy(doc);
                doc.Root().Map().at("config").Insert(node.Ref());
                auto labelSetsSeq = doc.Root().Map().at("label_sets").Sequence();

                for (auto& labelSet : labelSets) {
                    auto map = doc.Buildf("{}");
                    for (size_t i = 0; i < labelSet.size(); ++i) {
                        auto& label = labelSet[i];
                        NFyaml::TNodeRef node;
                        switch (label.Type) {
                        case NYamlConfig::TLabel::EType::Common:
                            node = doc.Buildf("%s: {type: COMMON, value: %s}", resolved.Labels[i].c_str(), label.Value.c_str());
                            break;
                        case NYamlConfig::TLabel::EType::Negative:
                            node = doc.Buildf("%s: {type: NOT_SET}", resolved.Labels[i].c_str());
                            break;
                        case NYamlConfig::TLabel::EType::Empty:
                            node = doc.Buildf("%s: {type: EMPTY}", resolved.Labels[i].c_str());
                            break;
                        default:
                            Y_ABORT("unknown label type");
                        }
                        map.Insert(node);
                    }
                    labelSetsSeq.Append(map);
                }

                configs.push_back(std::move(doc));
            }
        } else {
            auto doc = NFyaml::TDocument::Parse("---\nlabel_sets: []\nconfig: {}\n");

            auto labelSetsSeq = doc.Root().Map().at("label_sets").Sequence();
            auto map = doc.Buildf("{}");
            TSet<NYamlConfig::TNamedLabel> namedLabels;
            for (auto& [name, value] : Labels) {
                namedLabels.insert(NYamlConfig::TNamedLabel{TString{name}, TString{value}});
                auto node = doc.Buildf("%s: {type: COMMON, value: %s}", name.c_str(), value.c_str());
                map.Insert(node);
            }
            labelSetsSeq.Append(map);
            auto resolved = NYamlConfig::Resolve(tree, namedLabels);

            auto node = resolved.second.Copy(doc);
            doc.Root().Map().at("config").Insert(node.Ref());

            configs.push_back(std::move(doc));
        }
    } else {
        if (All) {
            auto result = client.VerboseResolveConfig(configStr, volatileConfigStrs).GetValueSync();
            NStatusHelpers::ThrowOnErrorOrPrintIssues(result);

            TVector<TString> labels(result.GetLabels().begin(), result.GetLabels().end());

            for (const auto& [labelSets, configStr] : result.GetConfigs()) {
                auto doc = NFyaml::TDocument::Parse("---\nlabel_sets: []\nconfig: {}\n");
                auto config = NFyaml::TDocument::Parse(TString{configStr});

                auto node = config.Root().Copy(doc);
                doc.Root().Map().at("config").Insert(node.Ref());
                auto labelSetsSeq = doc.Root().Map().at("label_sets").Sequence();

                for (auto& labelSet : labelSets) {
                    auto map = doc.Buildf("{}");
                    for (size_t i = 0; i < labelSet.size(); ++i) {
                        auto& label = labelSet[i];
                        NFyaml::TNodeRef node;
                        switch (label.Type) {
                        case NYdb::NDynamicConfig::TVerboseResolveConfigResult::TLabel::EType::Common:
                            node = doc.Buildf("%s: {type: COMMON, value: %s}", labels[i].c_str(), label.Value.c_str());
                            break;
                        case NYdb::NDynamicConfig::TVerboseResolveConfigResult::TLabel::EType::Negative:
                            node = doc.Buildf("%s: {type: NOT_SET}", labels[i].c_str());
                            break;
                        case NYdb::NDynamicConfig::TVerboseResolveConfigResult::TLabel::EType::Empty:
                            node = doc.Buildf("%s: {type: EMPTY}", labels[i].c_str());
                            break;
                        default:
                            Y_ABORT("unknown label type");
                        }
                        map.Insert(node);
                    }
                    labelSetsSeq.Append(map);
                }

                configs.push_back(std::move(doc));
            }
        } else {
            const auto result = client.ResolveConfig(configStr, volatileConfigStrs, Labels).GetValueSync();
            NStatusHelpers::ThrowOnErrorOrPrintIssues(result);

            auto doc = NFyaml::TDocument::Parse("---\nlabel_sets: []\nconfig: {}\n");

            auto labelSetsSeq = doc.Root().Map().at("label_sets").Sequence();
            auto map = doc.Buildf("{}");
            TSet<NYamlConfig::TNamedLabel> namedLabels;
            for (auto& [name, value] : Labels) {
                namedLabels.insert(NYamlConfig::TNamedLabel{TString{name}, TString{value}});
                auto node = doc.Buildf("%s: {type: COMMON, value: %s}", name.c_str(), value.c_str());
                map.Insert(node);
            }
            labelSetsSeq.Append(map);

            auto config = NFyaml::TDocument::Parse(TString{result.GetConfig()});
            auto node = config.Root().Copy(doc);
            doc.Root().Map().at("config").Insert(node.Ref());

            configs.push_back(std::move(doc));
        }
    }

    if (!OutDir) {
        for (auto& doc : configs) {
            Cout << doc;
        }
    } else {
        TFsPath dir(OutDir);
        dir.MkDirs();

        for (auto& doc : configs) {
            auto node = doc.Root().Map().at("config");
            auto filename = TString("config_") + ConfigHash(node) + ".yaml";
            auto filepath = dir / filename;
            TFileOutput out(filepath);
            out << doc;
        }
    }

    return EXIT_SUCCESS;
}

TCommandVolatileConfig::TCommandVolatileConfig()
    : TClientCommandTree("volatile-config", {}, "Volatile dynamic configs")
{
    AddCommand(std::make_unique<TCommandConfigVolatileAdd>());
    AddCommand(std::make_unique<TCommandConfigVolatileDrop>());
    AddCommand(std::make_unique<TCommandConfigVolatileFetch>());
}

TCommandConfigVolatileAdd::TCommandConfigVolatileAdd()
    : TYdbCommand("add", {}, "Add volatile dynamic config")
{
}

void TCommandConfigVolatileAdd::Config(TConfig& config) {
    TYdbCommand::Config(config);
    config.Opts->AddLongOption('f', "filename", "filename to set")
        .Required().RequiredArgument("[config.yaml]").StoreResult(&Filename);
    config.Opts->AddLongOption("ignore-local-validation", "Ignore local config applicability checks")
        .StoreTrue(&IgnoreCheck);
    config.Opts->AddLongOption("dry-run", "Check config applicability")
        .StoreTrue(&DryRun);
    config.SetFreeArgsNum(0);

}

void TCommandConfigVolatileAdd::Parse(TConfig& config) {
    TClientCommand::Parse(config);

    if (Filename.empty()) {
        ythrow yexception() << "Must non-empty specify -f (--filename)";
    }
}

int TCommandConfigVolatileAdd::Run(TConfig& config) {
    auto driver = std::make_unique<NYdb::TDriver>(CreateDriver(config));
    auto client = NYdb::NDynamicConfig::TDynamicConfigClient(*driver);

    const auto configStr = Filename == "-" ? Cin.ReadAll() : TFileInput(Filename).ReadAll();

    if (!IgnoreCheck) {
        auto result = client.GetConfig().GetValueSync();
        NStatusHelpers::ThrowOnErrorOrPrintIssues(result);

        if (result.GetConfig().empty()) {
            ythrow yexception() << "Config on server is empty";
        }

        NYamlConfig::GetVolatileMetadata(configStr);

        auto tree = NFyaml::TDocument::Parse(TString{result.GetConfig()});

        auto volatileCfg = NFyaml::TDocument::Parse(configStr);
        auto selectors = volatileCfg.Root().Map().at("selector_config");
        NYamlConfig::AppendVolatileConfigs(tree, selectors);

        auto resolved = NYamlConfig::ResolveAll(tree);

        Y_UNUSED(resolved); // we can't check it better without ydbd
    }

    auto status = client.AddVolatileConfig(configStr).GetValueSync();
    NStatusHelpers::ThrowOnErrorOrPrintIssues(status);

    Cout << status << Endl;

    return EXIT_SUCCESS;
}

TCommandConfigVolatileDrop::TCommandConfigVolatileDrop()
    : TYdbCommand("drop", {}, "Remove volatile dynamic configs")
{
}

void TCommandConfigVolatileDrop::Config(TConfig& config) {
    TYdbCommand::Config(config);
    config.Opts->AddLongOption("id", "Id of config to remove")
        .Optional().RequiredArgument("[ui64]")
        .InsertTo(&Ids);
    config.Opts->AddLongOption("all", "Remove all volatile configs")
        .StoreTrue(&All);
    config.Opts->AddLongOption('f', "filename", "Filename of the file containing configuration to remove")
        .RequiredArgument("[String]").DefaultValue("").StoreResult(&Filename);
    config.Opts->AddLongOption("cluster", "Cluster name")
        .RequiredArgument("[String]").DefaultValue("").StoreResult(&Cluster);
    config.Opts->AddLongOption("version", "Config version")
        .RequiredArgument("[ui64]").StoreResult(&Version);
    config.Opts->AddLongOption("force", "Ignore version and cluster check")
        .StoreTrue(&Force);
    config.Opts->AddLongOption("directory", "Directory with volatile configs")
        .Optional().RequiredArgument("[directory]").StoreResult(&Dir);
}

void TCommandConfigVolatileDrop::Parse(TConfig& config) {
    TClientCommand::Parse(config);

    if ((ui32(!Ids.empty()) + ui32(All) + ui32(!Filename.empty()) + ui32(!Dir.empty())) != 1) {
        ythrow yexception() << "Must specify one of --id, --all, -f (--filename) and --directory";
    }

    if ((ui32(!Filename.empty() || !Dir.empty()) + ui32(!Ids.empty() || All)) != 1) {
        ythrow yexception() << "Must specify either --directory or -f (--filename) and --id or --all";
    }

    if (!Force && !(!Cluster.empty() || !Version) && Filename.empty() && Dir.empty()) {
        ythrow yexception() << "Must specify either --force or --cluster and --version or --filename";
    }
}

int TCommandConfigVolatileDrop::Run(TConfig& config) {
    auto driver = std::make_unique<NYdb::TDriver>(CreateDriver(config));
    auto client = NYdb::NDynamicConfig::TDynamicConfigClient(*driver);

    if (!Dir.empty()) {
        auto dir = TFsPath(Dir);
        TVector<TFsPath> entries;
        dir.List(entries);
        for (auto& entry : entries) {
            if (entry.IsFile() && entry.GetName().StartsWith("volatile_") && entry.GetName().EndsWith(".yaml")) {
                auto volatileConfigStr = TFileInput(entry).ReadAll();
                if (NYamlConfig::IsVolatileConfig(volatileConfigStr)) {
                    auto metadata = NYamlConfig::GetVolatileMetadata(volatileConfigStr);
                    Ids.insert(metadata.Id.value());
                    Cluster = metadata.Cluster.value();
                    Version = metadata.Version.value();
                }
                // TODO: throw error on multiple Cluster/Versions
            }
        }
    }

    if (!Filename.empty()) {
        auto volatileConfigStr = TFileInput(Filename).ReadAll();
        if (NYamlConfig::IsVolatileConfig(volatileConfigStr)) {
            auto metadata = NYamlConfig::GetVolatileMetadata(volatileConfigStr);
            Ids.insert(metadata.Id.value());
            Cluster = metadata.Cluster.value();
            Version = metadata.Version.value();
        } else {
            ythrow yexception() << "File " << Filename << " is not volatile config";
        }
    }

    auto status = [&]() {
        if (All && Force) {
            return client.ForceRemoveAllVolatileConfigs().GetValueSync();
        }

        if (All) {
            return client.RemoveAllVolatileConfigs(Cluster, Version).GetValueSync();
        }

        if (Force) {
            return client.ForceRemoveVolatileConfig(std::vector<uint64_t>(Ids.begin(), Ids.end())).GetValueSync();
        }

        return client.RemoveVolatileConfig(Cluster, Version, std::vector<uint64_t>(Ids.begin(), Ids.end())).GetValueSync();
    }();

    NStatusHelpers::ThrowOnErrorOrPrintIssues(status);

    Cout << status << Endl;

    return EXIT_SUCCESS;
}

TCommandConfigVolatileFetch::TCommandConfigVolatileFetch()
    : TYdbCommand("fetch", {"get", "dump"}, "Fetch volatile config")
{
}

void TCommandConfigVolatileFetch::Config(TConfig& config) {
    TYdbCommand::Config(config);
    config.Opts->AddLongOption("id", "Volatile config id")
        .Optional().RequiredArgument("[ui64]").InsertTo(&Ids);
    config.Opts->AddLongOption("all", "Fetch all volatile configs")
        .StoreTrue(&All);
    config.Opts->AddLongOption("output-directory", "Directory to save config(s)")
        .RequiredArgument("[directory]").StoreResult(&OutDir);
    config.Opts->AddLongOption("strip-metadata", "Strip metadata from config(s)")
        .StoreTrue(&StripMetadata);
    config.SetFreeArgsNum(0);
    config.Opts->MutuallyExclusive("output-directory", "strip-metadata");
}

void TCommandConfigVolatileFetch::Parse(TConfig& config) {
    TClientCommand::Parse(config);
}

int TCommandConfigVolatileFetch::Run(TConfig& config) {
    auto driver = std::make_unique<NYdb::TDriver>(CreateDriver(config));
    auto client = NYdb::NDynamicConfig::TDynamicConfigClient(*driver);
    auto result = client.GetConfig().GetValueSync();
    NStatusHelpers::ThrowOnErrorOrPrintIssues(result);

    if (OutDir) {
        TFsPath dir(OutDir);
        dir.MkDirs();
    }

    ui64 version = 0;

    for (auto [id, cfg] : result.GetVolatileConfigs()) {
        if (All || Ids.contains(id)) {
            version = NYamlConfig::GetVolatileMetadata(TString{cfg}).Version.value();

            if (StripMetadata) {
                cfg = NYamlConfig::StripMetadata(TString{cfg});
            }

            if (!OutDir) {
                Cout << WrapYaml(TString{cfg});
            } else {
                auto filename = TString("volatile_") + ToString(version) + "_" + ToString(id) + ".yaml";
                auto filepath = (TFsPath(OutDir) / filename);
                TFileOutput out(filepath);
                out << cfg;
            }
        }
    }

    return EXIT_SUCCESS;
}

TCommandGenerateDynamicConfig::TCommandGenerateDynamicConfig(bool allowEmptyDatabase)
    : TYdbReadOnlyCommand("generate", {}, "Generate dynamic config from startup static config")
    , AllowEmptyDatabase(allowEmptyDatabase)
{
}

void TCommandGenerateDynamicConfig::Config(TConfig& config) {
    TYdbCommand::Config(config);
    config.SetFreeArgsNum(0);
    config.AllowEmptyDatabase = AllowEmptyDatabase;
}

int TCommandGenerateDynamicConfig::Run(TConfig& config) {
    auto driver = std::make_unique<NYdb::TDriver>(CreateDriver(config));
    auto client = NYdb::NDynamicConfig::TDynamicConfigClient(*driver);

    auto result = client.FetchStartupConfig().GetValueSync();
    NStatusHelpers::ThrowOnErrorOrPrintIssues(result);
    if (NYamlConfig::IsStaticConfig(TString{result.GetConfig()})) {
        Cout << WrapStaticConfig(TString{result.GetConfig()});
    } else {
        Cout << "Startup config is already dynamic" << Endl;
    }

    return EXIT_SUCCESS;
}

TCommandVersionDynamicConfig::TCommandVersionDynamicConfig(bool allowEmptyDatabase)
    : TYdbReadOnlyCommand("version", {}, "Show configuration version on nodes")
    , AllowEmptyDatabase(allowEmptyDatabase)
{
}

void TCommandVersionDynamicConfig::Config(TConfig& config) {
    TYdbCommand::Config(config);
    config.Opts->AddLongOption("list-nodes", "List nodes with different configuration versions")
        .StoreTrue(&ListNodes);
    config.SetFreeArgsNum(0);
    config.AllowEmptyDatabase = AllowEmptyDatabase;
    AddOutputFormats(config, {
        EDataFormat::Pretty,
        EDataFormat::Json,
        EDataFormat::Csv
    });
}

void TCommandVersionDynamicConfig::Parse(TConfig& config) {
    TClientCommand::Parse(config);
    ParseOutputFormats();
}

int TCommandVersionDynamicConfig::Run(TConfig& config) {
    auto driver = std::make_unique<NYdb::TDriver>(CreateDriver(config));
    auto client = NYdb::NDynamicConfig::TDynamicConfigClient(*driver);
    auto result = client.GetConfigurationVersion(ListNodes).GetValueSync();
    NStatusHelpers::ThrowOnErrorOrPrintIssues(result);
    auto sortNodes = [&](const auto& list) {
        std::vector<NYdb::NDynamicConfig::TGetConfigurationVersionResult::TNodeInfo> sortedNodes(list.begin(), list.end());
        std::sort(sortedNodes.begin(), sortedNodes.end());
        return sortedNodes;
    };
    if (OutputFormat == EDataFormat::Json) {
        NJson::TJsonValue jsonOutput(NJson::JSON_MAP);
        auto serializeNodesInfo = [&](const TString& key, const auto& listGetter) {
            NJson::TJsonValue nodesArray(NJson::JSON_ARRAY);
            for (const auto& node : sortNodes(listGetter())) {
                NJson::TJsonValue nodeJson(NJson::JSON_MAP);
                nodeJson.InsertValue("node_id", node.NodeId);
                nodeJson.InsertValue("hostname", node.Hostname);
                nodeJson.InsertValue("port", node.Port);
                nodesArray.AppendValue(nodeJson);
            }
            jsonOutput.InsertValue(key, nodesArray);
        };

#define ADD_INFO_TO_JSON(type, key) \
        jsonOutput.InsertValue(#key "_nodes_count", result.Get##type##Nodes()); \
        if (ListNodes) { \
            serializeNodesInfo(#key "_nodes_list", [&]() { return result.Get##type##NodesList(); }); \
        }

        ADD_INFO_TO_JSON(V1, v1)
        ADD_INFO_TO_JSON(V2, v2)
        ADD_INFO_TO_JSON(Unknown, unknown)

        NJson::WriteJson(&Cout, &jsonOutput, true);
        Cout << Endl;
    } else if (OutputFormat == EDataFormat::Csv) {
        if (ListNodes) {
            Cout << "config_version,node_id,hostname,port" << Endl;
            auto printNodesToCsv = [&](const TString& versionString, const auto& listGetter) {
                for (const auto& node : sortNodes(listGetter())) {
                    TStringBuilder row;
                    row << versionString << "," << node.NodeId << ",\"" << node.Hostname << "\"," << node.Port;
                    Cout << row << Endl;
                }
            };

#define PRINT_NODES_TO_CSV(type, key) \
        printNodesToCsv(#key, [&]() { return result.Get##type##NodesList(); }); \

            PRINT_NODES_TO_CSV(V1, v1)
            PRINT_NODES_TO_CSV(V2, v2)
            PRINT_NODES_TO_CSV(Unknown, unknown)
        } else {
            Cout << "config_version,nodes_count" << Endl;
            Cout << "v1," << result.GetV1Nodes() << Endl;
            Cout << "v2," << result.GetV2Nodes() << Endl;
            Cout << "unknown," << result.GetUnknownNodes() << Endl;
        }
    } else {
        auto printNodeList = [&](const TString& header, const auto& listGetter) {
            const auto& nodesVector = sortNodes(listGetter());
            Cout << header;
            for (const auto& node : nodesVector) {
                Cout << "\n";
                Cout << "  - " << node.Hostname << ":" << node.Port << " (node_id: " << node.NodeId << ")";
            }
            Cout << Endl;
        };

#define PRINT_NODE_VERSION_INFO(type) \
    Cout << #type " nodes: " << result.Get##type##Nodes() << Endl; \
    if (ListNodes) { \
        printNodeList(#type " node list: ", [&]() { return result.Get##type##NodesList(); }); \
    }

        PRINT_NODE_VERSION_INFO(V1)
        PRINT_NODE_VERSION_INFO(V2)
        PRINT_NODE_VERSION_INFO(Unknown)
    }

    return EXIT_SUCCESS;
}


} // namespace NYdb::NConsoleClient::NDynamicConfig
