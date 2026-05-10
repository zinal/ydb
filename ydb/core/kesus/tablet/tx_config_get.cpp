#include "tablet_impl.h"

namespace NKikimr {
namespace NKesus {

struct TKesusTablet::TTxConfigGet : public TTxBase {
    const TActorId Sender;
    const ui64 Cookie;
    const bool IncludeSemaphoreNames;

    THolder<TEvKesus::TEvGetConfigResult> Reply;

    TTxConfigGet(TSelf* self, const TActorId& sender, ui64 cookie, bool includeSemaphoreNames)
        : TTxBase(self)
        , Sender(sender)
        , Cookie(cookie)
        , IncludeSemaphoreNames(includeSemaphoreNames)
    {}

    TTxType GetTxType() const override { return TXTYPE_CONFIG_GET; }

    bool Execute(TTransactionContext& txc, const TActorContext& ctx) override {
        LOG_DEBUG_S(ctx, NKikimrServices::KESUS_TABLET,
            "[" << Self->TabletID() << "] TTxConfigGet::Execute (sender=" << Sender
                << ", cookie=" << Cookie << ")");

        NIceDb::TNiceDb db(txc.DB);
        Self->PersistStrictMarker(db);

        Reply.Reset(new TEvKesus::TEvGetConfigResult());
        auto* config = Reply->Record.MutableConfig();
        config->set_path(Self->KesusPath); // TODO: remove legacy field eventually
        config->set_self_check_period_millis(Self->SelfCheckPeriod.MilliSeconds());
        config->set_session_grace_period_millis(Self->SessionGracePeriod.MilliSeconds());
        config->set_read_consistency_mode(Self->ReadConsistencyMode);
        config->set_attach_consistency_mode(Self->AttachConsistencyMode);
        config->set_rate_limiter_counters_mode(Self->RateLimiterCountersMode);
        Reply->Record.SetVersion(Self->ConfigVersion);
        Reply->Record.SetPath(Self->KesusPath);
        if (IncludeSemaphoreNames) {
            TVector<TString> names;
            names.reserve(Self->Semaphores.size());
            for (const auto& kv : Self->Semaphores) {
                names.push_back(kv.second.Name);
            }
            Sort(names.begin(), names.end());
            for (const TString& name : names) {
                Reply->Record.AddSemaphoreNames(name);
            }
        }
        return true;
    }

    void Complete(const TActorContext& ctx) override {
        LOG_DEBUG_S(ctx, NKikimrServices::KESUS_TABLET,
            "[" << Self->TabletID() << "] TTxConfigGet::Complete (sender=" << Sender
                << ", cookie=" << Cookie << ")");
        Y_ABORT_UNLESS(Reply);
        ctx.Send(Sender, Reply.Release(), 0, Cookie);
    }
};

void TKesusTablet::Handle(TEvKesus::TEvGetConfig::TPtr& ev) {
    Execute(new TTxConfigGet(this, ev->Sender, ev->Cookie, ev->Get()->Record.GetIncludeSemaphoreNames()),
        TActivationContext::AsActorContext());
}

}
}
