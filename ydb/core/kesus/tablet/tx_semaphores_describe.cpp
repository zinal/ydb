#include "tablet_impl.h"

#include <util/generic/algorithm.h>

namespace NKikimr {
namespace NKesus {

struct TKesusTablet::TTxSemaphoresDescribe : public TTxBase {
    const TActorId Sender;
    const ui64 Cookie;
    const bool IncludeOwnersAndWaiters;

    THolder<TEvKesus::TEvDescribeSemaphoresResult> Reply;

    TTxSemaphoresDescribe(TSelf* self, const TActorId& sender, ui64 cookie, bool includeOwnersAndWaiters)
        : TTxBase(self)
        , Sender(sender)
        , Cookie(cookie)
        , IncludeOwnersAndWaiters(includeOwnersAndWaiters)
    {}

    TTxType GetTxType() const override { return TXTYPE_SEMAPHORES_DESCRIBE; }

    bool Execute(TTransactionContext& txc, const TActorContext& ctx) override {
        LOG_DEBUG_S(ctx, NKikimrServices::KESUS_TABLET,
            "[" << Self->TabletID() << "] TTxSemaphoresDescribe::Execute (sender=" << Sender
                << ", cookie=" << Cookie << ", includeOwnersAndWaiters=" << IncludeOwnersAndWaiters << ")");

        NIceDb::TNiceDb db(txc.DB);
        if (Self->UseStrictRead()) {
            Self->PersistStrictMarker(db);
        }

        Reply.Reset(new TEvKesus::TEvDescribeSemaphoresResult(0));

        TVector<TString> names;
        names.reserve(Self->SemaphoresByName.size());
        for (const auto& kv : Self->SemaphoresByName) {
            names.push_back(kv.first);
        }
        Sort(names);

        for (const TString& name : names) {
            auto* semaphore = Self->SemaphoresByName.Value(name, nullptr);
            Y_ABORT_UNLESS(semaphore);
            auto* desc = Reply->Record.AddSemaphoreDescriptions();
            desc->set_name(semaphore->Name);
            desc->set_data(semaphore->Data);
            desc->set_count(semaphore->Count);
            desc->set_limit(semaphore->Limit);
            desc->set_ephemeral(semaphore->Ephemeral);

            if (IncludeOwnersAndWaiters) {
                for (const auto* owner : semaphore->Owners) {
                    auto* p = desc->add_owners();
                    p->set_order_id(owner->OrderId);
                    p->set_session_id(owner->SessionId);
                    p->set_count(owner->Count);
                    p->set_data(owner->Data);
                }
                for (const auto& kv : semaphore->Waiters) {
                    auto* waiter = kv.second;
                    auto* p = desc->add_waiters();
                    p->set_order_id(waiter->OrderId);
                    p->set_session_id(waiter->SessionId);
                    p->set_timeout_millis(waiter->TimeoutMillis);
                    p->set_count(waiter->Count);
                    p->set_data(waiter->Data);
                }
            }
        }

        return true;
    }

    void Complete(const TActorContext& ctx) override {
        LOG_DEBUG_S(ctx, NKikimrServices::KESUS_TABLET,
            "[" << Self->TabletID() << "] TTxSemaphoresDescribe::Complete (sender=" << Sender
                << ", cookie=" << Cookie << ")");

        Y_ABORT_UNLESS(Reply);
        ctx.Send(Sender, Reply.Release(), 0, Cookie);
    }
};

void TKesusTablet::Handle(TEvKesus::TEvDescribeSemaphores::TPtr& ev) {
    const auto& record = ev->Get()->Record;
    VerifyKesusPath(record.GetKesusPath());

    Execute(new TTxSemaphoresDescribe(
                this,
                ev->Sender,
                ev->Cookie,
                record.GetIncludeOwnersAndWaiters()),
        TActivationContext::AsActorContext());
}

}
}
