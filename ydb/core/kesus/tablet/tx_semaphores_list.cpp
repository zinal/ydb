#include "tablet_impl.h"

namespace NKikimr {
namespace NKesus {

struct TKesusTablet::TTxSemaphoresList : public TTxBase {
    const TActorId Sender;
    const ui64 Cookie;
    const NKikimrKesus::TEvListSemaphores Record;

    THolder<TEvKesus::TEvListSemaphoresResult> Reply;

    TTxSemaphoresList(TSelf* self, const TActorId& sender, ui64 cookie, const NKikimrKesus::TEvListSemaphores& record)
        : TTxBase(self)
        , Sender(sender)
        , Cookie(cookie)
        , Record(record)
    {}

    TTxType GetTxType() const override { return TXTYPE_SEMAPHORES_LIST; }

    bool Execute(TTransactionContext& txc, const TActorContext& ctx) override {
        LOG_DEBUG_S(ctx, NKikimrServices::KESUS_TABLET,
            "[" << Self->TabletID() << "] TTxSemaphoresList::Execute (sender=" << Sender
                << ", cookie=" << Cookie << ")");

        NIceDb::TNiceDb db(txc.DB);
        if (Self->UseStrictRead()) {
            Self->PersistStrictMarker(db);
        }

        Reply.Reset(new TEvKesus::TEvListSemaphoresResult());
        Reply->Record.MutableError()->SetStatus(Ydb::StatusIds::SUCCESS);

        for (const auto& kv : Self->SemaphoresByName) {
            Reply->Record.AddSemaphoreNames(kv.first);
        }

        return true;
    }

    void Complete(const TActorContext& ctx) override {
        LOG_DEBUG_S(ctx, NKikimrServices::KESUS_TABLET,
            "[" << Self->TabletID() << "] TTxSemaphoresList::Complete (sender=" << Sender
                << ", cookie=" << Cookie << ")");

        Y_ABORT_UNLESS(Reply);
        ctx.Send(Sender, Reply.Release(), 0, Cookie);
    }
};

void TKesusTablet::Handle(TEvKesus::TEvListSemaphores::TPtr& ev) {
    const auto& record = ev->Get()->Record;
    VerifyKesusPath(record.GetKesusPath());

    Execute(new TTxSemaphoresList(this, ev->Sender, ev->Cookie, record), TActivationContext::AsActorContext());
}

}
}
