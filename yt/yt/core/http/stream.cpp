#include "stream.h"
#include "private.h"

#include <yt/yt/core/net/connection.h>

#include <yt/yt/core/misc/finally.h>

#include <library/cpp/yt/misc/global.h>

#include <util/generic/buffer.h>

#include <util/string/escape.h>

namespace NYT::NHttp {

using namespace NConcurrency;
using namespace NNet;

////////////////////////////////////////////////////////////////////////////////

constinit const auto Logger = HttpLogger;

////////////////////////////////////////////////////////////////////////////////

namespace {

YT_DEFINE_GLOBAL(const THeaders::THeaderNames, FilteredHeaders, {
    "transfer-encoding",
    "content-length",
    "connection",
    "host",
});

YT_DEFINE_GLOBAL(const TSharedRef, Http100ContinueBuffer, TSharedRef::FromString("HTTP/1.1 100 Continue\r\n\r\n"));
YT_DEFINE_GLOBAL(const TSharedRef, CrLfBuffer, TSharedRef::FromString("\r\n"));
YT_DEFINE_GLOBAL(const TSharedRef, ZeroCrLfBuffer, TSharedRef::FromString("0\r\n"));
YT_DEFINE_GLOBAL(const TSharedRef, ZeroCrLfCrLfBuffer, TSharedRef::FromString("0\r\n\r\n"));

} // namespace

////////////////////////////////////////////////////////////////////////////////

http_parser_settings THttpParser::GetParserSettings()
{
    http_parser_settings settings;
    http_parser_settings_init(&settings);

    settings.on_url = &OnUrl;
    settings.on_status = &OnStatus;
    settings.on_header_field = &OnHeaderField;
    settings.on_header_value = &OnHeaderValue;
    settings.on_headers_complete = &OnHeadersComplete;
    settings.on_body = &OnBody;
    settings.on_message_complete = &OnMessageComplete;

    return settings;
}

const http_parser_settings ParserSettings = THttpParser::GetParserSettings();

THttpParser::THttpParser(http_parser_type parserType)
    : ParserType_(parserType)
    , Headers_(New<THeaders>())
{
    http_parser_init(&Parser_, parserType);
    Parser_.data = reinterpret_cast<void*>(this);
}

EParserState THttpParser::GetState() const
{
    return State_;
}

void THttpParser::Reset()
{
    Headers_ = New<THeaders>();
    Trailers_.Reset();

    ShouldKeepAlive_ = false;
    HeaderBuffered_ = false;
    State_ = EParserState::Initialized;

    FirstLine_.Reset();
    NextField_.Reset();
    NextValue_.Reset();
    LastBodyChunk_ = {};
    YT_VERIFY(FirstLine_.GetLength() == 0);
    YT_VERIFY(NextField_.GetLength() == 0);
    YT_VERIFY(NextValue_.GetLength() == 0);

    http_parser_init(&Parser_, ParserType_);
}

TSharedRef THttpParser::Feed(const TSharedRef& input)
{
    InputBuffer_ = &input;
    auto finally = Finally([&] {
        InputBuffer_ = nullptr;
    });

    size_t read = http_parser_execute(&Parser_, &ParserSettings, input.Begin(), input.Size());
    auto http_errno = static_cast<enum http_errno>(Parser_.http_errno);
    if (http_errno != 0 && http_errno != HPE_PAUSED) {
        // 64 bytes before error
        size_t contextStart = read - std::min<size_t>(read, 64);

        // and 64 bytes after error
        size_t contextEnd = std::min(read + 64, input.Size());

        TString errorContext(input.Begin() + contextStart, contextEnd - contextStart);

        THROW_ERROR_EXCEPTION("HTTP parse error: %v", http_errno_description(http_errno))
            << TErrorAttribute("parser_error_name", http_errno_name(http_errno))
            << TErrorAttribute("error_context", EscapeC(errorContext));
    }

    if (http_errno == HPE_PAUSED) {
        http_parser_pause(&Parser_, 0);
    }

    return input.Slice(read, input.Size());
}

std::pair<int, int> THttpParser::GetVersion() const
{
    return std::pair<int, int>(Parser_.http_major, Parser_.http_minor);
}

EStatusCode THttpParser::GetStatusCode() const
{
    return EStatusCode(Parser_.status_code);
}

EMethod THttpParser::GetMethod() const
{
    return EMethod(Parser_.method);
}

TString THttpParser::GetFirstLine()
{
    return FirstLine_.Flush();
}

const THeadersPtr& THttpParser::GetHeaders() const
{
    return Headers_;
}

const THeadersPtr& THttpParser::GetTrailers() const
{
    return Trailers_;
}

TSharedRef THttpParser::GetLastBodyChunk()
{
    auto chunk = LastBodyChunk_;
    LastBodyChunk_ = TSharedRef::MakeEmpty();
    return chunk;
}

bool THttpParser::ShouldKeepAlive() const
{
    return ShouldKeepAlive_;
}

void THttpParser::MaybeFlushHeader(bool trailer)
{
    if (!HeaderBuffered_) {
        return;
    }

    HeaderBuffered_ = false;
    if (NextField_.GetLength() == 0) {
        return;
    }

    if (trailer) {
        if (!Trailers_) {
            Trailers_ = New<THeaders>();
        }
        Trailers_->Set(NextField_.Flush(), NextValue_.Flush());
    } else {
        Headers_->Set(NextField_.Flush(), NextValue_.Flush());
    }
}

int THttpParser::OnUrl(http_parser* parser, const char* at, size_t length)
{
    auto that = reinterpret_cast<THttpParser*>(parser->data);
    that->FirstLine_.AppendString(TStringBuf(at, length));

    return 0;
}

int THttpParser::OnStatus(http_parser* /*parser*/, const char* /*at*/, size_t /*length*/)
{
    return 0;
}

int THttpParser::OnHeaderField(http_parser* parser, const char* at, size_t length)
{
    auto that = reinterpret_cast<THttpParser*>(parser->data);
    that->MaybeFlushHeader(that->State_ == EParserState::HeadersFinished);

    that->NextField_.AppendString(TStringBuf(at, length));
    return 0;
}

int THttpParser::OnHeaderValue(http_parser* parser, const char* at, size_t length)
{
    auto that = reinterpret_cast<THttpParser*>(parser->data);
    that->NextValue_.AppendString(TStringBuf(at, length));
    that->HeaderBuffered_ = true;

    return 0;
}

int THttpParser::OnHeadersComplete(http_parser* parser)
{
    auto that = reinterpret_cast<THttpParser*>(parser->data);
    that->MaybeFlushHeader(that->State_ == EParserState::HeadersFinished);

    that->State_ = EParserState::HeadersFinished;

    return 0;
}

int THttpParser::OnBody(http_parser* parser, const char* at, size_t length)
{
    auto that = reinterpret_cast<THttpParser*>(parser->data);
    that->LastBodyChunk_ = that->InputBuffer_->Slice(at, at + length);
    http_parser_pause(parser, 1);
    return 0;
}

int THttpParser::OnMessageComplete(http_parser* parser)
{
    auto that = reinterpret_cast<THttpParser*>(parser->data);
    that->MaybeFlushHeader(that->State_ == EParserState::HeadersFinished);

    that->State_ = EParserState::MessageFinished;
    that->ShouldKeepAlive_ = http_should_keep_alive(parser);
    http_parser_pause(parser, 1);
    return 0;
}

////////////////////////////////////////////////////////////////////////////////

struct THttpParserTag
{ };

THttpInput::THttpInput(
    IConnectionPtr connection,
    const TNetworkAddress& remoteAddress,
    IInvokerPtr readInvoker,
    EMessageType messageType,
    THttpIOConfigPtr config)
    : Connection_(std::move(connection))
    , RemoteAddress_(remoteAddress)
    , MessageType_(messageType)
    , Config_(std::move(config))
    , ReadInvoker_(std::move(readInvoker))
    , InputBuffer_(TSharedMutableRef::Allocate<THttpParserTag>(Config_->ReadBufferSize, {.InitializeStorage = false}))
    , Parser_(messageType == EMessageType::Request ? HTTP_REQUEST : HTTP_RESPONSE)
    , StartByteCount_(Connection_->GetReadByteCount())
    , StartStatistics_(Connection_->GetReadStatistics())
    , LastProgressLogTime_(TInstant::Now())
{ }

std::pair<int, int> THttpInput::GetVersion()
{
    EnsureHeadersReceived();
    return Parser_.GetVersion();
}

EMethod THttpInput::GetMethod()
{
    YT_VERIFY(MessageType_ == EMessageType::Request);

    EnsureHeadersReceived();
    return Parser_.GetMethod();
}

const TUrlRef& THttpInput::GetUrl()
{
    YT_VERIFY(MessageType_ == EMessageType::Request);

    EnsureHeadersReceived();
    return Url_;
}

const THeadersPtr& THttpInput::GetHeaders()
{
    EnsureHeadersReceived();
    return Headers_;
}

EStatusCode THttpInput::GetStatusCode()
{
    EnsureHeadersReceived();
    return Parser_.GetStatusCode();
}

const THeadersPtr& THttpInput::GetTrailers()
{
    if (Parser_.GetState() != EParserState::MessageFinished) {
        THROW_ERROR(AnnotateError(TError("Cannot access trailers while body is not fully consumed")));
    }

    const auto& trailers = Parser_.GetTrailers();
    if (!trailers) {
        static THeadersPtr emptyTrailers = New<THeaders>();
        return emptyTrailers;
    }
    return trailers;
}

const TNetworkAddress& THttpInput::GetRemoteAddress() const
{
    return RemoteAddress_;
}

TConnectionId THttpInput::GetConnectionId() const
{
    return Connection_->GetId();
}

TRequestId THttpInput::GetRequestId() const
{
    return RequestId_;
}

void THttpInput::SetRequestId(TRequestId requestId)
{
    RequestId_ = requestId;
}

bool THttpInput::IsExpecting100Continue() const
{
    auto expectHeader = Headers_->Find("Expect");
    return expectHeader && *expectHeader == "100-continue";
}

bool THttpInput::IsSafeToReuse() const
{
    return SafeToReuse_;
}

void THttpInput::Reset()
{
    HeadersReceived_ = false;
    Headers_.Reset();
    Parser_.Reset();
    RawUrl_ = {};
    Url_ = {};
    SafeToReuse_ = false;
    LastProgressLogTime_ = TInstant::Now();
    StartTime_ = TInstant::Zero();

    StartByteCount_ = Connection_->GetReadByteCount();
    StartStatistics_ = Connection_->GetReadStatistics();
}

TError THttpInput::AnnotateError(const TError& error)
{
    return error
        << TErrorAttribute("connection_id", Connection_->GetId())
        << TErrorAttribute("request_id", RequestId_);
}

void THttpInput::FinishHeaders()
{
    HeadersReceived_ = true;
    Headers_ = Parser_.GetHeaders();

    if (MessageType_ == EMessageType::Request) {
        RawUrl_ = Parser_.GetFirstLine();
        Url_ = ParseUrl(RawUrl_);
    }
}

void THttpInput::EnsureHeadersReceived()
{
    if (!ReceiveHeaders()) {
        THROW_ERROR(AnnotateError(TError("Connection was closed before the first byte of HTTP message")));
    }
}

bool THttpInput::ReceiveHeaders()
{
    if (HeadersReceived_) {
        return true;
    }

    bool idleConnection = MessageType_ == EMessageType::Request;
    auto start = TInstant::Now();

    if (idleConnection) {
        Connection_->SetReadDeadline(start + Config_->ConnectionIdleTimeout);
    } else {
        Connection_->SetReadDeadline(start + Config_->HeaderReadTimeout);
    }

    while (true) {
        MaybeLogSlowProgress();

        bool eof = false;
        TErrorOr<size_t> readResult;
        if (UnconsumedData_.Empty()) {
            auto asyncReadResult = Connection_->Read(InputBuffer_);
            readResult = WaitFor(asyncReadResult);
            if (readResult.IsOK()) {
                UnconsumedData_ = InputBuffer_.Slice(0, readResult.ValueOrThrow());
                if (!StartTime_) {
                    StartTime_ = TInstant::Now();
                }
            } else {
                UnconsumedData_ = InputBuffer_.Slice(static_cast<size_t>(0), static_cast<size_t>(0));
            }
            eof = UnconsumedData_.Size() == 0;
        }

        try {
            UnconsumedData_ = Parser_.Feed(UnconsumedData_);
        } catch (const std::exception& ex) {
            if (!readResult.IsOK()) {
                THROW_ERROR(AnnotateError(TError(ex) << readResult));
            } else {
                throw;
            }
        }

        if (Parser_.GetState() != EParserState::Initialized) {
            FinishHeaders();
            if (Parser_.GetState() == EParserState::MessageFinished) {
                FinishMessage();
            }
            Connection_->SetReadDeadline(std::nullopt);
            return true;
        }

        // HTTP parser does not treat EOF at message start as error.
        if (eof) {
            return false;
        }

        if (idleConnection) {
            idleConnection = false;
            Connection_->SetReadDeadline(StartTime_ + Config_->HeaderReadTimeout);
        }
    }
}

void THttpInput::FinishMessage()
{
    SafeToReuse_ = Parser_.ShouldKeepAlive();

    auto stats = Connection_->GetReadStatistics();
    if (MessageType_ == EMessageType::Request) {
        YT_LOG_DEBUG("Finished reading HTTP request body (RequestId: %v, BytesIn: %v, IdleDuration: %v, BusyDuration: %v, Keep-Alive: %v)",
            RequestId_,
            GetReadByteCount(),
            stats.IdleDuration - StartStatistics_.IdleDuration,
            stats.BusyDuration - StartStatistics_.BusyDuration,
            Parser_.ShouldKeepAlive());
    }
}

TFuture<TSharedRef> THttpInput::Read()
{
    return BIND(&THttpInput::DoRead, MakeStrong(this))
        .AsyncVia(ReadInvoker_)
        .Run();
}

i64 THttpInput::GetReadByteCount() const
{
    return Connection_->GetReadByteCount() - StartByteCount_;
}

TInstant THttpInput::GetStartTime() const
{
    return StartTime_;
}

bool THttpInput::IsHttps() const
{
    return IsHttps_;
}

void THttpInput::SetHttps()
{
    IsHttps_ = true;
}

int THttpInput::GetPort() const
{
    return Port_;
}

void THttpInput::SetPort(int port)
{
    Port_ = port;
}

TSharedRef THttpInput::DoRead()
{
    if (Parser_.GetState() == EParserState::MessageFinished) {
        return TSharedRef{};
    }

    Connection_->SetReadDeadline(TInstant::Now() + Config_->BodyReadIdleTimeout);
    while (true) {
        MaybeLogSlowProgress();

        auto chunk = Parser_.GetLastBodyChunk();
        if (!chunk.Empty()) {
            Connection_->SetReadDeadline(std::nullopt);
            return chunk;
        }

        bool eof = false;
        if (UnconsumedData_.Empty()) {
            auto asyncRead = Connection_->Read(InputBuffer_);
            UnconsumedData_ = InputBuffer_.Slice(0, WaitFor(asyncRead).ValueOrThrow());
            eof = UnconsumedData_.Size() == 0;
        }

        UnconsumedData_ = Parser_.Feed(UnconsumedData_);
        if (Parser_.GetState() == EParserState::MessageFinished) {
            FinishMessage();

            Connection_->SetReadDeadline(std::nullopt);
            return TSharedRef{};
        }

        // EOF must be handled by HTTP parser.
        YT_VERIFY(!eof);
    }
}

void THttpInput::MaybeLogSlowProgress()
{
    auto now = TInstant::Now();
    if (LastProgressLogTime_ + Config_->BodyReadIdleTimeout < now) {
        YT_LOG_DEBUG("Reading HTTP message (RequestId: %v, BytesIn: %v)",
            RequestId_,
            GetReadByteCount());
        LastProgressLogTime_ = now;
    }
}

bool THttpInput::IsRedirectCode(EStatusCode code) const
{
    return code == EStatusCode::MovedPermanently ||
        code == EStatusCode::Found ||
        code == EStatusCode::SeeOther ||
        code == EStatusCode::UseProxy ||
        code == EStatusCode::TemporaryRedirect ||
        code == EStatusCode::PermanentRedirect;
}

std::optional<TString> THttpInput::TryGetRedirectUrl()
{
    EnsureHeadersReceived();
    if (IsRedirectCode(GetStatusCode())) {
        if (auto url = Headers_->Find("Location")) {
            // TODO(babenko): switch to std::string
            return TString(*url);
        }
    }
    return std::nullopt;
}

////////////////////////////////////////////////////////////////////////////////

THttpOutput::THttpOutput(
    THeadersPtr headers,
    IConnectionPtr connection,
    EMessageType messageType,
    THttpIOConfigPtr config)
    : Connection_(std::move(connection))
    , MessageType_(messageType)
    , Config_(std::move(config))
    , OnWriteFinish_(BIND_NO_PROPAGATE(&THttpOutput::OnWriteFinish, MakeWeak(this)))
    , StartByteCount_(Connection_->GetWriteByteCount())
    , StartStatistics_(Connection_->GetWriteStatistics())
    , LastProgressLogTime_(TInstant::Now())
    , Headers_(std::move(headers))
{ }

THttpOutput::THttpOutput(
    IConnectionPtr connection,
    EMessageType messageType,
    THttpIOConfigPtr config)
    : THttpOutput(
        New<THeaders>(),
        std::move(connection),
        messageType,
        std::move(config))
{ }

const THeadersPtr& THttpOutput::GetHeaders()
{
    return Headers_;
}

void THttpOutput::SetHost(TStringBuf host, TStringBuf port)
{
    if (!port.empty()) {
        HostHeader_ = Format("%v:%v", host, port);
    } else {
        HostHeader_ = TString(host);
    }
}

void THttpOutput::SetHeaders(const THeadersPtr& headers)
{
    Headers_ = headers;
}

bool THttpOutput::AreHeadersFlushed() const
{
    return HeadersFlushed_;
}

const THeadersPtr& THttpOutput::GetTrailers()
{
    if (!Trailers_) {
        Trailers_ = New<THeaders>();
    }
    return Trailers_;
}

void THttpOutput::AddConnectionCloseHeader()
{
    YT_VERIFY(MessageType_ == EMessageType::Response);
    ConnectionClose_ = true;
}

bool THttpOutput::IsSafeToReuse() const
{
    return MessageFinished_ && !ConnectionClose_;
}

void THttpOutput::Reset()
{
    RequestId_ = {};

    StartByteCount_ = Connection_->GetWriteByteCount();
    StartStatistics_ = Connection_->GetWriteStatistics();
    HeadersLogged_ = false;

    ConnectionClose_ = false;
    Headers_ = New<THeaders>();

    Status_.reset();
    Method_.reset();
    HostHeader_.reset();
    Path_.clear();

    HeadersFlushed_ = false;
    MessageFinished_ = false;
    LastProgressLogTime_ = TInstant::Now();

    Trailers_.Reset();
}

void THttpOutput::SetRequestId(TRequestId requestId)
{
    RequestId_ = requestId;
}

void THttpOutput::WriteRequest(EMethod method, const TString& path)
{
    YT_VERIFY(MessageType_ == EMessageType::Request);

    Method_ = method;
    Path_ = path;
}

std::optional<EStatusCode> THttpOutput::GetStatus() const
{
    return Status_;
}

void THttpOutput::SetStatus(EStatusCode status)
{
    YT_VERIFY(MessageType_ == EMessageType::Response);

    Status_ = status;
}

TSharedRef THttpOutput::GetHeadersPart(std::optional<size_t> contentLength)
{
    TBufferOutput messageHeaders;
    if (MessageType_ == EMessageType::Request) {
        YT_VERIFY(Method_);

        messageHeaders << ToHttpString(*Method_) << " " << Path_ << " HTTP/1.1\r\n";
    } else {
        if (!Status_) {
            Status_ = EStatusCode::OK;
        }

        messageHeaders << "HTTP/1.1 " << static_cast<int>(*Status_) << " " << ToHttpString(*Status_) << "\r\n";
    }

    bool methodNeedsContentLength = Method_ && *Method_ != EMethod::Get && *Method_ != EMethod::Head;

    if (contentLength) {
        if (MessageType_ == EMessageType::Response ||
            (MessageType_ == EMessageType::Request && (*contentLength > 0 || methodNeedsContentLength))) {
            messageHeaders << "Content-Length: " << *contentLength << "\r\n";
        }
    } else {
        messageHeaders << "Transfer-Encoding: chunked\r\n";
    }

    if (ConnectionClose_) {
        messageHeaders << "Connection: close\r\n";
    }

    if (HostHeader_) {
        messageHeaders << "Host: " << *HostHeader_ << "\r\n";
    }

    Headers_->WriteTo(&messageHeaders, &FilteredHeaders());

    TString headers;
    messageHeaders.Buffer().AsString(headers);
    return TSharedRef::FromString(headers);
}

TSharedRef THttpOutput::GetTrailersPart()
{
    TBufferOutput messageTrailers;

    Trailers_->WriteTo(&messageTrailers, &FilteredHeaders());

    TString trailers;
    messageTrailers.Buffer().AsString(trailers);
    return TSharedRef::FromString(trailers);
}

TSharedRef THttpOutput::GetChunkHeader(size_t size)
{
    return TSharedRef::FromString(Format("%llX\r\n", size));
}

void THttpOutput::Flush100Continue()
{
    if (HeadersFlushed_) {
        THROW_ERROR(AnnotateError(TError("Cannot send 100 Continue after headers")));
    }

    Connection_->SetWriteDeadline(TInstant::Now() + Config_->WriteIdleTimeout);
    WaitFor(Connection_->Write(Http100ContinueBuffer()).Apply(OnWriteFinish_))
        .ThrowOnError();
}

TFuture<void> THttpOutput::Write(const TSharedRef& data)
{
    if (MessageFinished_) {
        THROW_ERROR(AnnotateError(TError("Cannot write to finished HTTP message")));
    }

    std::vector<TSharedRef> writeRefs;
    if (!HeadersFlushed_) {
        HeadersFlushed_ = true;
        writeRefs.push_back(GetHeadersPart(std::nullopt));
        writeRefs.push_back(CrLfBuffer());
    }

    if (!data.Empty()) {
        writeRefs.push_back(GetChunkHeader(data.Size()));
        writeRefs.push_back(data);
        writeRefs.push_back(CrLfBuffer());
    }

    Connection_->SetWriteDeadline(TInstant::Now() + Config_->WriteIdleTimeout);
    return Connection_->WriteV(TSharedRefArray(std::move(writeRefs), TSharedRefArray::TMoveParts{}))
        .Apply(OnWriteFinish_);
}

TFuture<void> THttpOutput::Flush()
{
    return VoidFuture;
}

TFuture<void> THttpOutput::Close()
{
    if (MessageFinished_) {
        return VoidFuture;
    }

    if (!HeadersFlushed_) {
        return WriteBody(TSharedRef::MakeEmpty());
    }

    return FinishChunked();
}

TError THttpOutput::AnnotateError(const TError& error)
{
    return error
        << TErrorAttribute("connection_id", Connection_->GetId())
        << TErrorAttribute("request_id", RequestId_);
}

TFuture<void> THttpOutput::FinishChunked()
{
    std::vector<TSharedRef> writeRefs;

    if (Trailers_) {
        writeRefs.push_back(ZeroCrLfBuffer());
        writeRefs.push_back(GetTrailersPart());
        writeRefs.push_back(CrLfBuffer());
    } else {
        writeRefs.push_back(ZeroCrLfCrLfBuffer());
    }

    MessageFinished_ = true;
    Connection_->SetWriteDeadline(TInstant::Now() + Config_->WriteIdleTimeout);
    return Connection_->WriteV(TSharedRefArray(std::move(writeRefs), TSharedRefArray::TMoveParts{}))
        .Apply(OnWriteFinish_);
}

TFuture<void> THttpOutput::WriteBody(const TSharedRef& smallBody)
{
    if (HeadersFlushed_ || MessageFinished_) {
        THROW_ERROR(AnnotateError(TError("Cannot write body to partially flushed HTTP message")));
    }

    TSharedRefArray writeRefs;
    if (Trailers_) {
        writeRefs = TSharedRefArray(
            std::array<TSharedRef, 4>{
                GetHeadersPart(smallBody.Size()),
                GetTrailersPart(),
                CrLfBuffer(),
                smallBody,
            },
            TSharedRefArray::TCopyParts{});
    } else {
        writeRefs = TSharedRefArray(
            std::array<TSharedRef, 3>{
                GetHeadersPart(smallBody.Size()),
                CrLfBuffer(),
                smallBody,
            },
            TSharedRefArray::TCopyParts{});
    }

    HeadersFlushed_ = true;
    MessageFinished_ = true;
    Connection_->SetWriteDeadline(TInstant::Now() + Config_->WriteIdleTimeout);
    return Connection_->WriteV(writeRefs)
        .Apply(OnWriteFinish_);
}

i64 THttpOutput::GetWriteByteCount() const
{
    return Connection_->GetWriteByteCount() - StartByteCount_;
}

void THttpOutput::OnWriteFinish()
{
    Connection_->SetWriteDeadline({});

    auto now = TInstant::Now();
    auto stats = Connection_->GetWriteStatistics();
    if (LastProgressLogTime_ + Config_->WriteIdleTimeout < now) {
        YT_LOG_DEBUG("Writing HTTP message (Requestid: %v, BytesOut: %v, IdleDuration: %v, BusyDuration: %v)",
            RequestId_,
            GetWriteByteCount(),
            stats.IdleDuration - StartStatistics_.IdleDuration,
            stats.BusyDuration - StartStatistics_.BusyDuration);
        LastProgressLogTime_ = now;
    }

    if (MessageType_ == EMessageType::Response) {
        if (HeadersFlushed_ && !HeadersLogged_) {
            HeadersLogged_ = true;
            YT_LOG_DEBUG("Finished writing HTTP headers (RequestId: %v, StatusCode: %v)",
                RequestId_,
                Status_);
        }

        if (MessageFinished_) {
            YT_LOG_DEBUG("Finished writing HTTP response (RequestId: %v, BytesOut: %v)",
                RequestId_,
                GetWriteByteCount());
        }
    }
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NHttp
