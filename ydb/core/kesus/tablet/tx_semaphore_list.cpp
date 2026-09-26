#include "tablet_impl.h"

namespace NKikimr {
namespace NKesus {

struct TKesusTablet::TTxSemaphoreList : public TTxBase {
    const TActorId Sender;
    const ui64 Cookie;
    const NKikimrKesus::TEvListSemaphores Record;

    THolder<TEvKesus::TEvListSemaphoresResult> Reply;

    TTxSemaphoreList(TSelf* self, const TActorId& sender, ui64 cookie, const NKikimrKesus::TEvListSemaphores& record)
        : TTxBase(self)
        , Sender(sender)
        , Cookie(cookie)
        , Record(record)
    {}

    TTxType GetTxType() const override { return TXTYPE_SEMAPHORE_LIST; }

    bool Execute(TTransactionContext& txc, const TActorContext& ctx) override {
        LOG_DEBUG_S(ctx, NKikimrServices::KESUS_TABLET,
            "[" << Self->TabletID() << "] TTxSemaphoreList::Execute (sender=" << Sender
                << ", cookie=" << Cookie << ")");

        NIceDb::TNiceDb db(txc.DB);

        if (Record.GetProxyGeneration() != 0) {
            Reply.Reset(new TEvKesus::TEvListSemaphoresResult(
                Record.GetProxyGeneration(),
                Ydb::StatusIds::BAD_REQUEST,
                "Only direct proxy generation 0 is supported for listing semaphores"));
            return true;
        }

        if (Self->UseStrictRead()) {
            Self->PersistStrictMarker(db);
        }

        Reply.Reset(new TEvKesus::TEvListSemaphoresResult(0));

        TVector<TSemaphoreInfo*> sorted;
        sorted.reserve(Self->Semaphores.size());
        for (auto& kv : Self->Semaphores) {
            sorted.push_back(&kv.second);
        }
        Sort(sorted.begin(), sorted.end(), [](const TSemaphoreInfo* a, const TSemaphoreInfo* b) {
            return a->Name < b->Name;
        });

        const bool includeDetails = Record.GetIncludeDetails();
        for (TSemaphoreInfo* semaphore : sorted) {
            auto* desc = Reply->Record.AddSemaphoreDescriptions();
            desc->set_name(semaphore->Name);
            desc->set_limit(semaphore->Limit);
            desc->set_ephemeral(semaphore->Ephemeral);
            desc->set_count(semaphore->Count);
            if (includeDetails) {
                desc->set_data(semaphore->Data);
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
            "[" << Self->TabletID() << "] TTxSemaphoreList::Complete (sender=" << Sender
                << ", cookie=" << Cookie << ")");
        Y_ABORT_UNLESS(Reply);
        ctx.Send(Sender, Reply.Release(), 0, Cookie);
    }
};

void TKesusTablet::Handle(TEvKesus::TEvListSemaphores::TPtr& ev) {
    const auto& record = ev->Get()->Record;
    VerifyKesusPath(record.GetKesusPath());

    Execute(new TTxSemaphoreList(this, ev->Sender, ev->Cookie, record), TActivationContext::AsActorContext());
}

}
}
