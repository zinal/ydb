LIBRARY()

PEERDIR(
    yql/essentials/ast
    yql/essentials/core
    ydb/library/yql/dq/common
    ydb/library/yql/dq/expr_nodes
    yql/essentials/core/dq_integration
    yql/essentials/parser/pg_wrapper/interface
    ydb/library/yql/dq/proto
    ydb/library/yql/dq/type_ann
    ydb/library/yql/providers/dq/expr_nodes
    ydb/core/kqp/expr_nodes
)

SRCS(
    dq_opt.cpp
    dq_opt_build.cpp
    dq_opt_conflict_rules_collector.cpp
    dq_opt_join.cpp
    dq_opt_join_cbo_factory.cpp
    dq_opt_join_cost_based.cpp
    dq_opt_join_tree_node.cpp
    dq_opt_hopping.cpp
    dq_opt_log.cpp
    dq_opt_peephole.cpp
    dq_opt_phy_finalizing.cpp
    dq_opt_phy.cpp
    dq_opt_stat.cpp
    dq_opt_stat_transformer_base.cpp
    dq_opt_predicate_selectivity.cpp
)

YQL_LAST_ABI_VERSION()

END()

RECURSE_FOR_TESTS(ut)
