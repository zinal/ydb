#include "s3_fetcher.h"

#include <ydb/library/actors/core/actorsystem.h>
#include <ydb/library/actors/core/hfunc.h>
#include <ydb/library/yql/providers/s3/common/util.h>

namespace NKikimr::NExternalSource::NObjectStorage {

class S3Fetcher : public NActors::TActorBootstrapped<S3Fetcher> {
public:
    S3Fetcher(
        TString url,
        NYql::IHTTPGateway::TPtr gateway,
        NYql::IHTTPGateway::TRetryPolicy::TPtr retryPolicy,
        const NYql::TS3Credentials& credentials)
        : Url_{std::move(url)}
        , Gateway_{std::move(gateway)}
        , RetryPolicy_{std::move(retryPolicy)}
        , Credentials_(credentials)
    {}

    void Bootstrap() {
        Become(&S3Fetcher::WorkingState);
    }

    STRICT_STFUNC(WorkingState,
        HFunc(TEvRequestS3Range, HandleRequest);

        HFunc(TEvS3DownloadResponse, HandleDownloadReponse);
    )

    void HandleRequest(TEvRequestS3Range::TPtr& ev, const NActors::TActorContext& ctx) {
        StartDownload(std::shared_ptr<TEvRequestS3Range>(ev->Release().Release()), ctx.ActorSystem());
    }

    void HandleDownloadReponse(TEvS3DownloadResponse::TPtr& ev, const NActors::TActorContext& ctx) {
        auto& response = *ev->Get();
        auto& result = response.Result;

        bool badCurl = result.CurlResponseCode != CURLE_OK;
        const auto httpCodeStart = result.Content.HttpResponseCode / 100;
        bool badHttp = httpCodeStart == 4 || httpCodeStart == 5;
        if (badCurl || badHttp || !result.Issues.Empty()) {
            ctx.Send(response.Request->Sender, new TEvS3RangeError(
                result.CurlResponseCode,
                result.Content.HttpResponseCode,
                std::move(result.Issues),
                std::move(response.Request->Path),
                response.Request->RequestId)
            );
            return;
        }

        auto code = result.Content.HttpResponseCode;
        ctx.Send(response.Request->Sender, new TEvS3RangeResponse(
            code,
            result.Content.Extract(),
            std::move(response.Request->Path),
            response.Request->RequestId
        ));
    }

    void StartDownload(std::shared_ptr<TEvRequestS3Range>&& request, NActors::TActorSystem* actorSystem) {
        auto length = request->End - request->Start;
        const auto& authInfo = Credentials_.GetAuthInfo();
        auto headers = NYql::IHTTPGateway::MakeYcHeaders(
            request->RequestId.AsGuidString(),
            authInfo.GetToken(),
            {},
            authInfo.GetAwsUserPwd(),
            authInfo.GetAwsSigV4()
        );

        Gateway_->Download(
            NYql::NS3Util::UrlEscapeRet(Url_ + request->Path), std::move(headers), request->Start, length,
            [actorSystem, selfId = SelfId(), request = std::move(request)](NYql::IHTTPGateway::TResult&& result) mutable {
                actorSystem->Send(selfId, new TEvS3DownloadResponse(std::move(request), std::move(result)));
            }, {}, RetryPolicy_);
    }

private:
    TString Url_;
    NYql::IHTTPGateway::TPtr Gateway_;
    NYql::IHTTPGateway::TRetryPolicy::TPtr RetryPolicy_;
    const NYql::TS3Credentials Credentials_;
};

NActors::IActor* CreateS3FetcherActor(
    TString url,
    NYql::IHTTPGateway::TPtr gateway,
    NYql::IHTTPGateway::TRetryPolicy::TPtr retryPolicy,
    const NYql::TS3Credentials& credentials) {

    return new S3Fetcher(std::move(url), std::move(gateway), std::move(retryPolicy), credentials);
}
} // namespace NKikimr::NExternalSource::NObjectStorage
